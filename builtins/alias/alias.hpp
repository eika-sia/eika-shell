#pragma once

#include "../../shell/shell.hpp"

namespace builtins {

int run_alias_list(shell::ShellState &state, const parser::Command &cmd);
int run_alias_set(shell::ShellState &state, const parser::Command &cmd);
bool expand_aliases(const shell::ShellState &state, parser::Command &cmd);

} // namespace builtins
