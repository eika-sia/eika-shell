#pragma once

#include <cstddef>
#include <string>

namespace shell::prompt {
struct InputRenderState;

namespace render_utils {

struct CursorGeometry {
    size_t row = 0;
    size_t column = 0;
};

struct RenderMetrics {
    size_t header_rows = 1;
    size_t input_rows = 1;
    size_t total_rows = 2;
    size_t cursor_row = 0;
};

size_t get_terminal_columns();
size_t get_terminal_rows();
size_t measure_display_width(const std::string &text);
size_t measure_rendered_block_rows(const std::string &text, size_t columns);

CursorGeometry compute_render_end_geometry(size_t base_column,
                                           size_t display_width,
                                           size_t columns);
CursorGeometry compute_cursor_geometry(size_t base_column, size_t display_width,
                                       size_t columns, bool at_line_end);
size_t compute_rendered_rows(size_t base_column, size_t display_width,
                             size_t columns);
RenderMetrics measure_render_state(const InputRenderState &render_state,
                                   size_t columns);
std::string clear_render_block(size_t rows_above_cursor, size_t rows_to_clear);
std::string clear_rendered_prompt_block(const InputRenderState &render_state,
                                        size_t rows_below_prompt = 0);
std::string clear_previous_input_block(const InputRenderState &render_state,
                                       size_t columns);
std::string clear_previous_prompt_block(const InputRenderState &render_state,
                                        size_t old_columns, size_t new_columns);

} // namespace render_utils
} // namespace shell::prompt
