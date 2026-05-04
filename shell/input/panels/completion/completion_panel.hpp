#pragma once

#include "../panel.hpp"

namespace shell::prompt {
struct InputRenderState;
}

namespace shell::input::session_state {
struct CompletionSelectionState;
}

namespace shell::input::panels::completion {

Block build_block(const shell::prompt::InputRenderState &render_state,
                  const session_state::CompletionSelectionState &selection);

} // namespace shell::input::panels::completion
