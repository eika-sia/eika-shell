#pragma once

#include <vector>

#include "../../parser/ast.hpp"
#include "../../shell/diagnostics/diagnostics.hpp"

namespace shell {
struct ShellState;
}

namespace features {

bool expand_pipeline(shell::ShellState &state, parser::ast::Pipeline &pipeline,
                     std::vector<shell::diagnostics::Diagnostic> &diagnostics);

} // namespace features
