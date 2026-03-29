#pragma once

#include "../../shell/shell.hpp"

bool handle_alias_builtin(ShellState &state, const Command &cmd);
bool expand_aliases(const ShellState &state, Command &cmd);
