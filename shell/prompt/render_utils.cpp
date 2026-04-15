#include "render_utils.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

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
    return RenderedFragment{std::move(rendered),
                            measure_display_width(rendered)};
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

} // namespace shell::prompt::render_utils
