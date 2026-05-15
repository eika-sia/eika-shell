#include "command_plan.hpp"

namespace shell::script {

CommandPlan plan_command(ShellState &state, parser::ast::SimpleCommand &cmd,
                         builtins::ExecContext context) {
    CommandPlan plan{};
    plan.cmd = &cmd;

    if (!cmd.invocation || cmd.invocation->words.empty()) {
        if (cmd.explicit_with) {
            plan.kind = CommandKind::Reject;
            plan.reject_message = "with: expected command after assignments";
            return plan;
        }

        plan.kind = CommandKind::Noop;
        return plan;
    }

    if (!cmd.explicit_with && !cmd.assignments.empty()) {
        plan.kind = CommandKind::Reject;
        plan.reject_message = "temporary assignments require with";
        return plan;
    }

    const std::string &name = cmd.invocation->words[0].text;

    if (auto fn = state.functions.find(name); fn != state.functions.end()) {
        if (cmd.explicit_with) {
            plan.kind = CommandKind::Reject;
            plan.reject_message =
                "with: functions cannot run with child environment";
            return plan;
        }

        plan.kind = CommandKind::Function;
        plan.function = &fn->second;
        return plan;
    }

    plan.builtin = builtins::plan_builtin(cmd, context);
    if (cmd.explicit_with &&
        plan.builtin.decision != builtins::BuiltinDecision::External) {
        plan.kind = CommandKind::Reject;
        plan.reject_message =
            "with: builtins cannot run with child environment";
        return plan;
    }

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
