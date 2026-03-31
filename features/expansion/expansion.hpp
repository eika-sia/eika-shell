#pragma once

#include "../../parser/parser.hpp"

namespace shell {
struct ShellState;
}

namespace features {

bool expand_command(shell::ShellState &state, parser::Command &cmd);

} // namespace features
