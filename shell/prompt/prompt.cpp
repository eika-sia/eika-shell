#include "prompt.hpp"

#include "../../features/highlighting/highlighting.hpp"
#include "../shell.hpp"
#include "../terminal/terminal.hpp"
#include "./prompt_header/prompt_header.hpp"
#include "./render_utils.hpp"

#include <unistd.h>
#include <utility>

namespace shell::prompt {
namespace {

const std::string purple = "\033[1;35m";
const std::string reset = "\033[0m";

void reset_input_render_state(InputRenderState &render_state) {
    render_state = {};
}

void update_input_render_state(InputRenderState &render_state,
                               std::string header_rendered,
                               size_t header_display_width,
                               size_t prompt_prefix_display_width,
                               size_t input_display_width,
                               size_t cursor_display_width,
                               size_t terminal_columns) {
    render_state.header_rendered = std::move(header_rendered);
    render_state.header_display_width = header_display_width;
    render_state.prompt_prefix_display_width = prompt_prefix_display_width;
    render_state.input_display_width = input_display_width;
    render_state.cursor_display_width = cursor_display_width;
    render_state.terminal_columns = terminal_columns;
}

render_utils::RenderedFragment build_prompt_prefix() {
    return render_utils::make_rendered_fragment(purple + std::string("╰─❯ ") +
                                                reset);
}

prompt_header::HeaderInfo build_header_info(const shell::ShellState &state) {
    return prompt_header::build_header(state);
}

} // namespace

std::string build_prompt(const shell::ShellState &state,
                         InputRenderState &render_state) {
    const prompt_header::HeaderInfo header = build_header_info(state);
    const render_utils::RenderedFragment prefix = build_prompt_prefix();
    reset_input_render_state(render_state);
    update_input_render_state(render_state, header.rendered,
                              header.display_width, prefix.display_width, 0, 0,
                              render_utils::get_terminal_columns());
    return header.rendered + "\n" + prefix.rendered;
}

void redraw_input_line(InputRenderState &render_state,
                       const shell::ShellState &state, const std::string &line,
                       size_t cursor, bool full_prompt) {
    const size_t columns = render_utils::get_terminal_columns();
    const bool terminal_resized = columns != render_state.terminal_columns;
    const bool redraw_full_prompt = full_prompt || terminal_resized;
    const prompt_header::HeaderInfo header_info =
        redraw_full_prompt
            ? build_header_info(state)
            : prompt_header::HeaderInfo{render_state.header_rendered,
                                        render_state.header_display_width};
    const render_utils::RenderedFragment prefix_info = build_prompt_prefix();
    const std::string header = redraw_full_prompt ? header_info.rendered : "";
    const std::string prefix = redraw_full_prompt
                                   ? header + "\n" + prefix_info.rendered
                                   : prefix_info.rendered;
    const std::string rendered =
        features::highlighting::render_highlighted_line(state, line);
    const size_t clamped_cursor = cursor > line.size() ? line.size() : cursor;
    const size_t line_display_width = render_utils::measure_display_width(line);
    const size_t cursor_display_width =
        render_utils::measure_display_width(line.substr(0, clamped_cursor));
    const render_utils::CursorGeometry end =
        line.empty()
            ? render_utils::CursorGeometry{}
            : render_utils::compute_render_end_geometry(
                  prefix_info.display_width, line_display_width, columns);
    const render_utils::CursorGeometry cursor_geometry =
        render_utils::compute_cursor_geometry(
            prefix_info.display_width, cursor_display_width, columns,
            cursor_display_width == line_display_width);

    std::string frame;
    if (full_prompt) {
        frame = "\r\033[2K";
    } else if (terminal_resized) {
        frame = render_utils::clear_previous_prompt_block(
            render_state, render_state.terminal_columns, columns);
    } else {
        frame = render_utils::clear_previous_input_block(render_state, columns);
    }
    frame += prefix;
    frame += rendered;

    if (end.row > cursor_geometry.row) {
        frame += "\033[" + std::to_string(end.row - cursor_geometry.row) + "A";
    }
    frame += "\r";
    if (cursor_geometry.column > 0) {
        frame += "\033[" + std::to_string(cursor_geometry.column) + "C";
    }

    shell::terminal::write_stdout(frame);
    update_input_render_state(render_state, header_info.rendered,
                              header_info.display_width,
                              prefix_info.display_width, line_display_width,
                              cursor_display_width, columns);
}

void finalize_interrupted_input_line(InputRenderState &render_state) {
    const size_t columns = render_state.terminal_columns;
    const size_t cursor_row = render_utils::compute_cursor_geometry(
                                  render_state.prompt_prefix_display_width,
                                  render_state.cursor_display_width, columns,
                                  render_state.cursor_display_width ==
                                      render_state.input_display_width)
                                  .row;
    const size_t end_row = render_state.input_display_width == 0
                               ? 0
                               : render_utils::compute_render_end_geometry(
                                     render_state.prompt_prefix_display_width,
                                     render_state.input_display_width, columns)
                                     .row;

    std::string tail_newlines;
    for (size_t i = cursor_row; i < end_row; ++i) {
        tail_newlines += '\n';
    }

    if (!tail_newlines.empty()) {
        shell::terminal::write_stdout(tail_newlines);
    }

    reset_input_render_state(render_state);
}

} // namespace shell::prompt
