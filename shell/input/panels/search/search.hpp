#pragma once

#include "../panel.hpp"

namespace shell::prompt {
struct InputRenderState;
}

namespace shell::input::session_state {
struct SearchState;
}

namespace shell::input::panels::search {

Block build_block(const shell::prompt::InputRenderState &render_state,
                  const session_state::SearchState &selection);

} // namespace shell::input::panels::search
