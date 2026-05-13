#include "command_runtime.hpp"

#include "../../../builtins/alias/alias.hpp"
#include "../../../builtins/builtins.hpp"
#include "../../../features/expansion/expansion.hpp"
#include "../../exec/exec.hpp"
#include "./command_plan.hpp"
#include "./pipeline_runtime.hpp"

namespace shell::script {
namespace {

bool should_execute_pipeline(parser::ast::ChainCondition condition,
                             int previous_status) {
    switch (condition) {
    case parser::ast::ChainCondition::Always:
        return true;
    case parser::ast::ChainCondition::IfPreviousSucceeded:
        return previous_status == 0;
    case parser::ast::ChainCondition::IfPreviousFailed:
        return previous_status != 0;
    }

    return true;
}

int dispatch_pipeline(ShellState &state, parser::ast::Pipeline &pipe,
                      bool background,
                      std::vector<diagnostics::Diagnostic> &diagnostics) {
    if (!features::expand_pipeline(state, pipe, diagnostics)) {
        return 1;
    }

    std::vector<CommandPlan> plans;
    plans.reserve(pipe.commands.size());

    for (parser::ast::SimpleCommand &cmd : pipe.commands) {
        const builtins::ExecContext context =
            pipe.commands.size() > 1 ? builtins::ExecContext::PipelineStage
            : background ? builtins::ExecContext::BackgroundStandalone
                         : builtins::ExecContext::ForegroundStandalone;

        CommandPlan plan = plan_command(state, cmd, context);
        if (plan.kind == CommandKind::Reject) {
            diagnostics::add_error(diagnostics, cmd.span, plan.reject_message);
            return 1;
        }

        plans.push_back(plan);
    }

    if (plans.size() == 1 && !background) {
        CommandPlan &plan = plans[0];

        if (plan.kind == CommandKind::Noop) {
            return exec::run_parent_assignments_with_redirections(state,
                                                                  *plan.cmd);
        }

        if (plan.kind == CommandKind::Function) {
            return run_function(state, *plan.cmd, *plan.function, diagnostics);
        }

        if (plan.kind == CommandKind::Builtin &&
            plan.builtin.decision == builtins::BuiltinDecision::RunInParent) {
            return exec::run_parent_builtin_with_redirections(
                state, *plan.cmd, plan.builtin, diagnostics);
        }
    }

    return run_planned_pipeline(state, plans, background, diagnostics);
}

void run_command_chain(ShellState &state, parser::ast::CommandChain &chain,
                       std::vector<diagnostics::Diagnostic> &diagnostics) {
    // A background conditional chain needs its own controller process to
    // keep evaluating later `&&` / `||` stages after the parent returns.
    // That is not implemented as of yet.
    if (chain.background && chain.pipelines.size() > 1) {
        diagnostics::add_error(
            diagnostics, chain.span,
            "background conditional execution not implemented yet");
        state.last_status = 1;
        return;
    }

    bool executed_any_pipeline = false;
    int chain_status = state.last_status;

    for (parser::ast::ConditionalPipeline pipeline : chain.pipelines) {
        if (!should_execute_pipeline(pipeline.condition, chain_status)) {
            continue;
        }

        chain_status = dispatch_pipeline(state, pipeline.pipeline,
                                         chain.background, diagnostics);
        state.last_status = chain_status;
        executed_any_pipeline = true;
        if (!state.running) {
            return;
        }
    }
    if (executed_any_pipeline) {
        state.last_status = chain_status;
    }
}
} // namespace

void command_runtime(ShellState &state, const parser::ast::CommandChain &chain,
                     std::vector<diagnostics::Diagnostic> &diagnostics) {
    std::vector<parser::ast::CommandChain> expanded_chains;
    if (!builtins::expand_aliases(state, chain, expanded_chains, diagnostics)) {
        state.last_status = 1;
        return;
    }

    if (expanded_chains.empty()) {
        state.last_status = 0;
        return;
    }

    for (parser::ast::CommandChain &expanded_chain : expanded_chains) {
        run_command_chain(state, expanded_chain, diagnostics);
        if (!state.running) {
            return;
        }
    }
}

} // namespace shell::script
