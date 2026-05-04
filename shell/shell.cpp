#include "shell.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <unistd.h>

#include "../builtins/alias/alias.hpp"
#include "../builtins/builtins.hpp"
#include "../builtins/env/env.hpp"
#include "../features/expansion/expansion.hpp"
#include "../features/history/history.hpp"
#include "../parser/parser.hpp"
#include "./exec/exec.hpp"
#include "signals/signals.hpp"
#include "terminal/terminal.hpp"

namespace shell {
namespace {

using ExecutionClock = std::chrono::steady_clock;

void update_last_exec_seconds(ShellState &state,
                              ExecutionClock::time_point started_at) {
    state.last_exec_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                  ExecutionClock::now() - started_at)
                                  .count();
}

bool is_assignment_only_command(const parser::Command &cmd) {
    return cmd.args.empty() && !cmd.assignments.empty();
}

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

int dispatch_pipeline(ShellState &state, parser::Pipeline &pipe,
                      bool background) {
    for (parser::Command &cmd : pipe.commands) {
        if (!features::expand_command(state, cmd)) {
            return 1;
        }
    }

    if (pipe.commands.size() == 1) {
        const parser::Command &cmd = pipe.commands[0];

        if (!background && is_assignment_only_command(cmd)) {
            // Assignment-only commands still run redirections as a shell no-op,
            // so `A=1 >out` both persists `A` and creates/truncates `out`.
            return exec::run_parent_assignments_with_redirections(state, cmd);
        }

        const builtins::ExecContext ctx =
            background ? builtins::ExecContext::BackgroundStandalone
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

    return exec::run_pipeline(state, pipe, background);
}

} // namespace

void init_shell(ShellState &state) {
    state.shell_pgid = getpgrp();
    terminal::init_terminal(state);
    signals::install_signal_handlers();
    builtins::env::import_process_environment(state);

    if (!state.interactive) {
        return;
    }

    features::load_shell_history(state);

    const shell::ShellVariable *home =
        builtins::env::find_variable(state, "HOME");
    if (home == nullptr || home->value.empty()) {
        return;
    }

    builtins::source_file(state, home->value + "/.eshrc", true);
}

std::string trim(const std::string &source) {
    std::string s(source);
    s.erase(0, s.find_first_not_of(" \n\r\t"));
    s.erase(s.find_last_not_of(" \n\r\t") + 1);
    return s;
}

void execute_command_line(ShellState &state, std::string line,
                          ExecuteOptions options) {
    state.last_status = 0;
    line = trim(line);
    if (line.empty())
        return;

    const ExecutionClock::time_point started_at = ExecutionClock::now();

    process::cleanup_finished_processes(state);

    if (!features::expand_history(state, line)) {
        state.last_status = 1;
        update_last_exec_seconds(state, started_at);
        return;
    }

    parser::CommandList command_line = parser::parse_command_line(line);
    if (!command_line.valid) {
        state.last_status = 2;
        update_last_exec_seconds(state, started_at);
        return;
    }

    if (!builtins::expand_aliases(state, command_line)) {
        state.last_status = 1;
        update_last_exec_seconds(state, started_at);
        return;
    }

    for (parser::ConditionalChain chain : command_line.conditional_chains) {
        // A background conditional chain needs its own controller process to
        // keep evaluating later `&&` / `||` stages after the parent returns.
        // That is not implemented as of yet.
        if (chain.background && chain.pipelines.size() > 1) {
            std::cerr
                << "background conditional execution not implemented yet\n";
            state.last_status = 1;
            update_last_exec_seconds(state, started_at);
            return;
        }

        int chain_status = state.last_status;
        bool executed_any_pipeline = false;

        for (parser::Pipeline pipe : chain.pipelines) {
            if (!should_execute_pipeline(pipe.run_condition, chain_status)) {
                continue;
            }

            chain_status = dispatch_pipeline(state, pipe, chain.background);
            state.last_status = chain_status;
            executed_any_pipeline = true;
            if (!state.running) {
                update_last_exec_seconds(state, started_at);
                return;
            }
        }

        if (executed_any_pipeline) {
            state.last_status = chain_status;
        }
    }

    if (options.save_history) {
        features::save_command_line(state, line);
    }

    update_last_exec_seconds(state, started_at);
}

int execute_stream(ShellState &state, std::istream &stream,
                   ExecuteOptions options) {
    std::string line;
    while (state.running && std::getline(stream, line)) {
        execute_command_line(state, line, options);
        process::cleanup_finished_processes(state);
    }

    return state.last_status;
}

} // namespace shell
