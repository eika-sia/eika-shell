#include "terminal.hpp"

#include <cerrno>
#include <cstdio>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

namespace shell::terminal {
namespace {

bool write_all_stdout(const std::string &text) {
    size_t offset = 0;

    while (offset < text.size()) {
        const ssize_t written =
            write(STDOUT_FILENO, text.data() + offset, text.size() - offset);

        if (written > 0) {
            offset += static_cast<size_t>(written);
            continue;
        }

        if (written < 0 && errno == EINTR) {
            continue;
        }

        perror("write");
        return false;
    }

    return true;
}

} // namespace

void init_terminal(ShellState &state) {
    state.interactive = isatty(STDIN_FILENO);
    if (!state.interactive) {
        return;
    }

    if (tcgetattr(STDIN_FILENO, &state.shell_term_settings) == -1) {
        perror("tcgetattr");
        return;
    }

    state.shell_term_settings.c_lflag &= ~ECHOCTL;

    if (tcsetattr(STDIN_FILENO, 0, &state.shell_term_settings) == -1) {
        perror("tcsetattr");
    }

    signal(SIGTTOU, SIG_IGN);
}

void give_terminal_to(pid_t pgid) {
    if (!isatty(STDIN_FILENO)) {
        return;
    }

    if (tcsetpgrp(STDIN_FILENO, pgid) == -1) {
        perror("tcsetpgrp");
    }
}

void reclaim_terminal(const ShellState &state) {
    if (!state.interactive) {
        return;
    }

    if (tcsetpgrp(STDIN_FILENO, state.shell_pgid) == -1) {
        perror("tcsetpgrp");
    }

    if (tcsetattr(STDIN_FILENO, 0, &state.shell_term_settings) == -1) {
        perror("tcsetattr");
    }
}

void write_stdout(const std::string &text) { write_all_stdout(text); }

void write_stdout_line(const std::string &text) {
    std::string line = text;
    line += '\n';
    write_all_stdout(line);
}

} // namespace shell::terminal
