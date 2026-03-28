#pragma once

#include <string>
#include <sys/types.h>

struct ProcessInfo {
    pid_t pid = -1;
    pid_t pgid = -1;
    std::string command;
    bool running = false;
    bool background = false;
};
