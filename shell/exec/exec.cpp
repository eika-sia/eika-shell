#include "exec.hpp"

#include <array>
#include <fcntl.h>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#include "../signals/signals.hpp"
#include "../terminal/terminal.hpp"

std::vector<char *> build_argv(const std::vector<std::string> &args) {
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);

    for (auto &s : args) {
        argv.push_back(const_cast<char *>(s.c_str()));
    }

    argv.push_back(nullptr);
    return argv;
}

bool apply_redirections(const Command &cmd) {
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

void launch_pipeline(ShellState &state, const Pipeline &pipe) {
    if (pipe.commands.empty()) {
        std::cerr << "how did we get here?\n";
        return;
    }

    const size_t n = pipe.commands.size();
    std::vector<std::array<int, 2>> fds;

    if (n > 1) {
        fds.resize(n - 1);
        for (size_t i = 0; i < n - 1; ++i) {
            if (::pipe(fds[i].data()) == -1) {
                perror("pipe");
                return;
            }
        }
    }

    std::vector<pid_t> pids;
    pid_t pipeline_pgid = -1;

    for (size_t i = 0; i < n; ++i) {
        const Command &cmd = pipe.commands[i];
        std::vector<char *> argv = build_argv(cmd.args);

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
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
        add_process(state, pid, cmd);
    }

    for (std::array<int, 2> &fdpair : fds) {
        close(fdpair[0]);
        close(fdpair[1]);
    }

    if (pipe.background) {
        return;
    }

    state.foreground_pgid = pipeline_pgid;
    g_foreground_pgid = pipeline_pgid;

    give_terminal_to(pipeline_pgid);

    for (pid_t pid : pids) {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
        }
        mark_process_finished(state, pid);
    }

    reclaim_terminal(state);

    state.foreground_pgid = -1;
    g_foreground_pgid = -1;
}
