#include "exec.hpp"

#include <array>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "../../builtins/builtins.hpp"
#include "../../builtins/env/envexec/envexec.hpp"
#include "../signals/signals.hpp"
#include "../terminal/terminal.hpp"

namespace shell::exec {
namespace {

struct SavedStdio {
    int stdin_fd = -1;
    int stdout_fd = -1;
};

struct EnvironmentBlock {
    std::vector<std::string> storage;
    std::vector<char *> envp;
    std::string path_override;
    bool has_path_override = false;
};

std::vector<char *> build_argv(const std::vector<parser::ast::Word> &args) {
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);

    for (const parser::ast::Word &s : args) {
        argv.push_back(const_cast<char *>(s.text.c_str()));
    }

    argv.push_back(nullptr);
    return argv;
}

EnvironmentBlock
build_envp(const ShellState &state,
           const std::vector<parser::ast::Assignment> &assignments) {
    std::unordered_map<std::string, std::string> env_map;
    env_map.reserve(state.variables.size() + assignments.size());

    for (const auto &[name, variable] : state.variables) {
        if (variable.exported) {
            env_map[name] = variable.value;
        }
    }

    EnvironmentBlock block{};
    for (const parser::ast::Assignment &assignment : assignments) {
        env_map[assignment.name.text] = assignment.value.text;
        if (assignment.name.text == "PATH") {
            block.path_override = assignment.value.text;
            block.has_path_override = true;
        }
    }

    block.storage.reserve(env_map.size());
    block.envp.reserve(env_map.size() + 1);

    for (const auto &[name, value] : env_map) {
        block.storage.push_back(name + "=" + value);
    }

    for (std::string &entry : block.storage) {
        block.envp.push_back(const_cast<char *>(entry.c_str()));
    }
    block.envp.push_back(nullptr);

    return block;
}

struct redirects {
    std::string input_file{};
    std::string output_file{};
    bool append_output = false;
};

bool apply_redirections(const parser::ast::SimpleCommand &cmd) {
    redirects redirects{};

    for (auto &red : cmd.redirects) {
        switch (red.kind) {
        case parser::ast::RedirectKind::Input:
            redirects.input_file = red.target.text;
            break;
        case parser::ast::RedirectKind::Output:
            redirects.output_file = red.target.text;
            break;
        case parser::ast::RedirectKind::AppendOutput:
            redirects.output_file = red.target.text;
            redirects.append_output = true;
            break;
        }
    }

    if (!redirects.input_file.empty()) {
        int fd = open(redirects.input_file.c_str(), O_RDONLY);
        if (fd == -1) {
            perror("open input");
            return false;
        }

        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2 input");
            close(fd);
            return false;
        }

        close(fd);
    }

    if (!redirects.output_file.empty()) {
        int fd;

        if (redirects.append_output) {
            fd = open(redirects.output_file.c_str(),
                      O_WRONLY | O_CREAT | O_APPEND, 0644);
        } else {
            fd = open(redirects.output_file.c_str(),
                      O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }

        if (fd == -1) {
            perror("open output");
            return false;
        }

        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("dup2 output");
            close(fd);
            return false;
        }

        close(fd);
    }

    return true;
}

void close_pipe_fds(std::vector<std::array<int, 2>> &fds) {
    for (std::array<int, 2> &fdpair : fds) {
        if (fdpair[0] != -1) {
            close(fdpair[0]);
            fdpair[0] = -1;
        }
        if (fdpair[1] != -1) {
            close(fdpair[1]);
            fdpair[1] = -1;
        }
    }
}

int cleanup_failed_pipeline_start(ShellState &state,
                                  std::vector<std::array<int, 2>> &fds,
                                  const std::vector<pid_t> &pids,
                                  pid_t pipeline_pgid) {
    close_pipe_fds(fds);

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

} // namespace

int run_pipeline(ShellState &state, const parser::ast::Pipeline &pipe,
                 bool background,
                 std::vector<diagnostics::Diagnostic> &diagnostics) {
    if (pipe.commands.empty()) {
        diagnostics::add_error(diagnostics, pipe.span,
                               "internal error: empty pipeline");
        return 1;
    }

    const size_t n = pipe.commands.size();
    std::vector<std::array<int, 2>> fds;

    if (n > 1) {
        fds.assign(n - 1, std::array<int, 2>{-1, -1});
        for (size_t i = 0; i < n - 1; ++i) {
            if (::pipe(fds[i].data()) == -1) {
                perror("pipe");
                close_pipe_fds(fds);
                return 1;
            }
        }
    }

    std::vector<pid_t> pids;
    pid_t pipeline_pgid = -1;

    for (size_t i = 0; i < n; ++i) {
        const parser::ast::SimpleCommand &cmd = pipe.commands[i];

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return cleanup_failed_pipeline_start(state, fds, pids,
                                                 pipeline_pgid);
        }

        if (pid == 0) {
            // prva komanda je group leader
            if (pipeline_pgid == -1) {
                setpgid(0, 0);
            } else {
                setpgid(0, pipeline_pgid);
            }

            if (i > 0) {
                if (dup2(fds[i - 1][0], STDIN_FILENO) == -1) {
                    perror("dup2 stdin");
                    _exit(1);
                }
            }

            if (i + 1 < n) {
                if (dup2(fds[i][1], STDOUT_FILENO) == -1) {
                    perror("dup2 stdout");
                    _exit(1);
                }
            }

            for (auto &fdpair : fds) {
                close(fdpair[0]);
                close(fdpair[1]);
            }

            if (!apply_redirections(cmd)) {
                _exit(1);
            }

            if (!cmd.invocation) {
                builtins::env::apply_temporary_assignments(state,
                                                           cmd.assignments);
                std::cout.flush();
                std::cerr.flush();
                _exit(0);
            }

            // builtin pipelining
            builtins::ExecContext ctx =
                (n > 1) ? builtins::ExecContext::PipelineStage
                        : (background
                               ? builtins::ExecContext::BackgroundStandalone
                               : builtins::ExecContext::ForegroundStandalone);
            builtins::BuiltinPlan plan = builtins::plan_builtin(cmd, ctx);

            if (plan.decision == builtins::BuiltinDecision::RunInChild) {
                if (!cmd.assignments.empty()) {
                    builtins::env::apply_temporary_assignments(state,
                                                               cmd.assignments);
                }
                const size_t diagnostic_count = diagnostics.size();
                int status =
                    builtins::run_builtin(state, cmd, plan.kind, diagnostics);
                for (size_t j = diagnostic_count; j < diagnostics.size(); ++j) {
                    const diagnostics::Diagnostic &diagnostic = diagnostics[j];
                    const char *label =
                        diagnostic.severity ==
                                diagnostics::DiagnosticSeverity::Warning
                            ? "warning"
                            : "error";
                    std::cerr << label << ": " << diagnostic.message << '\n';
                }
                std::cout.flush();
                std::cerr.flush();
                _exit(status < 0 ? 1 : status);
            }

            if (plan.decision == builtins::BuiltinDecision::Reject) {
                std::cerr << command_name(cmd)
                          << ": cannot run in this context\n";
                _exit(2);
            }

            if (plan.decision == builtins::BuiltinDecision::RunInParent) {
                std::cerr
                    << "internal error: parent-only builtin reached child\n";
                std::cerr.flush();
                _exit(2);
            }

            // regular child exec
            EnvironmentBlock env = build_envp(state, cmd.assignments);
            if (env.has_path_override &&
                setenv("PATH", env.path_override.c_str(), 1) == -1) {
                perror("setenv PATH");
                _exit(1);
            }

            std::vector<char *> argv = build_argv(cmd.invocation->words);
            execvpe(argv[0], argv.data(), env.envp.data());
            perror(argv[0]);
            _exit(1);
        }

        if (pipeline_pgid == -1) {
            pipeline_pgid = pid;
        }

        if (setpgid(pid, pipeline_pgid) == -1) {
            perror("setpgid");
        }

        pids.push_back(pid);
        process::add_process(state, pid, pipeline_pgid, command_name(cmd),
                             background);
    }

    close_pipe_fds(fds);

    if (background) {
        return 0;
    }

    state.foreground_pgid = pipeline_pgid;
    signals::g_foreground_pgid = pipeline_pgid;

    terminal::give_terminal_to(pipeline_pgid);

    const int status = process::wait_for_processes(state, pids);

    terminal::reclaim_terminal(state);

    state.foreground_pgid = -1;
    signals::g_foreground_pgid = -1;
    return status;
}

namespace {

bool save_stdio(SavedStdio &saved) {
    saved = SavedStdio{};

    saved.stdin_fd = dup(STDIN_FILENO);
    if (saved.stdin_fd == -1) {
        perror("dup stdin");
        return false;
    }

    saved.stdout_fd = dup(STDOUT_FILENO);
    if (saved.stdout_fd == -1) {
        perror("dup stdout");
        close(saved.stdin_fd);
        saved.stdin_fd = -1;
        return false;
    }

    return true;
}

void restore_stdio(const SavedStdio &saved) {
    if (saved.stdin_fd != -1) {
        if (dup2(saved.stdin_fd, STDIN_FILENO) == -1) {
            perror("dup2 restore stdin");
        }
        close(saved.stdin_fd);
    }

    if (saved.stdout_fd != -1) {
        if (dup2(saved.stdout_fd, STDOUT_FILENO) == -1) {
            perror("dup2 restore stdout");
        }
        close(saved.stdout_fd);
    }
}

} // namespace

int run_parent_assignments_with_redirections(
    ShellState &state, const parser::ast::SimpleCommand &cmd) {
    SavedStdio saved{};
    if (!save_stdio(saved)) {
        return 1;
    }

    std::cout.flush();

    if (!apply_redirections(cmd)) {
        restore_stdio(saved);
        return 1;
    }

    builtins::env::apply_persistent_assignments(state, cmd.assignments);

    std::cout.flush();

    restore_stdio(saved);
    return 0;
}

int run_parent_builtin_with_redirections(
    ShellState &state, const parser::ast::SimpleCommand &cmd,
    const builtins::BuiltinPlan &plan,
    std::vector<diagnostics::Diagnostic> &diagnostics) {
    SavedStdio saved{};
    if (!save_stdio(saved)) {
        return 1;
    }

    std::cout.flush();

    if (!apply_redirections(cmd)) {
        restore_stdio(saved);
        return 1;
    }

    builtins::env::AssignmentSnapshot snapshot;
    if (!cmd.assignments.empty()) {
        snapshot =
            builtins::env::apply_temporary_assignments(state, cmd.assignments);
    }

    int status = builtins::run_builtin(state, cmd, plan.kind, diagnostics);

    if (!cmd.assignments.empty()) {
        builtins::env::restore_temporary_assignments(state, snapshot);
    }

    std::cout.flush();

    restore_stdio(saved);

    return status;
}

} // namespace shell::exec
