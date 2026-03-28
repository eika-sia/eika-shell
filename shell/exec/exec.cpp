#include "exec.hpp"

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

void launch_external(ShellState &state, const Command &cmd) {
    if (cmd.args.size() == 0) {
        std::cerr << "how did we get here?\n";
        return;
    }

    // argv bi trebao bit char* cosnt*
    std::vector<char *> argv = build_argv(cmd.args);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        setpgid(0, 0);
        if (!apply_redirections(cmd)) {
            _exit(1);
        }
        execvp(argv[0], argv.data());
        perror("execvp");
        _exit(1);
    }

    if (setpgid(pid, pid) == -1) {
        perror("setpgid");
    }

    add_process(state, pid, cmd);

    if (cmd.background)
        return;

    state.foreground_pgid = pid;
    g_foreground_pgid = pid;

    give_terminal_to(g_foreground_pgid);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
    }
    mark_process_finished(state, pid);

    reclaim_terminal(state);

    state.foreground_pgid = -1;
    g_foreground_pgid = -1;
}

void launch_pipeline(ShellState &state, const Pipeline pipe) {
    if (pipe.commands.size() == 1) {
        launch_external(state, pipe.commands[0]);
    } else {
        std::cerr << "sorry not implemented\n";
    }
}
