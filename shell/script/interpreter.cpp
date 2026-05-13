#include "interpreter.hpp"

#include <string>
#include <variant>
#include <vector>

#include "../shell.hpp"

namespace shell::script {
namespace {

void declare_function(ShellState &state, parser::ast::FunctionDecl &function,
                      std::vector<diagnostics::Diagnostic> &) {
    ShellFunction shell_function{};

    shell_function.body = std::move(function.body);

    if (!shell_function.body.statements.empty()) {
        const auto base = shell_function.body.statements.front()->span.start;

        for (auto &statement : shell_function.body.statements) {
            statement->span.start -= base;
            statement->span.end -= base;
        }
    }

    for (auto &word : function.params)
        shell_function.params.push_back(word.text);

    state.functions[function.name.text] = std::move(shell_function);
}

InterpretResult interpret_statement_list(
    ShellState &state,
    const std::vector<parser::ast::StatementPtr> &statmeent_list,
    std::vector<diagnostics::Diagnostic> &diagnostics) {
    for (const auto &statement : statmeent_list) {
        if (!statement) {
            continue;
        }

        if (parser::ast::CommandChain *chain =
                std::get_if<parser::ast::CommandChain>(&statement->node)) {
            command_runtime(state, *chain, diagnostics);
        } else if (parser::ast::FunctionDecl *func =
                       std::get_if<parser::ast::FunctionDecl>(
                           &statement->node)) {
            declare_function(state, *func, diagnostics);
        } else {
            diagnostics::add_error(diagnostics, statement->span,
                                   "This statement type not yet implemented");

            return {1, FlowKind::None};
        }
    }
    return {state.last_status, FlowKind::None};
}

} // namespace

InterpretResult
interpret_script(ShellState &state, const parser::ast::Program &program,
                 std::vector<diagnostics::Diagnostic> &diagnostics) {
    return interpret_statement_list(state, program.statements, diagnostics);
}

InterpretResult
interpret_block(ShellState &state, const parser::ast::Block &block,
                std::vector<diagnostics::Diagnostic> &diagnostics) {
    return interpret_statement_list(state, block.statements, diagnostics);
}

} // namespace shell::script
