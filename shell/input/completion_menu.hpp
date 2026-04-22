#pragma once

#include <string>

#include "./below_prompt_panel.hpp"

namespace shell::prompt {
struct InputRenderState;
}

namespace shell::input::session_state {
struct CompletionSelectionState;
}

namespace shell::input::completion_menu {

using RenderState = below_prompt_panel::RenderState;

bool is_visible(const RenderState &state);
std::string
build_render_frame(const shell::prompt::InputRenderState &render_state,
                   const session_state::CompletionSelectionState &selection,
                   RenderState &menu_state);
std::string
build_dismiss_frame(const shell::prompt::InputRenderState &render_state,
                    RenderState &menu_state);
std::string build_clear_prompt_and_menu_frame(
    const shell::prompt::InputRenderState &render_state,
    RenderState &menu_state);

} // namespace shell::input::completion_menu
