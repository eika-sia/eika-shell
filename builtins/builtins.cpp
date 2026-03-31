#include "builtins.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <signal.h>
#include <string>
#include <unistd.h>

#include "./alias/alias.hpp"

namespace builtins {
namespace {

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

int run_cd(const parser::Command &cmd) {
    if (cmd.args.size() == 1) {
        if (getenv("HOME")) {
            if (chdir(getenv("HOME")) == 0) {
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
        return 0;
    }

    perror("cd");
    return 1;
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
                                      : BuiltinKind::AliasSet;
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

    if (kind == BuiltinKind::History || kind == BuiltinKind::Ps ||
        kind == BuiltinKind::AliasList) {
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
        return run_cd(cmd);
    case BuiltinKind::History:
        return run_history(state, cmd);
    case BuiltinKind::Ps:
        return run_ps(state, cmd);
    case BuiltinKind::Kill:
        return run_kill(state, cmd);
    case BuiltinKind::AliasSet:
        return run_alias_set(state, cmd);
    case BuiltinKind::AliasList:
        return run_alias_list(state, cmd);
    case BuiltinKind::None:
        return -1;
    }

    return -1;
}

} // namespace builtins
