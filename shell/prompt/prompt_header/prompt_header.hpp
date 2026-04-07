#pragma once

#include <string>

namespace shell {
struct ShellState;
}

namespace shell::prompt::prompt_header {

struct HeaderInfo {
    std::string rendered = "";
    size_t display_width = 0;
};

HeaderInfo build_header(const shell::ShellState &state);

} // namespace shell::prompt::prompt_header
