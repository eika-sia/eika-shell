#pragma once

#include <string>

namespace shell {
struct ShellState;
}

namespace shell::prompt {

std::string build_prompt(const shell::ShellState &state);
std::string build_prompt_header(const shell::ShellState &state);
std::string build_prompt_prefix();

} // namespace shell::prompt
