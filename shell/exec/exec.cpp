#include "exec.hpp"

#include <array>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <unistd.h>

#include "../../builtins/builtins.hpp"
#include "../signals/signals.hpp"
#include "../terminal/terminal.hpp"

namespace shell::exec {
namespace {

struct SavedStdio {
    int stdin_fd = -1;
    int stdout_fd = -1;
};

std::vector<char *> build_argv(const std::vector<std::string> &args) {
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);

    for (auto &s : args) {
        argv.push_back(const_cast<char *>(s.c_str()));
    }

    argv.push_back(nullptr);
    return argv;
}

bool apply_redirections(const parser::Command &cmd) {
    if (!cmd.input_file.empty()) {
        int fd = open(cmd.input_file.c_str(), O_RDONLY);
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

    if (!cmd.output_file.empty()) {
        int fd;

        if (cmd.append_output) {
            fd = open(cmd.output_file.c_str(), O_WRONLY | O_CREAT | O_APPEND,
                      0644);
        } else {
            fd = open(cmd.output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                      0644);
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

void cleanup_failed_pipeline_launch(ShellState &state,
                                    std::vector<std::array<int, 2>> &fds,
                                    const std::vector<pid_t> &pids,
                                    pid_t pipeline_pgid) {
    close_pipe_fds(fds);

    if (pipeline_pgid > 0) {
        kill(-pipeline_pgid, SIGTERM);
    }

    process::wait_for_processes(state, pids);
}

void launch_pipeline_impl(ShellState &state, const parser::Pipeline &pipe) {
    if (pipe.commands.empty()) {
        std::cerr << "how did we get here?\n";
        return;
    }

    const size_t n = pipe.commands.size();
    std::vector<std::array<int, 2>> fds;

    if (n > 1) {
        fds.assign(n - 1, std::array<int, 2>{-1, -1});
        for (size_t i = 0; i < n - 1; ++i) {
            if (::pipe(fds[i].data()) == -1) {
                perror("pipe");
                close_pipe_fds(fds);
                return;
            }
        }
    }

    std::vector<pid_t> pids;
    pid_t pipeline_pgid = -1;

    for (size_t i = 0; i < n; ++i) {
        const parser::Command &cmd = pipe.commands[i];

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            cleanup_failed_pipeline_launch(state, fds, pids, pipeline_pgid);
            return;
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

            // builtin pipelining
            builtins::ExecContext ctx =
                (n > 1)
                    ? builtins::ExecContext::PipelineStage
                    : (pipe.background
                           ? builtins::ExecContext::BackgroundStandalone
                           : builtins::ExecContext::ForegroundStandalone);
            builtins::BuiltinPlan plan = builtins::plan_builtin(cmd, ctx);

            if (plan.decision == builtins::BuiltinDecision::RunInChild) {
                int status = builtins::run_builtin(state, cmd, plan.kind);
                std::cout.flush();
                std::cerr.flush();
                _exit(status < 0 ? 1 : status);
            }

            if (plan.decision == builtins::BuiltinDecision::Reject) {
                std::cerr << cmd.args[0] << ": cannot run in this context\n";
                std::cerr.flush();
                _exit(2);
            }

            if (plan.decision == builtins::BuiltinDecision::RunInParent) {
                std::cerr
                    << "internal error: parent-only builtin reached child\n";
                std::cerr.flush();
                _exit(2);
            }

            // regular child exec
            std::vector<char *> argv = build_argv(cmd.args);
            execvp(argv[0], argv.data());
            perror("execvp");
            _exit(1);
        }

        if (pipeline_pgid == -1) {
            pipeline_pgid = pid;
        }

        if (setpgid(pid, pipeline_pgid) == -1) {
            perror("setpgid");
        }

        pids.push_back(pid);
        process::add_process(state, pid, pipeline_pgid, cmd);
    }

    close_pipe_fds(fds);

    if (pipe.background) {
        return;
    }

    state.foreground_pgid = pipeline_pgid;
    signals::g_foreground_pgid = pipeline_pgid;

    terminal::give_terminal_to(pipeline_pgid);

    process::wait_for_processes(state, pids);

    terminal::reclaim_terminal(state);

    state.foreground_pgid = -1;
    signals::g_foreground_pgid = -1;
}

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

void launch_pipeline(ShellState &state, const parser::Pipeline &pipe) {
    launch_pipeline_impl(state, pipe);
}

int run_parent_builtin_with_redirections(ShellState &state,
                                         const parser::Command &cmd,
                                         const builtins::BuiltinPlan &plan) {
    SavedStdio saved{};
    if (!save_stdio(saved)) {
        return 1;
    }

    std::cout.flush();
    std::cerr.flush();

    if (!apply_redirections(cmd)) {
        restore_stdio(saved);
        return 1;
    }

    int status = builtins::run_builtin(state, cmd, plan.kind);

    std::cout.flush();
    std::cerr.flush();

    restore_stdio(saved);

    return status;
}

} // namespace shell::exec
