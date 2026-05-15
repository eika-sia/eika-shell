#include "builtins.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "../features/completion/path_completion.hpp"
#include "../shell/config/config_paths.hpp"
#include "./alias/alias.hpp"
#include "./env/env.hpp"

namespace builtins {
namespace {

using BuiltinFn = int (*)(shell::ShellState &,
                          const parser::ast::SimpleCommand &,
                          std::vector<shell::diagnostics::Diagnostic> &);

struct BuiltinSpec {
    std::string_view name;
    BuiltinKind kind;
    BuiltinFn run;
    std::string_view summary;
};

std::string get_current_working_directory() {
    char *cwd = getcwd(nullptr, 0);
    if (cwd == nullptr) {
        perror("getcwd");
        return "";
    }

    std::string path = cwd;
    free(cwd);
    return path;
}

std::string get_shell_pwd(const shell::ShellState &state) {
    if (const shell::ShellVariable *pwd = env::find_variable(state, "PWD")) {
        if (!pwd->value.empty()) {
            return pwd->value;
        }
    }

    return get_current_working_directory();
}

bool update_directory_variables(shell::ShellState &state,
                                const std::string &old_pwd) {
    if (!old_pwd.empty()) {
        state.scopes[0].variables["OLDPWD"] =
            shell::ShellVariable{old_pwd, false};
    }

    const std::string new_pwd = get_current_working_directory();
    if (new_pwd.empty()) {
        return false;
    }

    env::set_shell_variable(state, "PWD", new_pwd, 0);
    return true;
}

int run_exit(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
             std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() != 1) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "exit: unexpected arguments");
        return 1;
    }

    int status = 0;
    for (const process::ProcessInfo &proc : state.processes) {
        if (proc.running && kill(proc.pid, SIGKILL) == -1) {
            perror("kill");
            status = 1;
        }
    }

    state.running = false;
    return status;
}

int run_cd(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
           std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() > 2) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "cd: too many arguments");
        return 1;
    }

    std::string target;
    bool print_new_pwd = false;

    if (cmd.invocation->words.size() == 1) {
        const shell::ShellVariable *home = env::find_variable(state, "HOME");
        if (home == nullptr || home->value.empty()) {
            shell::diagnostics::add_error(
                diagnostics, cmd.span,
                "cd: only 1 argument provided but HOME not set");
            return 1;
        }
        target = home->value;
    } else if (cmd.invocation->words[1].text == "-") {
        const shell::ShellVariable *oldpwd =
            env::find_variable(state, "OLDPWD");
        if (oldpwd == nullptr || oldpwd->value.empty()) {
            shell::diagnostics::add_error(diagnostics, cmd.span,
                                          "cd: OLDPWD not set");
            return 1;
        }

        target = oldpwd->value;
        print_new_pwd = true;
    } else {
        target = cmd.invocation->words[1].text;
    }

    const std::string old_pwd = get_shell_pwd(state);
    if (chdir(target.c_str()) == -1) {
        perror("cd");
        return 1;
    }

    if (!update_directory_variables(state, old_pwd)) {
        return 1;
    }

    if (print_new_pwd) {
        std::cout << env::get_variable_value(state, "PWD") << std::endl;
    }

    return 0;
}

int run_pwd(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
            std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() != 1) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "pwd: unexpected arguments");
        return 1;
    }

    const std::string pwd = get_shell_pwd(state);
    if (pwd.empty()) {
        return 1;
    }

    std::cout << pwd << std::endl;
    return 0;
}

std::string describe_command_type(const shell::ShellState &state,
                                  const std::string &name) {
    if (const auto alias_it = state.alias.find(name);
        alias_it != state.alias.end()) {
        return name + " is an alias for " + alias_it->second;
    }

    if (is_builtin_name(name)) {
        return name + " is a shell builtin";
    }

    if (features::looks_like_path_token(name)) {
        if (features::path_is_executable_file(state, name)) {
            return name + " is " + name;
        }
        return "";
    }

    const std::string full_path =
        features::resolve_command_in_path(state, name);
    if (!full_path.empty()) {
        return name + " is " + full_path;
    }

    return "";
}

int run_type(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
             std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() < 2) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "type: expected arguments");
        return 1;
    }

    int status = 0;
    for (size_t i = 1; i < cmd.invocation->words.size(); ++i) {
        const std::string description =
            describe_command_type(state, cmd.invocation->words[i].text);
        if (description.empty()) {
            shell::diagnostics::add_error(
                diagnostics, cmd.invocation->words[i].span,
                "type: " + cmd.invocation->words[i].text + " not found");
            status = 1;
            continue;
        }

        std::cout << description << std::endl;
    }

    return status;
}

int run_help(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
             std::vector<shell::diagnostics::Diagnostic> &diagnostics);

int run_source(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
               std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() != 2) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "source: unexpected arguments");
        return 1;
    }

    return source_file(state, cmd.invocation->words[1].text, false);
}

int run_history(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
                std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() > 2) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "history: unexpected arguments");
        return 1;
    }

    size_t start = 0;
    if (cmd.invocation->words.size() == 2) {
        int count = 0;
        try {
            count = std::stoi(cmd.invocation->words[1].text);
        } catch (const std::invalid_argument &) {
            shell::diagnostics::add_error(diagnostics, cmd.span,
                                          "history: invalid argument");
            return 1;
        } catch (const std::out_of_range &) {
            shell::diagnostics::add_error(diagnostics, cmd.span,
                                          "history: invalid number");
            return 1;
        }

        if (count < 0) {
            shell::diagnostics::add_error(diagnostics, cmd.span,
                                          "history: invalid argument");
            return 1;
        }

        const size_t limit = static_cast<size_t>(count);
        if (limit < state.history.size()) {
            start = state.history.size() - limit;
        }
    }

    for (size_t i = start; i < state.history.size(); ++i) {
        std::cout << i + 1 << " " << state.history[i] << std::endl;
    }
    return 0;
}

int run_ps(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
           std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() != 1) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "ps: unexpected arguments");
        return 1;
    }

    std::cout << "PID   Name\n";
    for (const process::ProcessInfo &proc : state.processes) {
        if (proc.running) {
            std::cout << proc.pid << " " << proc.command << std::endl;
        }
    }

    return 0;
}

int run_kill(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
             std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() != 3) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "kill: unexpected arguments");
        return 1;
    }

    int signal_number = 0;
    pid_t pid = 0;
    try {
        signal_number = std::stoi(cmd.invocation->words[2].text);
        pid = std::stoi(cmd.invocation->words[1].text);
    } catch (const std::invalid_argument &) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "kill: invalid arguments");
        return 1;
    } catch (const std::out_of_range &) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "kill: number out of range");
        return 1;
    }

    const process::ProcessInfo *proc = process::find_process(state, pid);
    if (proc == nullptr) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "kill: process with PID " +
                                          std::to_string(pid) + " not found");
        return 1;
    }

    if (!proc->running) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "kill: process with PID " +
                                          std::to_string(pid) + " not running");
        return 1;
    }

    if (kill(pid, signal_number) == -1) {
        perror("kill");
        return 1;
    }

    process::reap_process_with_poll(state, pid);
    return 0;
}

int run_alias(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
              std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    return (cmd.invocation->words.size() == 1)
               ? run_alias_list(state, cmd, diagnostics)
               : run_alias_manage(state, cmd, diagnostics);
}

const std::array<BuiltinSpec, 15> &builtin_specs() {
    static const std::array<BuiltinSpec, 15> specs{{
        {"exit", BuiltinKind::Exit, run_exit, "exit the shell"},
        {"cd", BuiltinKind::Cd, run_cd, "change the working directory"},
        {"pwd", BuiltinKind::Pwd, run_pwd, "print the working directory"},
        {"type", BuiltinKind::Type, run_type, "describe how commands resolve"},
        {"help", BuiltinKind::Help, run_help, "show builtin help"},
        {"source", BuiltinKind::Source, run_source, "run commands from a file"},
        {"history", BuiltinKind::History, run_history, "show command history"},
        {"ps", BuiltinKind::Ps, run_ps, "show tracked processes"},
        {"kill", BuiltinKind::Kill, run_kill,
         "send a signal to a tracked process"},
        {"alias", BuiltinKind::Alias, run_alias, "list or create aliases"},
        {"unalias", BuiltinKind::Unalias, run_alias_manage, "remove an alias"},
        {"set", BuiltinKind::Set, env::run_set, "list shell variables"},
        {"let", BuiltinKind::Let, env::run_let, "list shell variables"},
        {"export", BuiltinKind::Export, env::run_export,
         "list or export variables"},
        {"unset", BuiltinKind::Unset, env::run_unset, "remove shell variables"},
    }};

    return specs;
}

const BuiltinSpec *find_builtin_spec(std::string_view name) {
    for (const BuiltinSpec &spec : builtin_specs()) {
        if (spec.name == name) {
            return &spec;
        }
    }

    return nullptr;
}

bool can_run_builtin_in_child(const parser::ast::SimpleCommand &cmd,
                              BuiltinKind kind) {
    switch (kind) {
    case BuiltinKind::Pwd:
    case BuiltinKind::Type:
    case BuiltinKind::Help:
    case BuiltinKind::History:
    case BuiltinKind::Ps:
    case BuiltinKind::Set:
    case BuiltinKind::Let:
        return true;
    case BuiltinKind::Alias:
    case BuiltinKind::Export:
        return cmd.invocation && cmd.invocation->words.size() == 1;
    case BuiltinKind::None:
    case BuiltinKind::Exit:
    case BuiltinKind::Cd:
    case BuiltinKind::Source:
    case BuiltinKind::Kill:
    case BuiltinKind::Unalias:
    case BuiltinKind::Unset:
        return false;
    }

    return false;
}

int run_help(shell::ShellState &, const parser::ast::SimpleCommand &cmd,
             std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() != 1) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "help: unexpected arguments");
        return 1;
    }

    std::cout << "eika shell beta v1.2\n";
    std::cout << "builtins:\n";
    for (const BuiltinSpec &spec : builtin_specs()) {
        std::cout << "  " << spec.name << " - " << spec.summary << '\n';
    }
    return 0;
}

} // namespace

int source_file(shell::ShellState &state, const std::string &path,
                bool silent_missing) {
    const std::string resolved_path =
        shell::config::resolve_source_path(state, path);
    if (resolved_path.empty()) {
        if (!silent_missing) {
            std::cerr << "source: " << path << ": not found\n";
        }
        return 1;
    }

    std::ifstream file(resolved_path);
    if (!file.is_open()) {
        if (!silent_missing) {
            perror(path.c_str());
        }
        return 1;
    }

    shell::ExecuteOptions options{};
    options.save_history = false;
    return shell::execute_stream(state, file, options);
}

BuiltinPlan plan_builtin(const parser::ast::SimpleCommand &cmd,
                         ExecContext ctx) {
    if (!cmd.invocation || cmd.invocation->words.empty()) {
        return BuiltinPlan{BuiltinKind::None, BuiltinDecision::External};
    }

    const BuiltinSpec *spec = find_builtin_spec(cmd.invocation->words[0].text);
    if (spec == nullptr) {
        return BuiltinPlan{BuiltinKind::None, BuiltinDecision::External};
    }

    if (ctx == ExecContext::ForegroundStandalone) {
        return BuiltinPlan{spec->kind, BuiltinDecision::RunInParent};
    }

    if (can_run_builtin_in_child(cmd, spec->kind)) {
        return BuiltinPlan{spec->kind, BuiltinDecision::RunInChild};
    }

    return BuiltinPlan{spec->kind, BuiltinDecision::Reject};
}

int run_builtin(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
                BuiltinKind kind,
                std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    for (const BuiltinSpec &spec : builtin_specs()) {
        if (spec.kind == kind) {
            return spec.run(state, cmd, diagnostics);
        }
    }

    return -1;
}

bool is_builtin_name(const std::string &name) {
    return find_builtin_spec(name) != nullptr;
}

std::vector<std::string> builtin_names() {
    std::vector<std::string> names;
    names.reserve(builtin_specs().size());

    for (const BuiltinSpec &spec : builtin_specs()) {
        names.push_back(std::string(spec.name));
    }

    return names;
}

} // namespace builtins
