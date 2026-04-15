#include "prompt.hpp"

#include "../../features/highlighting/highlighting.hpp"
#include "../shell.hpp"
#include "../terminal/terminal.hpp"
#include "./prompt_header/prompt_header.hpp"
#include "./render_utils.hpp"

#include <algorithm>
#include <unistd.h>
#include <utility>

namespace shell::prompt {
namespace {

const std::string purple = "\033[1;35m";
const std::string reset = "\033[0m";

struct RenderMetrics {
    size_t header_rows = 1;
    size_t input_rows = 1;
    size_t total_rows = 2;
    size_t cursor_row = 0;
};

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

RenderMetrics measure_render_state(const InputRenderState &render_state,
                                   size_t columns) {
    const bool cursor_at_line_end =
        render_state.cursor_display_width == render_state.input_display_width;

    RenderMetrics metrics;
    metrics.header_rows = render_utils::compute_rendered_rows(
        0, render_state.header_display_width, columns);
    metrics.input_rows = render_utils::compute_rendered_rows(
        render_state.prompt_prefix_display_width,
        render_state.input_display_width, columns);
    metrics.total_rows = metrics.header_rows + metrics.input_rows;
    metrics.cursor_row =
        render_utils::compute_cursor_geometry(
            render_state.prompt_prefix_display_width,
            render_state.cursor_display_width, columns, cursor_at_line_end)
            .row;
    return metrics;
}

std::string clear_render_block(size_t rows_above_cursor, size_t rows_to_clear) {
    std::string frame = "\r";

    if (rows_above_cursor > 0) {
        frame += "\033[" + std::to_string(rows_above_cursor) + "A";
    }
    frame += "\r";

    for (size_t i = 0; i < rows_to_clear; ++i) {
        frame += "\033[2K";
        if (i + 1 < rows_to_clear) {
            frame += "\033[1B\r";
        }
    }

    if (rows_to_clear > 1) {
        frame += "\033[" + std::to_string(rows_to_clear - 1) + "A";
    }
    frame += "\r";

    return frame;
}

std::string clear_previous_input_block(const InputRenderState &render_state,
                                       size_t columns) {
    const RenderMetrics metrics = measure_render_state(render_state, columns);
    return clear_render_block(metrics.cursor_row, metrics.input_rows);
}

std::string clear_previous_prompt_block(const InputRenderState &render_state,
                                        size_t old_columns,
                                        size_t new_columns) {
    const RenderMetrics old_metrics =
        measure_render_state(render_state, old_columns);
    const RenderMetrics new_metrics =
        measure_render_state(render_state, new_columns);

    return clear_render_block(
        new_metrics.cursor_row + new_metrics.header_rows,
        std::max(old_metrics.total_rows, new_metrics.total_rows));
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
        frame = clear_previous_prompt_block(
            render_state, render_state.terminal_columns, columns);
    } else {
        frame = clear_previous_input_block(render_state, columns);
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
