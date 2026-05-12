#pragma once

#include "../../parser/ast.hpp"
#include "../../shell/diagnostics/diagnostics.hpp"
#include "../../shell/shell.hpp"

#include <vector>

namespace builtins {

int run_alias_list(shell::ShellState &state,
                   const parser::ast::SimpleCommand &cmd,
                   std::vector<shell::diagnostics::Diagnostic> &diagnostics);
int run_alias_manage(shell::ShellState &state,
                     const parser::ast::SimpleCommand &cmd,
                     std::vector<shell::diagnostics::Diagnostic> &diagnostics);
bool expand_aliases(const shell::ShellState &state,
                    const parser::ast::CommandChain &chain,
                    std::vector<parser::ast::CommandChain> &chains,
                    std::vector<shell::diagnostics::Diagnostic> &diagnostics);

} // namespace builtins
