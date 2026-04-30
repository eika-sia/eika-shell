#include "completion_panel.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "../../../../features/completion/completion_format.hpp"
#include "../../../prompt/prompt.hpp"
#include "../../../prompt/render_utils.hpp"
#include "../../session_state/session_state.hpp"

namespace shell::input::panels::completion {
namespace {

const std::string blue = "\033[34m";
const std::string green = "\033[32m";
const std::string reverse_video = "\033[7m";
const std::string reset = "\033[0m";
const std::string none;

const std::string &
color_code_for_candidate(features::CompletionDisplayKind kind) {
    switch (kind) {
    case features::CompletionDisplayKind::Directory:
        return blue;
    case features::CompletionDisplayKind::Executable:
        return green;
    case features::CompletionDisplayKind::Plain:
    case features::CompletionDisplayKind::Builtin:
    case features::CompletionDisplayKind::Alias:
        return none;
    }

    return none;
}

std::string render_candidate_cell(const std::string &label,
                                  features::CompletionDisplayKind kind,
                                  size_t label_width, size_t cell_width,
                                  bool selected) {
    std::string padded =
        shell::input::panels::truncate_plain_text(label, label_width);
    const size_t display_width =
        prompt::render_utils::measure_display_width(padded);
    if (display_width < cell_width) {
        padded.append(cell_width - display_width, ' ');
    }

    const std::string &color = color_code_for_candidate(kind);
    if (!selected) {
        if (color.empty()) {
            return padded;
        }

        return color + padded + reset;
    }

    std::string rendered = reverse_video;
    if (!color.empty()) {
        rendered += color;
    }

    if (!padded.empty()) {
        rendered += padded.substr(0, padded.size() - 1);
    }

    rendered += reset;
    if (!padded.empty()) {
        rendered.push_back(padded.back());
    }

    return rendered;
}

std::vector<std::string>
display_labels(const session_state::CompletionSelectionState &selection) {
    std::vector<std::string> labels;
    labels.reserve(selection.display_candidates.size());
    for (const auto &candidate : selection.display_candidates) {
        labels.push_back(
            features::format_completion_display_label(candidate.text));
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

} // namespace

Block build_block(const prompt::InputRenderState &render_state,
                  const session_state::CompletionSelectionState &selection) {
    Block block;
    if (selection.display_candidates.empty()) {
        return block;
    }

    const size_t columns = render_state.terminal_columns;
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
    const bool has_selected_candidate = selection.active &&
                                        selection.preview_active &&
                                        !selection.display_candidates.empty();
    const size_t selected_index =
        has_selected_candidate
            ? selection.selected_index % selection.display_candidates.size()
            : 0;
    const size_t selected_row = selected_index / cols;
    const size_t panel_row_limit =
        shell::input::panels::max_panel_rows(render_state, columns);
    const bool truncated = total_rows > panel_row_limit;
    const size_t visible_rows = truncated && panel_row_limit > 1
                                    ? panel_row_limit - 1
                                    : std::min(total_rows, panel_row_limit);
    const size_t window_start_row =
        shell::input::panels::compute_window_start_row(
            total_rows, std::max<size_t>(1, visible_rows), selected_row);
    const size_t window_end_row = std::min(
        total_rows, window_start_row + std::max<size_t>(1, visible_rows));

    for (size_t row = window_start_row; row < window_end_row; ++row) {
        std::string line;

        for (size_t col = 0; col < cols; ++col) {
            const size_t index = row * cols + col;
            if (index >= selection.display_candidates.size()) {
                break;
            }

            const bool is_selected =
                has_selected_candidate && index == selected_index;
            line += render_candidate_cell(
                labels[index], selection.display_candidates[index].kind,
                label_width, cell_width, is_selected);
        }

        shell::input::panels::append_line(block, line);
    }

    if (truncated && panel_row_limit > 1) {
        const size_t first_visible_item = window_start_row * cols;
        const size_t last_visible_item =
            std::min(selection.display_candidates.size(),
                     window_end_row * cols) -
            1;
        shell::input::panels::append_line(
            block, shell::input::panels::build_items_footer(
                       first_visible_item, last_visible_item,
                       selection.display_candidates.size(), columns));
    }

    shell::input::panels::update_block_rows(block, columns);
    return block;
}

} // namespace shell::input::panels::completion
