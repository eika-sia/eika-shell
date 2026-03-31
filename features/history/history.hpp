#pragma once

#include <string>

#include "../../shell/shell.hpp"

namespace features {

bool expand_history(shell::ShellState &state, std::string &line);

} // namespace features
