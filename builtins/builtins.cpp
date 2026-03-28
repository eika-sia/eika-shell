#include "builtins.hpp"

#include <cstdio>
#include <iostream>
#include <signal.h>
#include <string>
#include <unistd.h>

void handle_exit(ShellState &state, const Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "exit: unexpected arguments\n";
        return;
    }

    for (const ProcessInfo &proc : state.processes) {
        if (proc.running)
            kill(proc.pid, SIGKILL);
    }

    state.running = false;
    return;
}

void handle_cd(const Command &cmd) {
    if (cmd.args.size() == 1) {
        if (getenv("HOME"))
            chdir(getenv("HOME"));
        else
            std::cerr << "cd: only 1 argument provided and HOME is not set\n";
        return;
    }

    if (cmd.args.size() > 2) {
        std::cerr << "cd: too many arguments\n";
        return;
    }
    int res = chdir(cmd.args[1].c_str());

    if (!res)
        return;

    perror("cd");
}

void handle_history(ShellState &state, const Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "history: unexpected arguments\n";
        return;
    }

    for (size_t i = 0; i < state.history.size(); ++i) {
        std::cout << i + 1 << " " << state.history[i] << std::endl;
    }
    return;
}

void handle_ps(ShellState &state, const Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "ps: unexpected arguments\n";
        return;
    }

    cleanup_finished_processes(state);

    std::cout << "PID   Name\n";
    for (const ProcessInfo &proc : state.processes) {
        if (proc.running)
            std::cout << proc.pid << " " << proc.command << std::endl;
    }
}

void handle_kill(ShellState &state, const Command &cmd) {
    if (cmd.args.size() != 3) {
        std::cerr << "kill: unexpected arguments\n";
        return;
    }

    int signal = 0;
    pid_t pid = 0;
    try {
        signal = std::stoi(cmd.args[2]);
        pid = std::stoi(cmd.args[1]);
    } catch (const std::invalid_argument &e) {
        std::cerr << "kill: invalid argument" << std::endl;
        return;
    } catch (const std::out_of_range &e) {
        std::cerr << "kill: number out of range" << std::endl;
        return;
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
        return;
    }
    if (!running) {
        std::cerr << "kill: process with PID " << pid << " not running\n";
        return;
    }

    if (kill(pid, signal) == -1)
        perror("kill");

    cleanup_finished_processes(state);
    return;
}

// res returna true ako je matchao s pravom komandom
bool handle_builtin(ShellState &state, const Command &cmd) {
    if (cmd.args.size() == 0) {
        return false;
    }
    std::string first = cmd.args[0];
    bool res = false;

    if (first == "exit") {
        res = true;
        handle_exit(state, cmd);
    } else if (first == "cd") {
        res = true;
        handle_cd(cmd);
    } else if (first == "history") {
        res = true;
        handle_history(state, cmd);
    } else if (first == "ps") {
        res = true;
        handle_ps(state, cmd);
    } else if (first == "kill") {
        res = true;
        handle_kill(state, cmd);
    }
    return res;
}
