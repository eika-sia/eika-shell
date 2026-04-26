#include "below_prompt_panel.hpp"

#include "../prompt/prompt.hpp"
#include "../prompt/render_utils.hpp"

namespace shell::input::below_prompt_panel {
namespace {

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

} // namespace shell::input::below_prompt_panel
