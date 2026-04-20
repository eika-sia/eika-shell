#include "signals.hpp"

#include <cstdio>
#include <signal.h>

namespace shell::signals {

volatile sig_atomic_t g_foreground_pgid = -1;
volatile sig_atomic_t g_input_interrupted = 0;
volatile sig_atomic_t g_resize_pending = 0;

namespace {

void handle_sigint(int signo) {
    (void)signo;

    if (g_foreground_pgid > 0) {
        kill(-g_foreground_pgid, SIGINT);
    } else {
        g_input_interrupted = 1;
    }
}

void handle_sigwinch(int signo) {
    (void)signo;
    g_resize_pending = 1;
}

} // namespace

void install_signal_handlers() {
    struct sigaction sigint_action{};
    sigint_action.sa_handler = handle_sigint;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;

    if (sigaction(SIGINT, &sigint_action, nullptr) == -1) {
        perror("sigaction");
    }

    struct sigaction sigwinch_action{};
    sigwinch_action.sa_handler = handle_sigwinch;
    sigemptyset(&sigwinch_action.sa_mask);
    sigwinch_action.sa_flags = 0;

    if (sigaction(SIGWINCH, &sigwinch_action, nullptr) == -1) {
        perror("sigaction");
    }
}

} // namespace shell::signals
