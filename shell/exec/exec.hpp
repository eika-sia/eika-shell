#pragma once

#include "../shell.hpp"

void launch_external(ShellState &state, const Command &cmd);
void launch_pipeline(ShellState &state, const Pipeline pipe);
