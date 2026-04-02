#pragma once

#include <string>
#include <vector>

namespace shell::input {

struct InputResult {
    std::string line;
    bool eof = false;
    bool interrupted = false;
};

InputResult read_command_line(std::vector<std::string> &history,
                              bool interactive);

} // namespace shell::input
