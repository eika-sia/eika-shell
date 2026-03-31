#include "process.hpp"

#include <cerrno>
#include <cstdio>
#include <sys/wait.h>
#include <vector>

#include "../shell/shell.hpp"

namespace process {

ProcessInfo *find_process(shell::ShellState &state, pid_t pid) {
    for (ProcessInfo &proc : state.processes) {
        if (proc.pid == pid) {
            return &proc;
        }
    }

    return nullptr;
}

const ProcessInfo *find_process(const shell::ShellState &state, pid_t pid) {
    for (const ProcessInfo &proc : state.processes) {
        if (proc.pid == pid) {
            return &proc;
        }
    }

    return nullptr;
}

void add_process(shell::ShellState &state, pid_t pid, pid_t pgid,
                 const parser::Command &cmd) {
    ProcessInfo new_proc{};
    new_proc.background = cmd.background;
    new_proc.running = true;
    new_proc.command = cmd.raw;
    new_proc.pgid = pgid;
    new_proc.pid = pid;

    state.processes.push_back(new_proc);
}

void mark_process_finished(shell::ShellState &state, pid_t pid) {
    if (ProcessInfo *proc = find_process(state, pid)) {
        proc->running = false;
    }
}

bool reap_process(shell::ShellState &state, pid_t pid, int options) {
    int status = 0;

    while (true) {
        pid_t waited = waitpid(pid, &status, options);
        if (waited > 0) {
            mark_process_finished(state, waited);
            return true;
        }

        if (waited == 0) {
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno != ECHILD) {
            perror("waitpid");
        }
        return false;
    }
}

void cleanup_finished_processes(shell::ShellState &state) {
    while (reap_process(state, -1, WNOHANG)) {
    }
}

void wait_for_processes(shell::ShellState &state,
                        const std::vector<pid_t> &pids) {
    for (pid_t pid : pids) {
        reap_process(state, pid, 0);
    }
}

} // namespace process
