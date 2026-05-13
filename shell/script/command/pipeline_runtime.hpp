#pragma once

#include "../../../parser/ast.hpp"
#include "./command_plan.hpp"

namespace shell {
struct ShellState;
}

namespace shell::script {

enum class FlowKind {
    None,
    Return,
    Break,
    Continue,
};

struct InterpretResult {
    int status = 0;
    FlowKind flow = FlowKind::None;
};

int run_function(ShellState &state, const parser::ast::SimpleCommand &cmd,
                 const ShellFunction &function,
                 std::vector<diagnostics::Diagnostic> &diagnostics);

int run_planned_pipeline(ShellState &state,
                         const std::vector<CommandPlan> &plans, bool background,
                         std::vector<diagnostics::Diagnostic> &diagnostics);

} // namespace shell::script
