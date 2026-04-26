#pragma once

#include <optional>
#include <string>

namespace shell {
struct ShellState;
}

namespace shell::prompt::prompt_segments {

std::optional<std::string> render_token(const shell::ShellState &state,
                                        const std::string &token);

} // namespace shell::prompt::prompt_segments
