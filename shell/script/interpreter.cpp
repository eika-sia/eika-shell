#include "interpreter.hpp"

#include "command_runtime.hpp"
#include <variant>

namespace shell::script {

void interpret_script(ShellState &state, const parser::ast::Program &program,
                      std::vector<diagnostics::Diagnostic> &diagnostics) {
    for (const auto &statement : program.statements) {
        if (!statement) {
            continue;
        }

        if (parser::ast::CommandChain *chain =
                std::get_if<parser::ast::CommandChain>(&statement->node)) {
            command_runtime(state, *chain, diagnostics);
        } else {
            diagnostics::add_error(diagnostics, statement->span,
                                   "This statement type not yet implemented");
        }
    }
}

} // namespace shell::script
