#pragma once

#include <string>
#include <sys/types.h>
#include <termios.h>
#include <unordered_map>
#include <vector>

#include "../process/process.hpp"

namespace shell {

struct ShellVariable {
    std::string value;
    bool exported = false;
};

struct ShellState {
    std::vector<std::string> history;
    std::vector<process::ProcessInfo> processes;

    pid_t shell_pgid = -1;
    pid_t foreground_pgid = -1;

    termios shell_term_settings{};
    bool interactive = false;
    bool running = true;
    int last_status = 0;

    std::unordered_map<std::string, std::string> alias;
    std::unordered_map<std::string, ShellVariable> variables;
};

void init_shell(ShellState &state);
void execute_command_line(ShellState &state, std::string line);
std::string trim(const std::string &source);

} // namespace shell
