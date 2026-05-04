#include "search.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "../../../prompt/prompt.hpp"
#include "../../../prompt/render_utils.hpp"
#include "../../session_state/session_state.hpp"

namespace shell::input::panels::search {
namespace {

const std::string reverse_video = "\033[7m";
const std::string dim = "\033[2m";
const std::string reset = "\033[0m";

std::vector<std::string>
display_labels(const session_state::SearchState &selection) {
    std::vector<std::string> labels;
    labels.reserve(selection.candidates.size());
    for (const auto &candidate : selection.candidates) {
        labels.push_back(candidate);
    }

    return labels;
}

size_t max_label_width(const std::vector<std::string> &labels) {
    size_t max_width = 0;
    for (const std::string &label : labels) {
        max_width = std::max(
            max_width, prompt::render_utils::measure_display_width(label));
    }
    return max_width;
}

std::string render_candidate_cell(const std::string &label, size_t label_width,
                                  size_t cell_width, bool selected) {
    std::string padded = panels::truncate_plain_text(label, label_width);
    const size_t display_width =
        prompt::render_utils::measure_display_width(padded);
    if (display_width < cell_width) {
        padded.append(cell_width - display_width, ' ');
    }

    if (!selected) {
        return padded + reset;
    }

    std::string rendered = reverse_video;

    if (!padded.empty()) {
        rendered += padded.substr(0, padded.size() - 1);
    }

    rendered += reset;
    if (!padded.empty()) {
        rendered.push_back(padded.back());
    }

    return rendered;
}

std::string build_query_line(const session_state::SearchState &selection,
                             size_t columns) {
    const std::string prefix = "search: ";
    if (selection.query.empty()) {
        const size_t prefix_width =
            prompt::render_utils::measure_display_width(prefix);
        if (columns <= prefix_width) {
            return panels::truncate_plain_text(prefix, columns);
        }

        return prefix + dim +
               panels::truncate_plain_text("type to search history",
                                           columns - prefix_width) +
               reset;
    }

    return panels::truncate_plain_text(prefix + selection.query, columns);
}

std::string build_no_matches_line(size_t columns) {
    return dim + panels::truncate_plain_text("no matches", columns) + reset;
}

} // namespace

Block build_block(const prompt::InputRenderState &render_state,
                  const session_state::SearchState &selection) {
    Block block;
    if (!selection.active) {
        return block;
    }

    const size_t columns = render_state.terminal_columns;
    const size_t panel_row_limit =
        panels::max_panel_rows(render_state, columns);
    panels::append_line(block, build_query_line(selection, columns));

    if (panel_row_limit <= 1) {
        panels::update_block_rows(block, columns);
        return block;
    }

    if (selection.candidates.empty()) {
        panels::append_line(block, build_no_matches_line(columns));
        panels::update_block_rows(block, columns);
        return block;
    }

    const std::vector<std::string> labels = display_labels(selection);
    const size_t max_width = max_label_width(labels);
    const size_t gutter = 2;
    const size_t cell_width =
        columns == 0
            ? 1
            : std::max<size_t>(1, std::min(max_width + gutter, columns));
    const size_t label_width = cell_width > gutter ? cell_width - gutter : 1;
    const size_t cols =
        cell_width == 0 ? 1 : std::max<size_t>(1, columns / cell_width);
    const size_t total_rows = (labels.size() + cols - 1) / cols;
    const bool has_selected_candidate =
        selection.active && !selection.candidates.empty();
    const size_t selected_index =
        has_selected_candidate
            ? selection.selected_index % selection.candidates.size()
            : 0;
    const size_t selected_row = selected_index / cols;
    const size_t candidate_row_limit = panel_row_limit - 1;
    const bool show_footer =
        total_rows > candidate_row_limit && candidate_row_limit > 1;
    const size_t visible_rows = show_footer
                                    ? candidate_row_limit - 1
                                    : std::min(total_rows, candidate_row_limit);
    const size_t window_start_row = panels::compute_window_start_row(
        total_rows, std::max<size_t>(1, visible_rows), selected_row);
    const size_t window_end_row = std::min(
        total_rows, window_start_row + std::max<size_t>(1, visible_rows));

    for (size_t row = window_start_row; row < window_end_row; ++row) {
        std::string line;

        for (size_t col = 0; col < cols; ++col) {
            const size_t index = row * cols + col;
            if (index >= selection.candidates.size()) {
                break;
            }

            const bool is_selected =
                has_selected_candidate && index == selected_index;
            line += render_candidate_cell(labels[index], label_width,
                                          cell_width, is_selected);
        }

        panels::append_line(block, line);
    }

    if (show_footer) {
        const size_t first_visible_item = window_start_row * cols;
        const size_t last_visible_item =
            std::min(selection.candidates.size(), window_end_row * cols) - 1;
        panels::append_line(block, panels::build_items_footer(
                                       first_visible_item, last_visible_item,
                                       selection.candidates.size(), columns));
    }

    panels::update_block_rows(block, columns);
    return block;
}

} // namespace shell::input::panels::search
