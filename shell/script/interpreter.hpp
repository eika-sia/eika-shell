#pragma once

#include "../../parser/ast.hpp"
#include "./command/command_runtime.hpp"
#include "./command/pipeline_runtime.hpp"

#include <vector>

namespace shell::script {

InterpretResult
interpret_script(ShellState &state, const parser::ast::Program &program,
                 std::vector<diagnostics::Diagnostic> &diagnostics);
InterpretResult
interpret_block(ShellState &state, const parser::ast::Block &program,
                std::vector<diagnostics::Diagnostic> &diagnostics);

} // namespace shell::script
