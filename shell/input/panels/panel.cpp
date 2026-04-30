#include "panel.hpp"

#include "../../prompt/prompt.hpp"
#include "../../prompt/render_utils.hpp"

#include <algorithm>

namespace shell::input::panels {
namespace {

const std::string dim = "\033[2m";
const std::string reset = "\033[0m";

size_t visible_panel_row_cap(size_t terminal_rows) {
    if (terminal_rows <= 4) {
        return 1;
    }

    return std::max<size_t>(4, terminal_rows / 3);
}

prompt::render_utils::CursorGeometry
input_cursor_geometry(const prompt::InputRenderState &render_state,
                      size_t columns) {
    const bool cursor_at_line_end =
        render_state.cursor_display_width == render_state.input_display_width;

    return prompt::render_utils::compute_cursor_geometry(
        render_state.layout.prompt_prefix_display_width,
        render_state.cursor_display_width, columns, cursor_at_line_end);
}

prompt::render_utils::CursorGeometry
input_end_geometry(const prompt::InputRenderState &render_state,
                   size_t columns) {
    return prompt::render_utils::compute_cursor_geometry(
        render_state.layout.prompt_prefix_display_width,
        render_state.input_display_width, columns, true);
}

std::string restore_input_cursor(const prompt::InputRenderState &render_state,
                                 size_t columns, size_t rows_below_input_end) {
    const prompt::render_utils::CursorGeometry cursor =
        input_cursor_geometry(render_state, columns);
    const prompt::render_utils::CursorGeometry end =
        input_end_geometry(render_state, columns);

    std::string frame = "\r";
    const size_t current_row = end.row + rows_below_input_end;
    if (current_row > cursor.row) {
        frame += "\033[" + std::to_string(current_row - cursor.row) + "A";
    } else if (cursor.row > current_row) {
        frame += "\033[" + std::to_string(cursor.row - current_row) + "B";
    }

    if (cursor.column > 0) {
        frame += "\033[" + std::to_string(cursor.column) + "C";
    }

    return frame;
}

std::string
move_from_cursor_to_input_end(const prompt::InputRenderState &render_state,
                              size_t columns) {
    const size_t cursor_row =
        prompt::render_utils::measure_render_state(render_state, columns)
            .cursor_row;
    const size_t end_row = input_end_geometry(render_state, columns).row;

    std::string frame;
    if (end_row > cursor_row) {
        frame += "\033[" + std::to_string(end_row - cursor_row) + "B";
    } else if (cursor_row > end_row) {
        frame += "\033[" + std::to_string(cursor_row - end_row) + "A";
    }

    frame += "\r";
    return frame;
}

std::string
move_from_cursor_to_panel_start(const prompt::InputRenderState &render_state,
                                size_t columns) {
    std::string frame = move_from_cursor_to_input_end(render_state, columns);
    frame += "\033[1B\r";
    return frame;
}

} // namespace

bool is_visible(const RenderState &state) { return state.rows > 0; }

std::string truncate_plain_text(const std::string &text, size_t max_width) {
    if (max_width == 0 || text.empty()) {
        return "";
    }

    if (prompt::render_utils::measure_display_width(text) <= max_width) {
        return text;
    }

    if (max_width <= 3) {
        return std::string(max_width, '.');
    }

    std::string out;
    size_t width = 0;
    for (size_t i = 0; i < text.size() && width + 3 < max_width;) {
        const size_t start = i;
        ++i;
        while (i < text.size() &&
               (static_cast<unsigned char>(text[i]) & 0xC0U) == 0x80U) {
            ++i;
        }

        out.append(text, start, i - start);
        ++width;
    }

    out += "...";
    return out;
}

size_t max_panel_rows(const prompt::InputRenderState &render_state,
                      size_t columns) {
    const size_t terminal_rows = prompt::render_utils::get_terminal_rows();
    const prompt::render_utils::RenderMetrics metrics =
        prompt::render_utils::measure_render_state(render_state, columns);
    const size_t capped_rows = visible_panel_row_cap(terminal_rows);

    if (terminal_rows <= metrics.total_rows) {
        return 1;
    }

    return std::min(terminal_rows - metrics.total_rows, capped_rows);
}

size_t compute_window_start_row(size_t total_rows, size_t visible_rows,
                                size_t selected_row) {
    if (visible_rows >= total_rows) {
        return 0;
    }

    const size_t half_window = visible_rows / 2;
    const size_t desired_start =
        selected_row > half_window ? selected_row - half_window : 0;
    const size_t max_start = total_rows - visible_rows;
    return std::min(desired_start, max_start);
}

std::string build_items_footer(size_t first_item_index, size_t last_item_index,
                               size_t total_items, size_t columns) {
    const std::string footer = "Items " + std::to_string(first_item_index + 1) +
                               " to " + std::to_string(last_item_index + 1) +
                               " of " + std::to_string(total_items);
    return dim + truncate_plain_text(footer, columns) + reset;
}

void append_line(Block &block, const std::string &line) {
    if (!block.text.empty()) {
        block.text.push_back('\n');
    }

    block.text += line;
}

void update_block_rows(Block &block, size_t columns) {
    block.rows =
        prompt::render_utils::measure_rendered_block_rows(block.text, columns);
}

std::string build_render_frame(const prompt::InputRenderState &render_state,
                               const Block &block, RenderState &panel_state) {
    if (block.text.empty() || block.rows == 0) {
        return build_dismiss_frame(render_state, panel_state);
    }

    const size_t columns = render_state.terminal_columns;
    std::string frame = move_from_cursor_to_panel_start(render_state, columns);

    if (is_visible(panel_state)) {
        frame += prompt::render_utils::clear_render_block(0, panel_state.rows);
    }

    frame += block.text;
    panel_state.rows = block.rows;
    frame += restore_input_cursor(render_state, columns, block.rows);
    return frame;
}

std::string build_dismiss_frame(const prompt::InputRenderState &render_state,
                                RenderState &panel_state) {
    if (!is_visible(panel_state)) {
        return "";
    }

    const size_t columns = render_state.terminal_columns;
    std::string frame = move_from_cursor_to_panel_start(render_state, columns);
    frame += prompt::render_utils::clear_render_block(0, panel_state.rows);
    frame += restore_input_cursor(render_state, columns, 1);
    panel_state = {};
    return frame;
}

std::string
build_clear_prompt_and_panel_frame(const prompt::InputRenderState &render_state,
                                   RenderState &panel_state) {
    const std::string frame = prompt::render_utils::clear_rendered_prompt_block(
        render_state, panel_state.rows);
    panel_state = {};
    return frame;
}

} // namespace shell::input::panels
