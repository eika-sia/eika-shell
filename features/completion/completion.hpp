#pragma once

#include <string>

namespace shell {
struct ShellState;
}

namespace features {

void handle_tab_completion(const shell::ShellState &state, std::string &buf,
                           size_t &cursor);

} // namespace features
