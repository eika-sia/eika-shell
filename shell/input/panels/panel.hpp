#pragma once

#include <cstddef>
#include <string>

namespace shell::prompt {
struct InputRenderState;
}

namespace shell::input::panels {

struct Block {
    std::string text;
    size_t rows = 0;
};

struct RenderState {
    size_t rows = 0;
};

bool is_visible(const RenderState &state);
std::string truncate_plain_text(const std::string &text, size_t max_width);
size_t max_panel_rows(const shell::prompt::InputRenderState &render_state,
                      size_t columns);
size_t compute_window_start_row(size_t total_rows, size_t visible_rows,
                                size_t selected_row);
std::string build_items_footer(size_t first_item_index, size_t last_item_index,
                               size_t total_items, size_t columns);
void append_line(Block &block, const std::string &line);
void update_block_rows(Block &block, size_t columns);
std::string
build_render_frame(const shell::prompt::InputRenderState &render_state,
                   const Block &block, RenderState &panel_state);
std::string
build_dismiss_frame(const shell::prompt::InputRenderState &render_state,
                    RenderState &panel_state);
std::string build_clear_prompt_and_panel_frame(
    const shell::prompt::InputRenderState &render_state,
    RenderState &panel_state);

} // namespace shell::input::panels
