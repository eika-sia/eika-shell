#pragma once

#include <cstddef>
#include <string>

namespace shell::prompt {
struct InputRenderState;
}

namespace shell::input::below_prompt_panel {

struct Block {
    std::string text;
    size_t rows = 0;
};

struct RenderState {
    size_t rows = 0;
};

bool is_visible(const RenderState &state);
std::string build_render_frame(const shell::prompt::InputRenderState &render_state,
                               const Block &block, RenderState &panel_state);
std::string build_dismiss_frame(const shell::prompt::InputRenderState &render_state,
                                RenderState &panel_state);
std::string
build_clear_prompt_and_panel_frame(const shell::prompt::InputRenderState &render_state,
                                   RenderState &panel_state);

} // namespace shell::input::below_prompt_panel
