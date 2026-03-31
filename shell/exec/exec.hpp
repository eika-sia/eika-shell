#pragma once

#include "../../builtins/builtins.hpp"
#include "../shell.hpp"

namespace shell::exec {

void launch_pipeline(ShellState &state, const parser::Pipeline &pipe);
int run_parent_builtin_with_redirections(ShellState &state,
                                         const parser::Command &cmd,
                                         const builtins::BuiltinPlan &plan);

} // namespace shell::exec
