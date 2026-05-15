#include "pipeline_runtime.hpp"

#include <array>
#include <csignal>
#include <iostream>
#include <unistd.h>
#include <vector>

#include "../../../builtins/env/env.hpp"
#include "../../../process/process.hpp"
#include "../../exec/exec.hpp"
#include "../../signals/signals.hpp"
#include "../../terminal/terminal.hpp"

namespace shell::script {

InterpretResult
interpret_block(ShellState &state, const parser::ast::Block &program,
                std::vector<diagnostics::Diagnostic> &diagnostics);

namespace {
int cleanup_failed_pipeline_start(ShellState &state,
                                  std::vector<std::array<int, 2>> &fds,
                                  const std::vector<pid_t> &pids,
                                  pid_t pipeline_pgid) {
    exec::close_pipe_fds(fds);

    if (pipeline_pgid > 0) {
        kill(-pipeline_pgid, SIGTERM);
    }

    process::wait_for_processes(state, pids);
    return 1;
}

std::string command_name(const parser::ast::SimpleCommand &cmd) {
    if (!cmd.invocation || cmd.invocation->words.empty()) {
        return "";
    }

    std::string name;
    for (const parser::ast::Word &word : cmd.invocation->words) {
        if (!name.empty()) {
            name += ' ';
        }
        name += word.text;
    }

    return name;
}

void print_child_diagnostic(std::vector<diagnostics::Diagnostic> diagnostics,
                            size_t diagnostic_count) {
    for (size_t j = diagnostic_count; j < diagnostics.size(); ++j) {
        const diagnostics::Diagnostic &diagnostic = diagnostics[j];
        std::string label =
            diagnostic.severity == diagnostics::DiagnosticSeverity::Warning
                ? "warning"
                : "error";
        std::cerr << label << ": " << diagnostic.message << std::endl;
    }
    std::cerr.flush();
    std::cout.flush();
}

} // namespace

int run_function(ShellState &state, const parser::ast::SimpleCommand &cmd,
                 const ShellFunction &function,
                 std::vector<diagnostics::Diagnostic> &diagnostics) {
    size_t named_param_count =
        std::min(function.params.size(), cmd.invocation->words.size() - 1);
    std::vector<parser::ast::Assignment> assigns;
    for (size_t i = 0; i < named_param_count; ++i) {
        assigns.push_back(parser::ast::Assignment{
            parser::ast::Identifier{function.params[i], ""},
            parser::ast::Word{cmd.invocation->words[i + 1].text}});
    }
    state.scopes.push_back({});
    for (const parser::ast::Assignment &assign : assigns) {
        state.scopes.back().variables[assign.name.text] =
            ShellVariable{assign.value.text, false};
    }

    InterpretResult body_res =
        interpret_block(state, function.body, diagnostics);

    state.scopes.pop_back();
    return body_res.status;
}

int run_planned_pipeline(ShellState &state,
                         const std::vector<CommandPlan> &plans, bool background,
                         std::vector<diagnostics::Diagnostic> &diagnostics) {
    if (plans.empty()) {
        diagnostics::add_error(diagnostics, parser::SourceSpan{0, 0},
                               "internal error: empty pipeline");
        return 1;
    }

    const size_t count = plans.size();
    std::vector<std::array<int, 2>> fds{};

    if (count > 1) {
        fds.assign(count - 1, std::array<int, 2>{-1, -1});
        for (size_t i = 0; i < count - 1; ++i) {
            if (::pipe(fds[i].data()) == -1) {
                perror("pipe");
                exec::close_pipe_fds(fds);
                return 1;
            }
        }
    }

    std::vector<pid_t> pids;
    pid_t pipeline_pgid = -1;

    for (size_t i = 0; i < count; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return cleanup_failed_pipeline_start(state, fds, pids,
                                                 pipeline_pgid);
        }

        if (pid == 0) {
            if (pipeline_pgid == -1)
                setpgid(0, 0);
            else
                setpgid(0, pipeline_pgid);

            exec::apply_child_pipes(i, count, fds);

            if (!exec::apply_redirections(*plans[i].cmd)) {
                _exit(1);
            }

            size_t diagnostic_count = diagnostics.size();
            int status;

            switch (plans[i].kind) {
            case CommandKind::Noop:
                status = builtins::env::run_update_value(state, *plans[i].cmd,
                                                         diagnostics);
                std::cout.flush();
                std::cerr.flush();
                _exit(status);
            case CommandKind::Function:
                status = run_function(state, *plans[i].cmd, *plans[i].function,
                                      diagnostics);
                print_child_diagnostic(diagnostics, diagnostic_count);
                _exit(status);
            case CommandKind::Builtin:
                status = builtins::run_builtin(
                    state, *plans[i].cmd, plans[i].builtin.kind, diagnostics);
                print_child_diagnostic(diagnostics, diagnostic_count);
                _exit(status);
            case CommandKind::External:
                exec::exec_external(state, *plans[i].cmd);
                _exit(127);
            case CommandKind::Reject:
                std::cerr << "something went wrong" << std::endl;
                _exit(1);
            }
        }

        if (pipeline_pgid == -1) {
            pipeline_pgid = pid;
        }

        if (setpgid(pid, pipeline_pgid) == -1) {
            perror("setpgid");
        }

        pids.push_back(pid);
        process::add_process(state, pid, pipeline_pgid,
                             command_name(*plans[i].cmd), background);
    }

    exec::close_pipe_fds(fds);

    if (background)
        return 0;

    state.foreground_pgid = pipeline_pgid;
    signals::g_foreground_pgid = pipeline_pgid;

    terminal::give_terminal_to(pipeline_pgid);

    const int status = process::wait_for_processes(state, pids);

    terminal::reclaim_terminal(state);

    state.foreground_pgid = -1;
    signals::g_foreground_pgid = -1;
    return status;
}

} // namespace shell::script
