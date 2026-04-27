#pragma once

#include <string>

namespace shell {
struct ShellState;
}

namespace shell::prompt {

struct PromptLayout {
    std::string header_rendered;
    std::string input_prefix_rendered;
    size_t prompt_prefix_display_width = 0;
    std::string input_right_rendered;
    size_t input_right_display_width = 0;
};

struct InputRenderState {
    PromptLayout layout;
    size_t input_display_width = 0;
    size_t cursor_display_width = 0;
    size_t terminal_columns = 80;
    bool needs_full_redraw = false;
};

struct InputFrame {
    std::string frame;
    InputRenderState next_render_state;
};

std::string build_prompt(const shell::ShellState &state,
                         InputRenderState &render_state);
InputFrame
build_redraw_input_frame(const InputRenderState &current_render_state,
                         const shell::ShellState &state,
                         const std::string &line, size_t cursor,
                         bool full_prompt);
void redraw_input_line(InputRenderState &render_state,
                       const shell::ShellState &state, const std::string &line,
                       size_t cursor, bool full_prompt);

} // namespace shell::prompt
