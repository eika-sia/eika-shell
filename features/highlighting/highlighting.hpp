#pragma once

#include <string>

namespace shell {
struct ShellState;
}

namespace features::highlighting {
std::string render_highlighted_line(const shell::ShellState &state,
                                    const std::string &line);
}
