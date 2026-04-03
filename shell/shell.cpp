#include "shell.hpp"

#include <iostream>
#include <string>
#include <unistd.h>

#include "../builtins/builtins.hpp"
#include "../builtins/env/env.hpp"
#include "../builtins/env/envexec/envexec.hpp"
#include "../features/expansion/expansion.hpp"
#include "../features/history/history.hpp"
#include "../parser/parser.hpp"
#include "./exec/exec.hpp"
#include "signals/signals.hpp"
#include "terminal/terminal.hpp"

namespace shell {
namespace {

bool should_execute_pipeline(parser::RunCondition condition,
                             int previous_status) {
    switch (condition) {
    case parser::RunCondition::Always:
        return true;
    case parser::RunCondition::IfPreviousSucceeded:
        return previous_status == 0;
    case parser::RunCondition::IfPreviousFailed:
        return previous_status != 0;
    }

    return true;
}

int dispatch_pipeline(ShellState &state, parser::Pipeline &pipe) {
    for (parser::Command &cmd : pipe.commands) {
        if (!features::expand_command(state, cmd)) {
            return 1;
        }

        cmd.background = pipe.background;
    }

    if (pipe.commands.size() == 1) {
        const parser::Command &cmd = pipe.commands[0];

        if (cmd.args.empty() && !cmd.assignments.empty()) {
            builtins::env::apply_persistent_assignments(state, cmd.assignments);
            return 0;
        }

        const builtins::ExecContext ctx =
            pipe.background ? builtins::ExecContext::BackgroundStandalone
                            : builtins::ExecContext::ForegroundStandalone;
        const builtins::BuiltinPlan plan = builtins::plan_builtin(cmd, ctx);

        if (plan.decision == builtins::BuiltinDecision::RunInParent) {
            const int status =
                exec::run_parent_builtin_with_redirections(state, cmd, plan);
            return status < 0 ? 1 : status;
        }

        if (plan.decision == builtins::BuiltinDecision::Reject) {
            std::cerr << cmd.args[0] << ": cannot run in this context\n";
            return 1;
        }
    }

    return exec::run_pipeline(state, pipe);
}

} // namespace

void init_shell(ShellState &state) {
    state.shell_pgid = getpgrp();
    terminal::init_terminal(state);
    signals::install_signal_handlers();
    builtins::env::import_process_environment(state);
}

std::string trim(const std::string &source) {
    std::string s(source);
    s.erase(0, s.find_first_not_of(" \n\r\t"));
    s.erase(s.find_last_not_of(" \n\r\t") + 1);
    return s;
}

void execute_command_line(ShellState &state, std::string line) {
    line = trim(line);
    if (line.empty())
        return;

    process::cleanup_finished_processes(state);

    if (!features::expand_history(state, line)) {
        state.last_status = 1;
        return;
    }

    parser::CommandList command_line = parser::parse_command_line(line);
    if (!command_line.valid) {
        state.last_status = 2;
        return;
    }

    for (parser::ConditionalPipeline and_or : command_line.and_or_pipelines) {
        if (and_or.background && and_or.pipelines.size() > 1) {
            std::cerr
                << "background conditional execution not implemented yet\n";
            state.last_status = 1;
            return;
        }

        int chain_status = state.last_status;
        bool executed_any_pipeline = false;

        for (parser::Pipeline pipe : and_or.pipelines) {
            pipe.background = and_or.background;
            if (!should_execute_pipeline(pipe.run_condition, chain_status)) {
                continue;
            }

            chain_status = dispatch_pipeline(state, pipe);
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

    features::save_command_line(state, line);
}

} // namespace shell
