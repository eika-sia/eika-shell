#include "prompt.hpp"

#include "../../features/highlighting/highlighting.hpp"
#include "../shell.hpp"
#include "./prompt_header/prompt_header.hpp"

#include <algorithm>
#include <linux/limits.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>

namespace shell::prompt {
namespace {

const std::string purple = "\033[1;35m";
const std::string cyan = "\033[1;36m";
const std::string reset = "\033[0m";
constexpr size_t prompt_prefix_display_width = 4;

struct CursorGeometry {
    size_t row = 0;
    size_t column = prompt_prefix_display_width;
};

struct RenderMetrics {
    size_t header_rows = 1;
    size_t input_rows = 1;
    size_t total_rows = 2;
    size_t cursor_row = 0;
};

size_t get_terminal_columns() {
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }

    return 80;
}

CursorGeometry compute_render_end_geometry(size_t base_column,
                                           size_t character_count,
                                           size_t columns) {
    const size_t absolute_offset = base_column + character_count;

    if (absolute_offset > 0 && absolute_offset % columns == 0) {
        // Terminals delay the wrap until the next printable character, so an
        // exact edge hit still visually sits on the previous row.
        return CursorGeometry{absolute_offset / columns - 1, columns - 1};
    }

    return CursorGeometry{absolute_offset / columns, absolute_offset % columns};
}

CursorGeometry compute_cursor_geometry(size_t base_column,
                                       size_t character_count, size_t columns,
                                       bool at_line_end) {
    if (at_line_end) {
        return compute_render_end_geometry(base_column, character_count,
                                           columns);
    }

    const size_t absolute_offset = base_column + character_count;
    return CursorGeometry{absolute_offset / columns, absolute_offset % columns};
}

size_t compute_rendered_rows(size_t base_column, size_t character_count,
                             size_t columns) {
    if (character_count == 0) {
        return 1;
    }

    return compute_render_end_geometry(base_column, character_count, columns)
               .row +
           1;
}

void reset_input_render_state(InputRenderState &render_state) {
    render_state = {};
}

void update_input_render_state(InputRenderState &render_state,
                               std::string header_rendered,
                               size_t header_display_width, size_t input_length,
                               size_t cursor_index, size_t terminal_columns) {
    render_state.header_rendered = std::move(header_rendered);
    render_state.header_display_width = header_display_width;
    render_state.input_length = input_length;
    render_state.cursor_index = cursor_index;
    render_state.terminal_columns = terminal_columns;
}

RenderMetrics measure_render_state(const InputRenderState &render_state,
                                   size_t columns) {
    const bool cursor_at_line_end =
        render_state.cursor_index == render_state.input_length;

    RenderMetrics metrics;
    metrics.header_rows =
        compute_rendered_rows(0, render_state.header_display_width, columns);
    metrics.input_rows = compute_rendered_rows(
        prompt_prefix_display_width, render_state.input_length, columns);
    metrics.total_rows = metrics.header_rows + metrics.input_rows;
    metrics.cursor_row = compute_cursor_geometry(prompt_prefix_display_width,
                                                 render_state.cursor_index,
                                                 columns, cursor_at_line_end)
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

std::string build_prompt_prefix() {
    return purple + std::string("╰─❯ ") + reset;
}

prompt_header::HeaderInfo build_header_info(const shell::ShellState &state) {
    return prompt_header::build_header(state);
}

} // namespace

std::string build_prompt(const shell::ShellState &state,
                         InputRenderState &render_state) {
    const prompt_header::HeaderInfo header = build_header_info(state);
    reset_input_render_state(render_state);
    update_input_render_state(render_state, header.rendered,
                              header.display_width, 0, 0,
                              get_terminal_columns());
    return header.rendered + "\n" + build_prompt_prefix();
}

void redraw_input_line(InputRenderState &render_state,
                       const shell::ShellState &state, const std::string &line,
                       size_t cursor, bool full_prompt) {
    const size_t columns = get_terminal_columns();
    const bool terminal_resized = columns != render_state.terminal_columns;
    const bool redraw_full_prompt = full_prompt || terminal_resized;
    const prompt_header::HeaderInfo header_info =
        redraw_full_prompt
            ? build_header_info(state)
            : prompt_header::HeaderInfo{render_state.header_rendered,
                                        render_state.header_display_width};
    const std::string header = redraw_full_prompt ? header_info.rendered : "";
    const std::string prefix = redraw_full_prompt
                                   ? header + "\n" + build_prompt_prefix()
                                   : build_prompt_prefix();
    const std::string rendered =
        features::highlighting::render_highlighted_line(state, line);
    const size_t clamped_cursor = cursor > line.size() ? line.size() : cursor;
    const CursorGeometry end =
        line.empty() ? CursorGeometry{}
                     : compute_render_end_geometry(prompt_prefix_display_width,
                                                   line.size(), columns);
    const CursorGeometry cursor_geometry =
        compute_cursor_geometry(prompt_prefix_display_width, clamped_cursor,
                                columns, clamped_cursor == line.size());

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

    write(STDOUT_FILENO, frame.c_str(), frame.size());
    update_input_render_state(render_state, header_info.rendered,
                              header_info.display_width, line.size(),
                              clamped_cursor, columns);
}

void finalize_interrupted_input_line(InputRenderState &render_state) {
    const size_t columns = render_state.terminal_columns;
    const size_t cursor_row =
        compute_cursor_geometry(
            prompt_prefix_display_width, render_state.cursor_index, columns,
            render_state.cursor_index == render_state.input_length)
            .row;
    const size_t end_row =
        render_state.input_length == 0
            ? 0
            : compute_render_end_geometry(prompt_prefix_display_width,
                                          render_state.input_length, columns)
                  .row;

    std::string tail_newlines;
    for (size_t i = cursor_row; i < end_row; ++i) {
        tail_newlines += '\n';
    }

    if (!tail_newlines.empty()) {
        write(STDOUT_FILENO, tail_newlines.c_str(), tail_newlines.size());
    }

    reset_input_render_state(render_state);
}

} // namespace shell::prompt
