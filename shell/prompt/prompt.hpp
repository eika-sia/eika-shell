#pragma once

#include <string>

namespace shell {
struct ShellState;
}

namespace shell::prompt {

struct InputRenderState {
    std::string header_rendered;
    size_t header_display_width = 0;
    size_t prompt_prefix_display_width = 0;
    size_t input_display_width = 0;
    size_t cursor_display_width = 0;
    size_t terminal_columns = 80;
};

std::string build_prompt(const shell::ShellState &state,
                         InputRenderState &render_state);
void redraw_input_line(InputRenderState &render_state,
                       const shell::ShellState &state, const std::string &line,
                       size_t cursor, bool full_prompt);
void finalize_interrupted_input_line(InputRenderState &render_state);

} // namespace shell::prompt
