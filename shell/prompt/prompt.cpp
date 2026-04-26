#include "prompt.hpp"

#include "../../features/highlighting/highlighting.hpp"
#include "../shell.hpp"
#include "../terminal/terminal.hpp"
#include "./prompt_utils/prompt_template.hpp"
#include "./render_utils.hpp"

#include <utility>

namespace shell::prompt {
namespace {

void reset_input_render_state(InputRenderState &render_state) {
    render_state = {};
}

void update_input_render_state(InputRenderState &render_state,
                               PromptLayout layout, size_t input_display_width,
                               size_t cursor_display_width,
                               size_t terminal_columns) {
    render_state.layout = std::move(layout);
    render_state.input_display_width = input_display_width;
    render_state.cursor_display_width = cursor_display_width;
    render_state.terminal_columns = terminal_columns;
    render_state.needs_full_redraw = false;
}

std::string render_prompt_layout(const PromptLayout &layout) {
    if (layout.header_rendered.empty()) {
        return layout.input_prefix_rendered;
    }

    return layout.header_rendered + "\n" + layout.input_prefix_rendered;
}

} // namespace

std::string build_prompt(const shell::ShellState &state,
                         InputRenderState &render_state) {
    const PromptLayout layout = prompt_template::build_layout(state);
    reset_input_render_state(render_state);
    update_input_render_state(render_state, layout, 0, 0,
                              render_utils::get_terminal_columns());
    render_state.needs_full_redraw = true;
    return render_prompt_layout(layout);
}

InputFrame
build_redraw_input_frame(const InputRenderState &current_render_state,
                         const shell::ShellState &state,
                         const std::string &line, size_t cursor,
                         bool full_prompt) {
    const size_t columns = render_utils::get_terminal_columns();
    const bool terminal_resized =
        columns != current_render_state.terminal_columns;
    const bool redraw_full_prompt = full_prompt || terminal_resized ||
                                    current_render_state.needs_full_redraw;
    const PromptLayout layout = redraw_full_prompt
                                    ? prompt_template::build_layout(state)
                                    : current_render_state.layout;
    const std::string prefix = redraw_full_prompt
                                   ? render_prompt_layout(layout)
                                   : layout.input_prefix_rendered;
    const std::string rendered =
        features::highlighting::render_highlighted_line(state, line);
    const size_t clamped_cursor = cursor > line.size() ? line.size() : cursor;
    const size_t line_display_width = render_utils::measure_display_width(line);
    const size_t cursor_display_width =
        render_utils::measure_display_width(line.substr(0, clamped_cursor));
    const render_utils::CursorGeometry end =
        line.empty() ? render_utils::CursorGeometry{}
                     : render_utils::compute_render_end_geometry(
                           layout.prompt_prefix_display_width,
                           line_display_width, columns);
    const render_utils::CursorGeometry cursor_geometry =
        render_utils::compute_cursor_geometry(
            layout.prompt_prefix_display_width, cursor_display_width, columns,
            cursor_display_width == line_display_width);

    InputFrame result;
    result.next_render_state = current_render_state;

    if (redraw_full_prompt) {
        if (terminal_resized) {
            result.frame = render_utils::clear_previous_prompt_block(
                current_render_state, current_render_state.terminal_columns,
                columns);
        } else {
            result.frame =
                render_utils::clear_rendered_prompt_block(current_render_state);
        }
    } else {
        result.frame = render_utils::clear_previous_input_block(
            current_render_state, columns);
    }
    result.frame += prefix;
    result.frame += rendered;

    if (end.row > cursor_geometry.row) {
        result.frame +=
            "\033[" + std::to_string(end.row - cursor_geometry.row) + "A";
    }
    result.frame += "\r";
    if (cursor_geometry.column > 0) {
        result.frame += "\033[" + std::to_string(cursor_geometry.column) + "C";
    }

    update_input_render_state(result.next_render_state, layout,
                              line_display_width, cursor_display_width,
                              columns);
    return result;
}

void redraw_input_line(InputRenderState &render_state,
                       const shell::ShellState &state, const std::string &line,
                       size_t cursor, bool full_prompt) {
    InputFrame frame = build_redraw_input_frame(render_state, state, line,
                                                cursor, full_prompt);
    shell::terminal::write_stdout(frame.frame);
    render_state = std::move(frame.next_render_state);
}

} // namespace shell::prompt
