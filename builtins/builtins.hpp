#pragma once

#include "../parser/parser.hpp"
#include "../shell/shell.hpp"

bool handle_builtin(ShellState &state, const Command &cmd);
