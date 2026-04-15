#pragma once

#include <cstddef>
#include <string>

namespace shell::prompt::render_utils {

struct CursorGeometry {
    size_t row = 0;
    size_t column = 0;
};

struct RenderedFragment {
    std::string rendered;
    size_t display_width = 0;
};

size_t get_terminal_columns();
size_t measure_display_width(const std::string &text);
RenderedFragment make_rendered_fragment(std::string rendered);

CursorGeometry compute_render_end_geometry(size_t base_column,
                                           size_t display_width,
                                           size_t columns);
CursorGeometry compute_cursor_geometry(size_t base_column, size_t display_width,
                                       size_t columns, bool at_line_end);
size_t compute_rendered_rows(size_t base_column, size_t display_width,
                             size_t columns);

} // namespace shell::prompt::render_utils
