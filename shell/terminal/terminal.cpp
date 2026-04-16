#include "terminal.hpp"

#include <cerrno>
#include <cstdio>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

namespace shell::terminal {
namespace {

constexpr const char *bracketed_paste_enable = "\033[?2004h";
constexpr const char *bracketed_paste_disable = "\033[?2004l";

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

void set_bracketed_paste_mode(bool enabled) {
    write_all_stdout(enabled ? bracketed_paste_enable
                             : bracketed_paste_disable);
}

bool can_control_foreground_process_group() {
    if (!isatty(STDIN_FILENO)) {
        return false;
    }

    while (true) {
        if (tcgetpgrp(STDIN_FILENO) != -1) {
            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == ENOTTY) {
            return false;
        }

        perror("tcgetpgrp");
        return false;
    }
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
    set_bracketed_paste_mode(true);
}

void shutdown_terminal(const ShellState &state) {
    if (!state.interactive) {
        return;
    }

    set_bracketed_paste_mode(false);
}

void give_terminal_to(pid_t pgid) {
    if (!isatty(STDIN_FILENO)) {
        return;
    }

    set_bracketed_paste_mode(false);
    if (!can_control_foreground_process_group()) {
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

    if (can_control_foreground_process_group() &&
        tcsetpgrp(STDIN_FILENO, state.shell_pgid) == -1) {
        perror("tcsetpgrp");
    }

    if (tcsetattr(STDIN_FILENO, 0, &state.shell_term_settings) == -1) {
        perror("tcsetattr");
    }

    set_bracketed_paste_mode(true);
}

void write_stdout(const std::string &text) { write_all_stdout(text); }

void write_stdout_line(const std::string &text) {
    std::string line = text;
    line += '\n';
    write_all_stdout(line);
}

} // namespace shell::terminal
