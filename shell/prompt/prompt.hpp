#pragma once

#include <string>

namespace shell {
struct ShellState;
}

namespace shell::prompt {

std::string build_prompt(const shell::ShellState &state);
void redraw_input_line(const shell::ShellState &state, const std::string &line,
                       size_t cursor, bool full_prompt);

} // namespace shell::prompt
