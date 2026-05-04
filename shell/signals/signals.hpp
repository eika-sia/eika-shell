#pragma once

#include <signal.h>

namespace shell::signals {

void install_signal_handlers();
extern volatile sig_atomic_t g_foreground_pgid;
extern volatile sig_atomic_t g_input_interrupted;
extern volatile sig_atomic_t g_resize_pending;

} // namespace shell::signals
