#include "render_utils.hpp"
#include "prompt.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

namespace shell::prompt::render_utils {

size_t get_terminal_columns() {
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }

    return 80;
}

size_t measure_display_width(const std::string &text) {
    size_t width = 0;

    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);

        if (c == '\033') {
            ++i;
            if (i < text.size() && text[i] == '[') {
                ++i;
                while (i < text.size() && !(text[i] >= '@' && text[i] <= '~')) {
                    ++i;
                }
                if (i < text.size()) {
                    ++i;
                }
            }
            continue;
        }

        if ((c & 0xC0) != 0x80) {
            ++width;
        }
        ++i;
    }

    return width;
}

RenderedFragment make_rendered_fragment(std::string rendered) {
    const size_t display_width = measure_display_width(rendered);
    return RenderedFragment{std::move(rendered), display_width};
}

CursorGeometry compute_render_end_geometry(size_t base_column,
                                           size_t display_width,
                                           size_t columns) {
    const size_t absolute_offset = base_column + display_width;

    if (absolute_offset > 0 && absolute_offset % columns == 0) {
        // Terminals delay the wrap until the next printable character, so an
        // exact edge hit still visually sits on the previous row.
        return CursorGeometry{absolute_offset / columns - 1, columns - 1};
    }

    return CursorGeometry{absolute_offset / columns, absolute_offset % columns};
}

CursorGeometry compute_cursor_geometry(size_t base_column, size_t display_width,
                                       size_t columns, bool at_line_end) {
    if (at_line_end) {
        return compute_render_end_geometry(base_column, display_width, columns);
    }

    const size_t absolute_offset = base_column + display_width;
    return CursorGeometry{absolute_offset / columns, absolute_offset % columns};
}

size_t compute_rendered_rows(size_t base_column, size_t display_width,
                             size_t columns) {
    if (display_width == 0) {
        return 1;
    }

    return compute_render_end_geometry(base_column, display_width, columns)
               .row +
           1;
}

RenderMetrics measure_render_state(const InputRenderState &render_state,
                                   size_t columns) {
    const bool cursor_at_line_end =
        render_state.cursor_display_width == render_state.input_display_width;

    RenderMetrics metrics;
    metrics.header_rows = compute_rendered_rows(
        0, render_state.header_display_width, columns);
    metrics.input_rows = compute_rendered_rows(
        render_state.prompt_prefix_display_width,
        render_state.input_display_width, columns);
    metrics.total_rows = metrics.header_rows + metrics.input_rows;
    metrics.cursor_row =
        compute_cursor_geometry(render_state.prompt_prefix_display_width,
                                render_state.cursor_display_width, columns,
                                cursor_at_line_end)
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

std::string clear_rendered_prompt_block(const InputRenderState &render_state,
                                        size_t rows_below_prompt) {
    const size_t columns = render_state.terminal_columns;
    const RenderMetrics metrics = measure_render_state(render_state, columns);
    return clear_render_block(metrics.header_rows + metrics.cursor_row,
                              metrics.total_rows + rows_below_prompt);
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

} // namespace shell::prompt::render_utils
