#pragma once

#include "../prompt.hpp"

namespace shell {
struct ShellState;
}

namespace shell::prompt::prompt_template {

PromptLayout build_layout(const shell::ShellState &state);

} // namespace shell::prompt::prompt_template
