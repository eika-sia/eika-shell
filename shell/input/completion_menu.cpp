#include "completion_menu.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "../../features/completion/completion_format.hpp"
#include "../prompt/prompt.hpp"
#include "../prompt/render_utils.hpp"
#include "./session_state/session_state.hpp"

namespace shell::input::completion_menu {
namespace {

const std::string blue = "\033[34m";
const std::string green = "\033[32m";
const std::string dim = "\033[2m";
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
                                  size_t cell_width, bool selected) {
    std::string padded = label;
    const size_t display_width =
        prompt::render_utils::measure_display_width(label);
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

size_t max_panel_rows(const prompt::InputRenderState &render_state,
                      size_t columns) {
    const size_t terminal_rows = prompt::render_utils::get_terminal_rows();
    const prompt::render_utils::RenderMetrics metrics =
        prompt::render_utils::measure_render_state(render_state, columns);

    if (terminal_rows <= metrics.total_rows) {
        return 1;
    }

    return terminal_rows - metrics.total_rows;
}

size_t compute_window_start_row(size_t total_rows, size_t visible_rows,
                                size_t selected_row) {
    if (visible_rows >= total_rows) {
        return 0;
    }

    const size_t half_window = visible_rows / 2;
    const size_t desired_start =
        selected_row > half_window ? selected_row - half_window : 0;
    const size_t max_start = total_rows - visible_rows;
    return std::min(desired_start, max_start);
}

std::string build_truncation_footer(size_t first_item_index,
                                    size_t last_item_index,
                                    size_t total_items) {
    return dim + "Items " + std::to_string(first_item_index + 1) + " to " +
           std::to_string(last_item_index + 1) + " of " +
           std::to_string(total_items) + reset;
}

void append_block_line(below_prompt_panel::Block &block,
                       const std::string &line) {
    if (!block.text.empty()) {
        block.text.push_back('\n');
    }

    block.text += line;
}

below_prompt_panel::Block build_candidates_block(
    const prompt::InputRenderState &render_state,
    const session_state::CompletionSelectionState &selection) {
    below_prompt_panel::Block block;
    if (selection.display_candidates.empty()) {
        return block;
    }

    const size_t columns = render_state.terminal_columns;
    std::vector<std::string> labels;
    labels.reserve(selection.display_candidates.size());
    for (const auto &candidate : selection.display_candidates) {
        labels.push_back(
            features::format_completion_display_label(candidate.text));
    }

    size_t max_width = 0;
    for (const std::string &label : labels) {
        max_width = std::max(
            max_width, prompt::render_utils::measure_display_width(label));
    }

    const size_t gutter = 2;
    const size_t cell_width = max_width + gutter;
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
    const size_t panel_row_limit = max_panel_rows(render_state, columns);
    const bool truncated = total_rows > panel_row_limit;
    const size_t visible_rows = truncated && panel_row_limit > 1
                                    ? panel_row_limit - 1
                                    : std::min(total_rows, panel_row_limit);
    const size_t window_start_row = compute_window_start_row(
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
                cell_width, is_selected);
        }

        append_block_line(block, line);
    }

    if (truncated && panel_row_limit > 1) {
        const size_t first_visible_item = window_start_row * cols;
        const size_t last_visible_item =
            std::min(selection.display_candidates.size(),
                     window_end_row * cols) -
            1;
        append_block_line(block, build_truncation_footer(
                                     first_visible_item, last_visible_item,
                                     selection.display_candidates.size()));
    }

    const size_t rendered_lines = (window_end_row - window_start_row) +
                                  (truncated && panel_row_limit > 1);
    block.rows = rendered_lines;
    return block;
}

} // namespace

bool is_visible(const RenderState &state) {
    return below_prompt_panel::is_visible(state);
}

std::string
build_render_frame(const prompt::InputRenderState &render_state,
                   const session_state::CompletionSelectionState &selection,
                   RenderState &menu_state) {
    const below_prompt_panel::Block block =
        build_candidates_block(render_state, selection);
    return below_prompt_panel::build_render_frame(render_state, block,
                                                  menu_state);
}

std::string build_dismiss_frame(const prompt::InputRenderState &render_state,
                                RenderState &menu_state) {
    return below_prompt_panel::build_dismiss_frame(render_state, menu_state);
}

std::string
build_clear_prompt_and_menu_frame(const prompt::InputRenderState &render_state,
                                  RenderState &menu_state) {
    return below_prompt_panel::build_clear_prompt_and_panel_frame(render_state,
                                                                  menu_state);
}

} // namespace shell::input::completion_menu
