#pragma once

#include "../../builtins/builtins.hpp"
#include "../shell.hpp"

namespace shell::exec {

int run_parent_assignments_with_redirections(
    ShellState &state, const parser::ast::SimpleCommand &cmd);
int run_parent_builtin_with_redirections(
    ShellState &state, const parser::ast::SimpleCommand &cmd,
    const builtins::BuiltinPlan &plan,
    std::vector<diagnostics::Diagnostic> &diagnostics);

bool apply_redirections(const parser::ast::SimpleCommand &cmd);

void apply_child_pipes(size_t index, size_t count,
                       const std::vector<std::array<int, 2>> &fds);

void close_pipe_fds(std::vector<std::array<int, 2>> &fds);

[[noreturn]] void exec_external(ShellState &state,
                                const parser::ast::SimpleCommand &cmd);

} // namespace shell::exec
