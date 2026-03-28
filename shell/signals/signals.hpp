#pragma once

#include <signal.h>

void install_signal_handlers();
extern volatile sig_atomic_t g_sigint_received;
extern volatile sig_atomic_t g_foreground_pgid;
