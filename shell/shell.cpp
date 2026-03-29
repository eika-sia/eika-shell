#include "shell.hpp"

#include <cstdio>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "../builtins/builtins.hpp"
#include "../features/alias/alias.hpp"
#include "../features/expansion/expansion.hpp"
#include "../features/history/history.hpp"
#include "../parser/parser.hpp"
#include "./exec/exec.hpp"
#include "signals/signals.hpp"
#include "terminal/terminal.hpp"

void init_shell(ShellState &state) {
    state.shell_pgid = getpgrp();
    init_terminal(state);
    install_signal_handlers();
}

void cleanup_finished_processes(ShellState &state) {
    int status = 0;

    while (true) {
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            mark_process_finished(state, pid);
            continue;
        }
        // za pid=0 (nista) ili pid=-1 (error) odustanemo
        break;
    }
}

void add_process(ShellState &state, pid_t pid, const Command &cmd) {
    ProcessInfo new_proc = ProcessInfo{};
    new_proc.background = cmd.background;
    new_proc.running = true;
    new_proc.command = cmd.raw;
    new_proc.pgid = pid;
    new_proc.pid = pid;

    state.processes.push_back(new_proc);
}

void mark_process_finished(ShellState &state, pid_t pid) {
    for (size_t i = 0; i < state.processes.size(); ++i) {
        if (state.processes[i].pid == pid) {
            state.processes[i].running = false;
            break;
        }
    }
}

std::string trim(const std::string &source) {
    std::string s(source);
    s.erase(0, s.find_first_not_of(" \n\r\t"));
    s.erase(s.find_last_not_of(" \n\r\t") + 1);
    return s;
}

void execute_command_line(ShellState &state, std::string line) {
    line = trim(line);
    if (line.empty())
        return;

    if (!expand_history(state, line))
        return;

    CommandList command_line = parse_command_line(line);
    if (!command_line.valid) {
        return;
    }

    for (Pipeline pipe : command_line.pipelines) {

        for (Command &cmd : pipe.commands) {
            if (!expand_command(state, cmd)) {
                return;
            }

            cmd.background = pipe.background;
        }

        if (pipe.commands.size() == 1 &&
            (handle_builtin(state, pipe.commands[0]) ||
             handle_alias_builtin(state, pipe.commands[0]))) {
            if (!state.running) {
                break;
            }
            continue;
        }

        launch_pipeline(state, pipe);
        if (!state.running) {
            break;
        }
    }
}
