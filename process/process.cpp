#include "process.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <sys/wait.h>
#include <vector>

#include "../shell/shell.hpp"

namespace process {
namespace {

bool process_reaper(shell::ShellState &state, pid_t pid, int options,
                    int *raw_wait_status = nullptr,
                    pid_t *reaped_pid = nullptr) {
    int status = 0;

    while (true) {
        pid_t waited = waitpid(pid, &status, options);
        if (waited > 0) {
            mark_process_finished(state, waited, status);
            if (raw_wait_status) {
                *raw_wait_status = status;
            }
            if (reaped_pid) {
                *reaped_pid = waited;
            }
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

} // namespace

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
                 const std::string &command, bool background) {
    ProcessInfo new_proc{};
    new_proc.background = background;
    new_proc.running = true;
    new_proc.command = command;
    new_proc.pgid = pgid;
    new_proc.pid = pid;

    state.processes.push_back(new_proc);
}

void mark_process_finished(shell::ShellState &state, pid_t pid,
                           int raw_wait_status) {
    if (ProcessInfo *proc = find_process(state, pid)) {
        proc->running = false;
        proc->raw_wait_status = raw_wait_status;
        proc->has_wait_status = true;
    }
}

void cleanup_finished_processes(shell::ShellState &state) {
    while (process_reaper(state, -1, WNOHANG)) {
    }

    state.processes.erase(
        std::remove_if(state.processes.begin(), state.processes.end(),
                       [](const ProcessInfo &proc) {
                           return !proc.running && proc.has_wait_status;
                       }),
        state.processes.end());
}

int shell_status_from_wait_status(int raw_wait_status) {
    // Shell conditionals and `$?` consume normalized shell exit codes
    if (WIFEXITED(raw_wait_status)) {
        return WEXITSTATUS(raw_wait_status);
    }

    if (WIFSIGNALED(raw_wait_status)) {
        return 128 + WTERMSIG(raw_wait_status);
    }

    return 1;
}

int wait_for_processes(shell::ShellState &state,
                       const std::vector<pid_t> &pids) {
    int last_status = 0;

    for (pid_t pid : pids) {
        int raw_wait_status = 0;
        if (process_reaper(state, pid, 0, &raw_wait_status)) {
            last_status = shell_status_from_wait_status(raw_wait_status);
            continue;
        }

        const ProcessInfo *proc = find_process(state, pid);
        if (proc && proc->has_wait_status) {
            last_status = shell_status_from_wait_status(proc->raw_wait_status);
        } else {
            last_status = 1;
        }
    }

    return last_status;
}

} // namespace process
