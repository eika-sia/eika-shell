#include "signals.hpp"

#include <cstdio>
#include <signal.h>
#include <unistd.h>

namespace shell::signals {

volatile sig_atomic_t g_foreground_pgid = -1;

namespace {

void handle_sigint(int signo) {
    (void)signo;

    if (g_foreground_pgid > 0) {
        kill(-g_foreground_pgid, SIGINT);
    } else {
        write(STDOUT_FILENO, "\n", 1);
    }
}

} // namespace

void install_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        perror("sigaction");
    }
}

} // namespace shell::signals
