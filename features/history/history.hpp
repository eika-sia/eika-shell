#pragma once

#include <string>

#include "../../shell/shell.hpp"

namespace features {

bool expand_history(shell::ShellState &state, std::string &line);
void save_command_line(shell::ShellState &state, const std::string &line);
void load_history_file(shell::ShellState &state, const std::string &path);
void save_history_file(const shell::ShellState &state, const std::string &path);
void load_shell_history(shell::ShellState &state);
void save_shell_history(const shell::ShellState &state);

} // namespace features
