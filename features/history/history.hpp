#pragma once

#include <string>

#include "../../shell/shell.hpp"

namespace features {

bool expand_history(shell::ShellState &state, std::string &line);
void save_command_line(shell::ShellState &state, const std::string &line);

} // namespace features
