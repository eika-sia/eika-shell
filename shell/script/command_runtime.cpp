#include "command_runtime.hpp"

#include "../../builtins/alias/alias.hpp"
#include "../../builtins/builtins.hpp"
#include "../../features/expansion/expansion.hpp"
#include "../exec/exec.hpp"

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

bool validate_pipeline_builtins(
    const parser::ast::Pipeline &pipe,
    std::vector<diagnostics::Diagnostic> &diagnostics) {
    if (pipe.commands.size() <= 1) {
        return true;
    }

    for (const parser::ast::SimpleCommand &cmd : pipe.commands) {
        const builtins::BuiltinPlan plan =
            builtins::plan_builtin(cmd, builtins::ExecContext::PipelineStage);
        if (plan.decision != builtins::BuiltinDecision::Reject) {
            continue;
        }

        diagnostics::add_error(diagnostics, cmd.span,
                               cmd.invocation->words[0].text +
                                   ": cannot run in this context");
        return false;
    }

    return true;
}

int dispatch_pipeline(ShellState &state, parser::ast::Pipeline &pipe,
                      bool background,
                      std::vector<diagnostics::Diagnostic> &diagnostics) {
    if (!features::expand_pipeline(state, pipe, diagnostics)) {
        return 1;
    }

    if (!validate_pipeline_builtins(pipe, diagnostics)) {
        return 1;
    }

    if (pipe.commands.size() == 1) {
        const parser::ast::SimpleCommand &cmd = pipe.commands[0];

        if (!background && !cmd.invocation) {
            // Commandless simple commands still run redirections as shell
            // no-ops, so `A=1 >out` persists `A` and creates/truncates `out`.
            return exec::run_parent_assignments_with_redirections(state, cmd);
        }

        const builtins::ExecContext ctx =
            background ? builtins::ExecContext::BackgroundStandalone
                       : builtins::ExecContext::ForegroundStandalone;
        const builtins::BuiltinPlan plan = builtins::plan_builtin(cmd, ctx);

        if (plan.decision == builtins::BuiltinDecision::RunInParent) {
            const int status = exec::run_parent_builtin_with_redirections(
                state, cmd, plan, diagnostics);
            return status < 0 ? 1 : status;
        }

        if (plan.decision == builtins::BuiltinDecision::Reject) {
            diagnostics::add_error(diagnostics, pipe.span,
                                   cmd.invocation->words[0].text +
                                       ": cannot run in this context");
            return 1;
        }
    }

    return exec::run_pipeline(state, pipe, background, diagnostics);
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
