#include "builtins.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string>
#include <unistd.h>

#include "./alias/alias.hpp"
#include "./env/env.hpp"
#include "../features/completion/path_completion.hpp"

namespace builtins {
namespace {

void update_pwd(shell::ShellState &state) {
    char *cwd = getcwd(nullptr, 0);
    if (cwd == nullptr) {
        perror("getcwd");
        return;
    }

    env::set_shell_variable(state, "PWD", cwd);
    free(cwd);
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
    if (cmd.args.size() == 1) {
        const shell::ShellVariable *home = env::find_variable(state, "HOME");
        if (home != nullptr) {
            if (chdir(home->value.c_str()) == 0) {
                update_pwd(state);
                return 0;
            }
            perror("cd");
        } else {
            std::cerr << "cd: only 1 argument provided and HOME is not set\n";
        }
        return 1;
    }

    if (cmd.args.size() > 2) {
        std::cerr << "cd: too many arguments\n";
        return 1;
    }

    if (chdir(cmd.args[1].c_str()) == 0) {
        update_pwd(state);
        return 0;
    }

    perror("cd");
    return 1;
}

int run_pwd(const shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "pwd: unexpected arguments\n";
        return 1;
    }

    if (const shell::ShellVariable *pwd = env::find_variable(state, "PWD")) {
        std::cout << pwd->value << std::endl;
        return 0;
    }

    char *cwd = getcwd(nullptr, 0);
    if (cwd == nullptr) {
        perror("getcwd");
        return 1;
    }

    std::cout << cwd << std::endl;
    free(cwd);
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

    const std::string full_path = features::resolve_command_in_path(state, name);
    if (!full_path.empty()) {
        return name + " is " + full_path;
    }

    return "";
}

int run_type(const shell::ShellState &state, const parser::Command &cmd) {
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

int run_source(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() != 2) {
        std::cerr << "source: unexpected arguments\n";
        return 1;
    }

    std::ifstream file(cmd.args[1]);
    if (!file.is_open()) {
        perror("source");
        return 1;
    }

    std::string line;
    while (std::getline(file, line)) {
        shell::execute_command_line(state, line);
        if (!state.running) {
            break;
        }
    }

    return state.last_status;
}

int run_history(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "history: unexpected arguments\n";
        return 1;
    }

    for (size_t i = 0; i < state.history.size(); ++i) {
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

    int signal = 0;
    pid_t pid = 0;
    try {
        signal = std::stoi(cmd.args[2]);
        pid = std::stoi(cmd.args[1]);
    } catch (const std::invalid_argument &) {
        std::cerr << "kill: invalid argument" << std::endl;
        return 1;
    } catch (const std::out_of_range &) {
        std::cerr << "kill: number out of range" << std::endl;
        return 1;
    }

    const process::ProcessInfo *proc = process::find_process(state, pid);
    if (!proc) {
        std::cerr << "kill: process with PID " << pid << " not found\n";
        return 1;
    }

    if (!proc->running) {
        std::cerr << "kill: process with PID " << pid << " not running\n";
        return 1;
    }

    if (kill(pid, signal) == -1) {
        perror("kill");
        return 1;
    }

    return 0;
}

BuiltinKind classify_builtin(const parser::Command &cmd) {
    if (cmd.args.empty()) {
        return BuiltinKind::None;
    }

    const std::string &first = cmd.args[0];
    if (first == "exit") {
        return BuiltinKind::Exit;
    }
    if (first == "cd") {
        return BuiltinKind::Cd;
    }
    if (first == "pwd") {
        return BuiltinKind::Pwd;
    }
    if (first == "type") {
        return BuiltinKind::Type;
    }
    if (first == "source") {
        return BuiltinKind::Source;
    }
    if (first == "history") {
        return BuiltinKind::History;
    }
    if (first == "ps") {
        return BuiltinKind::Ps;
    }
    if (first == "kill") {
        return BuiltinKind::Kill;
    }
    if (first == "alias") {
        return (cmd.args.size() == 1) ? BuiltinKind::AliasList
                                      : BuiltinKind::AliasManage;
    }
    if (first == "unalias") {
        return BuiltinKind::AliasManage;
    }
    if (first == "set") {
        return BuiltinKind::SetList;
    }
    if (first == "export") {
        return (cmd.args.size() == 1) ? BuiltinKind::ExportList
                                      : BuiltinKind::ExportManage;
    }
    if (first == "unset") {
        return BuiltinKind::ExportManage;
    }

    return BuiltinKind::None;
}

BuiltinDecision decide_builtin(BuiltinKind kind, ExecContext ctx) {
    if (kind == BuiltinKind::None) {
        return BuiltinDecision::External;
    }

    if (ctx == ExecContext::ForegroundStandalone) {
        return BuiltinDecision::RunInParent;
    }

    if (kind == BuiltinKind::Pwd || kind == BuiltinKind::Type ||
        kind == BuiltinKind::History || kind == BuiltinKind::Ps ||
        kind == BuiltinKind::AliasList || kind == BuiltinKind::SetList ||
        kind == BuiltinKind::ExportList) {
        return BuiltinDecision::RunInChild;
    }

    return BuiltinDecision::Reject;
}

} // namespace

BuiltinPlan plan_builtin(const parser::Command &cmd, ExecContext ctx) {
    const BuiltinKind kind = classify_builtin(cmd);
    return BuiltinPlan{kind, decide_builtin(kind, ctx)};
}

int run_builtin(shell::ShellState &state, const parser::Command &cmd,
                BuiltinKind kind) {
    switch (kind) {
    case BuiltinKind::Exit:
        return run_exit(state, cmd);
    case BuiltinKind::Cd:
        return run_cd(state, cmd);
    case BuiltinKind::Pwd:
        return run_pwd(state, cmd);
    case BuiltinKind::Type:
        return run_type(state, cmd);
    case BuiltinKind::Source:
        return run_source(state, cmd);
    case BuiltinKind::History:
        return run_history(state, cmd);
    case BuiltinKind::Ps:
        return run_ps(state, cmd);
    case BuiltinKind::Kill:
        return run_kill(state, cmd);
    case BuiltinKind::AliasManage:
        return run_alias_manage(state, cmd);
    case BuiltinKind::AliasList:
        return run_alias_list(state, cmd);
    case BuiltinKind::SetList:
        return env::run_set_list(state, cmd);
    case BuiltinKind::ExportManage:
        return env::run_export_manage(state, cmd);
    case BuiltinKind::ExportList:
        return env::run_export_list(state, cmd);
    case BuiltinKind::None:
        return -1;
    }

    return -1;
}

bool is_builtin_name(const std::string &name) {
    parser::Command fake_cmd{};
    fake_cmd.args.push_back(name);
    return classify_builtin(fake_cmd) != BuiltinKind::None;
}

} // namespace builtins
