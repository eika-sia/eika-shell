#pragma once

#include <string>

namespace shell {
struct ShellState;
}

namespace features {

std::string expand_environment_variables(const shell::ShellState &state,
                                         const std::string &line);

} // namespace features
