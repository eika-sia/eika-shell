#pragma once

#include "../../parser/ast.hpp"
#include "../diagnostics/diagnostics.hpp"

#include <vector>

namespace shell {
struct ShellState;
}

namespace shell::script {

void command_runtime(ShellState &state, const parser::ast::CommandChain &chain,
                     std::vector<diagnostics::Diagnostic> &diagnostics);

}
