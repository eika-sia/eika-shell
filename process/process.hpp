#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

namespace shell {
struct ShellState;
}

namespace process {

struct ProcessInfo {
    pid_t pid = -1;
    pid_t pgid = -1;
    std::string command;
    bool running = false;
    bool background = false;
    int raw_wait_status = 0;
    bool has_wait_status = false;
};

ProcessInfo *find_process(shell::ShellState &state, pid_t pid);
const ProcessInfo *find_process(const shell::ShellState &state, pid_t pid);

void add_process(shell::ShellState &state, pid_t pid, pid_t pgid,
                 const std::string &command, bool background);
void mark_process_finished(shell::ShellState &state, pid_t pid,
                           int raw_wait_status);

void cleanup_finished_processes(shell::ShellState &state);
int shell_status_from_wait_status(int raw_wait_status);
int wait_for_processes(shell::ShellState &state,
                       const std::vector<pid_t> &pids);

} // namespace process
