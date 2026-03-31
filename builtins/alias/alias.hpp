#pragma once

#include "../../shell/shell.hpp"

int run_alias_list(ShellState &state, const Command &cmd);
int run_alias_set(ShellState &state, const Command &cmd);
bool expand_aliases(const ShellState &state, Command &cmd);
