#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

#include "../parser/parser.hpp"

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
};

ProcessInfo *find_process(shell::ShellState &state, pid_t pid);
const ProcessInfo *find_process(const shell::ShellState &state, pid_t pid);

void add_process(shell::ShellState &state, pid_t pid, pid_t pgid,
                 const parser::Command &cmd);
void mark_process_finished(shell::ShellState &state, pid_t pid);

bool reap_process(shell::ShellState &state, pid_t pid, int options);
void cleanup_finished_processes(shell::ShellState &state);
void wait_for_processes(shell::ShellState &state,
                        const std::vector<pid_t> &pids);

} // namespace process
