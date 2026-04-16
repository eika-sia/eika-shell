#pragma once

#include <string>

#include "../shell.hpp"

namespace shell::terminal {

void init_terminal(ShellState &state);
void shutdown_terminal(const ShellState &state);
void give_terminal_to(pid_t pgid);
void reclaim_terminal(const ShellState &state);
void write_stdout(const std::string &text);
void write_stdout_line(const std::string &text);

} // namespace shell::terminal
