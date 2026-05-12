#pragma once

#include "../../parser/ast.hpp"
#include "command_runtime.hpp"

#include <vector>

namespace shell::script {

void interpret_script(ShellState &state, const parser::ast::Program &program,
                      std::vector<diagnostics::Diagnostic> &diagnostics);

}
