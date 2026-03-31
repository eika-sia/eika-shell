#include "builtins.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <signal.h>
#include <string>
#include <unistd.h>

#include "../features/alias/alias.hpp"

int run_exit(ShellState &state, const Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "exit: unexpected arguments\n";
        return 1;
    }

    int status = 0;
    for (const ProcessInfo &proc : state.processes) {
        if (proc.running && kill(proc.pid, SIGKILL) == -1) {
            perror("kill");
            status = 1;
        }
    }

    state.running = false;
    return status;
}

int run_cd(const Command &cmd) {
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
    int res = chdir(cmd.args[1].c_str());

    if (!res)
        return 0;

    perror("cd");
    return 1;
}

int run_history(ShellState &state, const Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "history: unexpected arguments\n";
        return 1;
    }

    for (size_t i = 0; i < state.history.size(); ++i) {
        std::cout << i + 1 << " " << state.history[i] << std::endl;
    }
    return 0;
}

int run_ps(ShellState &state, const Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "ps: unexpected arguments\n";
        return 1;
    }

    cleanup_finished_processes(state);

    std::cout << "PID   Name\n";
    for (const ProcessInfo &proc : state.processes) {
        if (proc.running)
            std::cout << proc.pid << " " << proc.command << std::endl;
    }

    return 0;
}

int run_kill(ShellState &state, const Command &cmd) {
    if (cmd.args.size() != 3) {
        std::cerr << "kill: unexpected arguments\n";
        return 1;
    }

    int signal = 0;
    pid_t pid = 0;
    try {
        signal = std::stoi(cmd.args[2]);
        pid = std::stoi(cmd.args[1]);
    } catch (const std::invalid_argument &e) {
        std::cerr << "kill: invalid argument" << std::endl;
        return 1;
    } catch (const std::out_of_range &e) {
        std::cerr << "kill: number out of range" << std::endl;
        return 1;
    }

    bool found = false;
    bool running = false;
    for (const ProcessInfo &proc : state.processes) {
        if (proc.pid == pid) {
            found = true;
            running = proc.running;
        }
    }

    if (!found) {
        std::cerr << "kill: process with PID " << pid << " not found\n";
        return 1;
    }
    if (!running) {
        std::cerr << "kill: process with PID " << pid << " not running\n";
        return 1;
    }

    if (kill(pid, signal) == -1) {
        perror("kill");
        return 1;
    }

    cleanup_finished_processes(state);
    return 0;
}

BuiltinKind classify_builtin(const Command &cmd) {
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
        if (cmd.args.size() == 1) {
            return BuiltinKind::AliasList;
        } else {
            return BuiltinKind::AliasSet;
        }
    }

    return BuiltinKind::None;
}

BuiltinDecision decide_builtin(const Command &cmd, ExecContext ctx) {
    const BuiltinKind kind = classify_builtin(cmd);
    if (kind == BuiltinKind::None) {
        return BuiltinDecision::External;
    }

    if (ctx == ExecContext::ForegroundStandalone) {
        return BuiltinDecision::RunInParent;
    }

    if (kind == BuiltinKind::History || kind == BuiltinKind::Ps ||
        kind == BuiltinKind::Kill || kind == BuiltinKind::AliasList) {
        return BuiltinDecision::RunInChild;
    }

    return BuiltinDecision::Reject;
}

BuiltinPlan plan_builtin(const Command &cmd, ExecContext ctx) {
    BuiltinPlan plan{};

    plan.decision = decide_builtin(cmd, ctx);
    plan.kind = classify_builtin(cmd);

    return plan;
}

int run_builtin(ShellState &state, const Command &cmd, BuiltinKind kind) {
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

bool handle_builtin(ShellState &state, const Command &cmd) {
    return run_builtin(state, cmd, classify_builtin(cmd)) != -1;
}
