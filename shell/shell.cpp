#include "shell.hpp"

#include <chrono>
#include <iostream>
#include <istream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "../builtins/builtins.hpp"
#include "../builtins/env/env.hpp"
#include "../features/history/history.hpp"
#include "../parser/ast.hpp"
#include "../parser/parser.hpp"
#include "./config/config_paths.hpp"
#include "./diagnostics/diagnostics.hpp"
#include "./script/interpreter.hpp"
#include "./signals/signals.hpp"
#include "./terminal/terminal.hpp"

namespace shell {
namespace {

using ExecutionClock = std::chrono::steady_clock;

void update_last_exec_seconds(ShellState &state,
                              ExecutionClock::time_point started_at) {
    state.last_exec_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                  ExecutionClock::now() - started_at)
                                  .count();
}

std::string to_string(std::istream &in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
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

    const std::string config_path = config::startup_config_path(state);
    if (!config_path.empty()) {
        builtins::source_file(state, config_path, true);
    }
}

void execute_script(ShellState &state, std::string line,
                    ExecuteOptions options) {
    state.last_status = 0;

    const ExecutionClock::time_point started_at = ExecutionClock::now();

    process::cleanup_finished_processes(state);

    if (!features::expand_history(state, line)) {
        state.last_status = 1;
        update_last_exec_seconds(state, started_at);
        return;
    }

    if (options.save_history) {
        features::save_command_line(state, line);
    }

    parser::ast::ParseResult parse_result = parser::parse_program(line);
    if (!parse_result.ok) {
        diagnostics::print_diagnostics(line, parse_result.diagnostics);
        state.last_status = 2;
        update_last_exec_seconds(state, started_at);
        return;
    }

    std::vector<diagnostics::Diagnostic> diagnostics;

    script::interpret_script(state, parse_result.program, diagnostics);
    if (!diagnostics.empty()) {
        diagnostics::print_diagnostics(line, diagnostics);
    }

    update_last_exec_seconds(state, started_at);
}

int execute_stream(ShellState &state, std::istream &stream,
                   ExecuteOptions options) {
    std::string line = to_string(stream);

    execute_script(state, line, options);

    return state.last_status;
}

} // namespace shell
