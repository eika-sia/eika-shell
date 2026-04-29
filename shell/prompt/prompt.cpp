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

bool should_render_input_right_prompt(const PromptLayout &layout,
                                      size_t input_display_width,
                                      size_t columns) {
    if (layout.input_right_rendered.empty()) {
        return false;
    }

    if (render_utils::compute_rendered_rows(layout.prompt_prefix_display_width,
                                            input_display_width,
                                            columns) != 1) {
        return false;
    }

    return layout.prompt_prefix_display_width + input_display_width + 1 +
               layout.input_right_display_width <=
           columns;
}

void append_move_to_column(std::string &frame, size_t column) {
    frame += "\r";
    if (column > 0) {
        frame += "\033[" + std::to_string(column) + "C";
    }
}

void append_input_right_prompt(std::string &frame, const PromptLayout &layout,
                               size_t columns) {
    append_move_to_column(frame, columns - layout.input_right_display_width);
    frame += layout.input_right_rendered;
}

} // namespace

std::string build_prompt(const shell::ShellState &state,
                         InputRenderState &render_state) {
    const PromptLayout layout = prompt_template::build_layout(state);
    const size_t columns = render_utils::get_terminal_columns();
    reset_input_render_state(render_state);
    update_input_render_state(render_state, layout, 0, 0, columns);
    render_state.needs_full_redraw = true;

    std::string rendered = render_prompt_layout(layout);
    if (should_render_input_right_prompt(layout, 0, columns)) {
        append_input_right_prompt(rendered, layout, columns);
        append_move_to_column(rendered, layout.prompt_prefix_display_width);
    }

    return rendered;
}

namespace {

InputFrame build_input_frame(const shell::ShellState &state,
                             const std::string &line, size_t cursor,
                             const PromptLayout &layout,
                             const std::string &prefix, size_t columns) {
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
    const bool right_prompt_visible =
        should_render_input_right_prompt(layout, line_display_width, columns);

    InputFrame result;
    result.frame = prefix;
    result.frame += rendered;

    if (right_prompt_visible) {
        append_input_right_prompt(result.frame, layout, columns);
    } else if (end.row > cursor_geometry.row) {
        result.frame +=
            "\033[" + std::to_string(end.row - cursor_geometry.row) + "A";
    }
    append_move_to_column(result.frame, cursor_geometry.column);

    update_input_render_state(result.next_render_state, layout,
                              line_display_width, cursor_display_width,
                              columns);
    return result;
}

} // namespace

InputFrame build_fresh_input_frame(const shell::ShellState &state,
                                   const std::string &line, size_t cursor) {
    const size_t columns = render_utils::get_terminal_columns();
    const PromptLayout layout = prompt_template::build_layout(state);
    return build_input_frame(state, line, cursor, layout,
                             render_prompt_layout(layout), columns);
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
    InputFrame result =
        build_input_frame(state, line, cursor, layout, prefix, columns);

    if (redraw_full_prompt) {
        if (terminal_resized) {
            result.frame = render_utils::clear_previous_prompt_block(
                               current_render_state,
                               current_render_state.terminal_columns, columns) +
                           result.frame;
        } else {
            result.frame = render_utils::clear_rendered_prompt_block(
                               current_render_state) +
                           result.frame;
        }
    } else {
        result.frame = render_utils::clear_previous_input_block(
                           current_render_state, columns) +
                       result.frame;
    }
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
