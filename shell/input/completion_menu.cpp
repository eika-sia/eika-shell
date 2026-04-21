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

below_prompt_panel::Block
build_candidates_block(const session_state::CompletionSelectionState &selection,
                       size_t columns) {
    below_prompt_panel::Block block;
    if (selection.display_candidates.empty()) {
        return block;
    }

    std::vector<std::string> labels;
    labels.reserve(selection.display_candidates.size());
    for (const auto &candidate : selection.display_candidates) {
        labels.push_back(features::format_completion_display_label(
            candidate.text));
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
    const size_t rows = (labels.size() + cols - 1) / cols;
    const bool has_selected_candidate = selection.active &&
                                        selection.preview_active &&
                                        !selection.display_candidates.empty();
    const size_t selected_index =
        has_selected_candidate
            ? selection.selected_index % selection.display_candidates.size()
            : 0;

    for (size_t row = 0; row < rows; ++row) {
        std::string line;

        for (size_t col = 0; col < cols; ++col) {
            const size_t index = row * cols + col;
            if (index >= selection.display_candidates.size()) {
                break;
            }

            const bool is_selected =
                has_selected_candidate && index == selected_index;
            line += render_candidate_cell(labels[index],
                                          selection.display_candidates[index].kind,
                                          cell_width, is_selected);
        }

        block.text += line;
        block.text.push_back('\n');
    }

    block.rows = rows + 1;
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
        build_candidates_block(selection, render_state.terminal_columns);
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
