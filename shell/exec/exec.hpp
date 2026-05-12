#pragma once

#include "../../builtins/builtins.hpp"
#include "../shell.hpp"

namespace shell::exec {

int run_pipeline(ShellState &state, const parser::ast::Pipeline &pipe,
                 bool background,
                 std::vector<diagnostics::Diagnostic> &diagnostics);
int run_parent_assignments_with_redirections(
    ShellState &state, const parser::ast::SimpleCommand &cmd);
int run_parent_builtin_with_redirections(
    ShellState &state, const parser::ast::SimpleCommand &cmd,
    const builtins::BuiltinPlan &plan,
    std::vector<diagnostics::Diagnostic> &diagnostics);

} // namespace shell::exec
