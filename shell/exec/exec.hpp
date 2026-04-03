#pragma once

#include "../../builtins/builtins.hpp"
#include "../shell.hpp"

namespace shell::exec {

int run_pipeline(ShellState &state, const parser::Pipeline &pipe,
                 bool background);
int run_parent_assignments_with_redirections(
    ShellState &state, const parser::Command &cmd);
int run_parent_builtin_with_redirections(ShellState &state,
                                         const parser::Command &cmd,
                                         const builtins::BuiltinPlan &plan);

} // namespace shell::exec
