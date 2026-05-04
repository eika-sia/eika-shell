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

#include "../features/completion/path_completion.hpp"
#include "./alias/alias.hpp"
#include "./env/env.hpp"

namespace builtins {
namespace {

using BuiltinFn = int (*)(shell::ShellState &, const parser::Command &);

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

void set_local_shell_variable(shell::ShellState &state, std::string name,
                              std::string value) {
    state.variables[name] = shell::ShellVariable{value, false};
}

bool update_directory_variables(shell::ShellState &state,
                                const std::string &old_pwd) {
    if (!old_pwd.empty()) {
        set_local_shell_variable(state, "OLDPWD", old_pwd);
    }

    const std::string new_pwd = get_current_working_directory();
    if (new_pwd.empty()) {
        return false;
    }

    env::set_shell_variable(state, "PWD", new_pwd);
    return true;
}

int run_exit(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "exit: unexpected arguments\n";
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

int run_cd(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() > 2) {
        std::cerr << "cd: too many arguments\n";
        return 1;
    }

    std::string target;
    bool print_new_pwd = false;

    if (cmd.args.size() == 1) {
        const shell::ShellVariable *home = env::find_variable(state, "HOME");
        if (home == nullptr || home->value.empty()) {
            std::cerr << "cd: only 1 argument provided and HOME is not set\n";
            return 1;
        }
        target = home->value;
    } else if (cmd.args[1] == "-") {
        const shell::ShellVariable *oldpwd =
            env::find_variable(state, "OLDPWD");
        if (oldpwd == nullptr || oldpwd->value.empty()) {
            std::cerr << "cd: OLDPWD not set\n";
            return 1;
        }

        target = oldpwd->value;
        print_new_pwd = true;
    } else {
        target = cmd.args[1];
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

int run_pwd(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "pwd: unexpected arguments\n";
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

int run_type(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() < 2) {
        std::cerr << "type: unexpected arguments\n";
        return 1;
    }

    int status = 0;
    for (size_t i = 1; i < cmd.args.size(); ++i) {
        const std::string description =
            describe_command_type(state, cmd.args[i]);
        if (description.empty()) {
            std::cerr << "type: " << cmd.args[i] << ": not found\n";
            status = 1;
            continue;
        }

        std::cout << description << std::endl;
    }

    return status;
}

int run_help(shell::ShellState &, const parser::Command &cmd);

int run_source(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() != 2) {
        std::cerr << "source: unexpected arguments\n";
        return 1;
    }

    return source_file(state, cmd.args[1], false);
}

int run_history(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() > 2) {
        std::cerr << "history: unexpected arguments\n";
        return 1;
    }

    size_t start = 0;
    if (cmd.args.size() == 2) {
        int count = 0;
        try {
            count = std::stoi(cmd.args[1]);
        } catch (const std::invalid_argument &) {
            std::cerr << "history: invalid argument" << std::endl;
            return 1;
        } catch (const std::out_of_range &) {
            std::cerr << "history: number out of range" << std::endl;
            return 1;
        }

        if (count < 0) {
            std::cerr << "history: invalid argument" << std::endl;
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

int run_ps(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "ps: unexpected arguments\n";
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

int run_kill(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() != 3) {
        std::cerr << "kill: unexpected arguments\n";
        return 1;
    }

    int signal_number = 0;
    pid_t pid = 0;
    try {
        signal_number = std::stoi(cmd.args[2]);
        pid = std::stoi(cmd.args[1]);
    } catch (const std::invalid_argument &) {
        std::cerr << "kill: invalid argument" << std::endl;
        return 1;
    } catch (const std::out_of_range &) {
        std::cerr << "kill: number out of range" << std::endl;
        return 1;
    }

    const process::ProcessInfo *proc = process::find_process(state, pid);
    if (proc == nullptr) {
        std::cerr << "kill: process with PID " << pid << " not found\n";
        return 1;
    }

    if (!proc->running) {
        std::cerr << "kill: process with PID " << pid << " not running\n";
        return 1;
    }

    if (kill(pid, signal_number) == -1) {
        perror("kill");
        return 1;
    }

    process::reap_process_with_poll(state, pid);
    return 0;
}

int run_alias(shell::ShellState &state, const parser::Command &cmd) {
    return (cmd.args.size() == 1) ? run_alias_list(state, cmd)
                                  : run_alias_manage(state, cmd);
}

int run_unalias(shell::ShellState &state, const parser::Command &cmd) {
    return run_alias_manage(state, cmd);
}

const std::array<BuiltinSpec, 14> &builtin_specs() {
    static const std::array<BuiltinSpec, 14> specs{{
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
        {"unalias", BuiltinKind::Unalias, run_unalias, "remove an alias"},
        {"set", BuiltinKind::Set, env::run_set, "list shell variables"},
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

const BuiltinSpec *find_builtin_spec(BuiltinKind kind) {
    for (const BuiltinSpec &spec : builtin_specs()) {
        if (spec.kind == kind) {
            return &spec;
        }
    }

    return nullptr;
}

bool can_run_builtin_in_child(const parser::Command &cmd, BuiltinKind kind) {
    switch (kind) {
    case BuiltinKind::Pwd:
    case BuiltinKind::Type:
    case BuiltinKind::Help:
    case BuiltinKind::History:
    case BuiltinKind::Ps:
    case BuiltinKind::Set:
        return true;
    case BuiltinKind::Alias:
    case BuiltinKind::Export:
        return cmd.args.size() == 1;
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

int run_help(shell::ShellState &, const parser::Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "help: unexpected arguments\n";
        return 1;
    }

    std::cout << "eika shell\n";
    std::cout << "builtins:\n";
    for (const BuiltinSpec &spec : builtin_specs()) {
        std::cout << "  " << spec.name << " - " << spec.summary << '\n';
    }
    return 0;
}

} // namespace

int source_stream(shell::ShellState &state, std::istream &stream) {
    shell::ExecuteOptions options{};
    options.save_history = false;
    return shell::execute_stream(state, stream, options);
}

int source_file(shell::ShellState &state, const std::string &path,
                bool silent_missing) {
    std::ifstream file(path);
    if (!file.is_open()) {
        if (!silent_missing) {
            perror(path.c_str());
        }
        return 1;
    }

    return source_stream(state, file);
}

BuiltinPlan plan_builtin(const parser::Command &cmd, ExecContext ctx) {
    if (cmd.args.empty()) {
        return BuiltinPlan{BuiltinKind::None, BuiltinDecision::External};
    }

    const BuiltinSpec *spec = find_builtin_spec(cmd.args[0]);
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

int run_builtin(shell::ShellState &state, const parser::Command &cmd,
                BuiltinKind kind) {
    const BuiltinSpec *spec = find_builtin_spec(kind);
    if (spec == nullptr) {
        return -1;
    }

    return spec->run(state, cmd);
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
