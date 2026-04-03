#pragma once

#include "../../parser/parser.hpp"
#include "../../shell/shell.hpp"

namespace builtins {

int run_alias_list(shell::ShellState &state, const parser::Command &cmd);
int run_alias_manage(shell::ShellState &state, const parser::Command &cmd);
bool expand_aliases(const shell::ShellState &state, parser::CommandList &list);

} // namespace builtins
