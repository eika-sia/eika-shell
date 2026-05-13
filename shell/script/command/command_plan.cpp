#include "command_plan.hpp"

namespace shell::script {

CommandPlan plan_command(ShellState &state, parser::ast::SimpleCommand &cmd,
                         builtins::ExecContext context) {
    CommandPlan plan{};
    plan.cmd = &cmd;

    if (!cmd.invocation || cmd.invocation->words.empty()) {
        plan.kind = CommandKind::Noop;
        return plan;
    }

    const std::string &name = cmd.invocation->words[0].text;

    if (auto fn = state.functions.find(name); fn != state.functions.end()) {
        plan.kind = CommandKind::Function;
        plan.function = &fn->second;
        return plan;
    }

    plan.builtin = builtins::plan_builtin(cmd, context);
    if (plan.builtin.decision == builtins::BuiltinDecision::Reject) {
        plan.kind = CommandKind::Reject;
        plan.reject_message = name + ": cannot run in this context";
        return plan;
    }

    if (plan.builtin.decision != builtins::BuiltinDecision::External) {
        plan.kind = CommandKind::Builtin;
        return plan;
    }

    plan.kind = CommandKind::External;
    return plan;
}

} // namespace shell::script
