#pragma once

#include "../shell.hpp"

void init_terminal(ShellState &state);
void give_terminal_to(pid_t pgid);
void reclaim_terminal(const ShellState &state);
