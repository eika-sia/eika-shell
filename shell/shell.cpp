#include "shell.hpp"

#include <iostream>
#include <string>
#include <unistd.h>

#include "../builtins/builtins.hpp"
#include "../features/expansion/expansion.hpp"
#include "../features/history/history.hpp"
#include "../parser/parser.hpp"
#include "./exec/exec.hpp"
#include "signals/signals.hpp"
#include "terminal/terminal.hpp"

namespace shell {

void init_shell(ShellState &state) {
    state.shell_pgid = getpgrp();
    terminal::init_terminal(state);
    signals::install_signal_handlers();
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

    if (!features::expand_history(state, line))
        return;

    parser::CommandList command_line = parser::parse_command_line(line);
    if (!command_line.valid) {
        return;
    }

    for (parser::Pipeline pipe : command_line.pipelines) {

        for (parser::Command &cmd : pipe.commands) {
            if (!features::expand_command(state, cmd)) {
                return;
            }

            cmd.background = pipe.background;
        }

        if (pipe.commands.size() == 1) {

            const parser::Command &cmd = pipe.commands[0];
            const builtins::ExecContext ctx =
                pipe.background ? builtins::ExecContext::BackgroundStandalone
                                : builtins::ExecContext::ForegroundStandalone;
            const builtins::BuiltinPlan plan = builtins::plan_builtin(cmd, ctx);

            if (plan.decision == builtins::BuiltinDecision::RunInParent) {
                exec::run_parent_builtin_with_redirections(state, cmd, plan);

                if (!state.running) {
                    break;
                }
                continue;
            }

            if (plan.decision == builtins::BuiltinDecision::Reject) {
                std::cerr << cmd.args[0] << ": cannot run in this context\n";
                continue;
            }
        }

        exec::launch_pipeline(state, pipe);
        if (!state.running) {
            break;
        }
    }
}

} // namespace shell
