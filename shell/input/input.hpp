#pragma once

#include <string>

namespace shell {
struct ShellState;
}

namespace shell::input {

struct InputResult {
    std::string line;
    bool eof = false;
    bool interrupted = false;
};

InputResult read_command_line(shell::ShellState &state);

} // namespace shell::input
