#include "terminal.hpp"

#include <cstdio>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

namespace shell::terminal {

void init_terminal(ShellState &state) {
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
    if (tcsetpgrp(STDIN_FILENO, pgid) == -1) {
        perror("tcsetpgrp");
    }
}

void reclaim_terminal(const ShellState &state) {
    if (tcsetpgrp(STDIN_FILENO, state.shell_pgid) == -1) {
        perror("tcsetpgrp");
    }

    if (tcsetattr(STDIN_FILENO, 0, &state.shell_term_settings) == -1) {
        perror("tcsetattr");
    }
}

} // namespace shell::terminal
