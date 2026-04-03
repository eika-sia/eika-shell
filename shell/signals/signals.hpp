#pragma once

#include <signal.h>

namespace shell::signals {

void install_signal_handlers();
extern volatile sig_atomic_t g_foreground_pgid;

} // namespace shell::signals
