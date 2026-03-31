#pragma once

#include <string>
#include <sys/types.h>
#include <termios.h>
#include <unordered_map>
#include <vector>

#include "../parser/parser.hpp"

struct ProcessInfo {
    pid_t pid = -1;
    pid_t pgid = -1;
    std::string command;
    bool running = false;
    bool background = false;
};

struct ShellState {
    std::vector<std::string> history;
    std::vector<ProcessInfo> processes;

    pid_t shell_pgid = -1;
    pid_t foreground_pgid = -1;

    termios shell_term_settings{};
    bool running = true;

    std::unordered_map<std::string, std::string> alias;
};

void init_shell(ShellState &state);
void cleanup_finished_processes(ShellState &state);

void add_process(ShellState &state, pid_t pid, const Command &cmd);
void mark_process_finished(ShellState &state, pid_t pid);

void execute_command_line(ShellState &state, std::string line);
std::string trim(const std::string &source);
