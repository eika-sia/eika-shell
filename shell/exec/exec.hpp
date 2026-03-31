#pragma once

#include "../../builtins/builtins.hpp"
#include "../shell.hpp"

void launch_pipeline(ShellState &state, const Pipeline &pipe);

struct SavedStdio {
    int stdin_fd = -1;
    int stdout_fd = -1;
};

int run_parent_builtin_with_redirections(ShellState &state, const Command &cmd,
                                         BuiltinKind kind);
