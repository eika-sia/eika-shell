#include "editor_state.hpp"

#include "../../../features/shell_text/shell_text.hpp"
#include <vector>

namespace shell::input::editor_state {
namespace {

void clamp_cursor(LineBuffer &buffer) {
    if (buffer.cursor > buffer.text.size()) {
        buffer.cursor = buffer.text.size();
    }
}

void clamp_range(size_t text_size, size_t &range_begin, size_t &range_end) {
    if (range_begin > text_size) {
        range_begin = text_size;
    }
    if (range_end > text_size) {
        range_end = text_size;
    }
    if (range_end < range_begin) {
        range_end = range_begin;
    }
}

std::vector<bool> build_shell_separator_mask(const std::string &text) {
    std::vector<bool> separators(text.size(), false);

    features::shell_text::for_each_unescaped_position(
        text, [&](size_t &i, const features::shell_text::ScanState &) {
            separators[i] = features::shell_text::is_shell_separator(text[i]);
            return true;
        });

    return separators;
}

bool is_shell_separator_at(const std::vector<bool> &separators, size_t index) {
    return index < separators.size() && separators[index];
}

size_t find_word_left_boundary(const LineBuffer &buffer) {
    const std::vector<bool> separators =
        build_shell_separator_mask(buffer.text);
    size_t cursor = buffer.cursor;

    while (cursor > 0 && is_shell_separator_at(separators, cursor - 1)) {
        --cursor;
    }

    while (cursor > 0 && !is_shell_separator_at(separators, cursor - 1)) {
        --cursor;
    }

    return cursor;
}

size_t find_word_right_boundary(const LineBuffer &buffer) {
    const std::vector<bool> separators =
        build_shell_separator_mask(buffer.text);
    size_t cursor = buffer.cursor;
    const size_t size = buffer.text.size();

    while (cursor < size && is_shell_separator_at(separators, cursor)) {
        ++cursor;
    }

    while (cursor < size && !is_shell_separator_at(separators, cursor)) {
        ++cursor;
    }

    return cursor;
}

bool erase_range(LineBuffer &buffer, size_t erase_begin, size_t erase_end,
                 std::string *erased_text = nullptr) {
    clamp_cursor(buffer);

    size_t clamped_begin = erase_begin;
    size_t clamped_end = erase_end;
    clamp_range(buffer.text.size(), clamped_begin, clamped_end);

    if (clamped_begin == clamped_end) {
        return false;
    }

    if (erased_text != nullptr) {
        *erased_text =
            buffer.text.substr(clamped_begin, clamped_end - clamped_begin);
    }

    buffer.text.erase(clamped_begin, clamped_end - clamped_begin);
    buffer.cursor = clamped_begin;
    return true;
}

bool move_cursor_left(LineBuffer &buffer) {
    clamp_cursor(buffer);
    if (buffer.cursor == 0) {
        return false;
    }

    --buffer.cursor;
    return true;
}

bool move_cursor_right(LineBuffer &buffer) {
    clamp_cursor(buffer);
    if (buffer.cursor >= buffer.text.size()) {
        return false;
    }

    ++buffer.cursor;
    return true;
}

bool move_cursor_word_left(LineBuffer &buffer) {
    clamp_cursor(buffer);
    const size_t cursor = find_word_left_boundary(buffer);
    if (cursor == buffer.cursor) {
        return false;
    }

    buffer.cursor = cursor;
    return true;
}

bool move_cursor_word_right(LineBuffer &buffer) {
    clamp_cursor(buffer);
    const size_t cursor = find_word_right_boundary(buffer);
    if (cursor == buffer.cursor) {
        return false;
    }

    buffer.cursor = cursor;
    return true;
}

bool move_cursor_home(LineBuffer &buffer) {
    clamp_cursor(buffer);
    if (buffer.cursor == 0) {
        return false;
    }

    buffer.cursor = 0;
    return true;
}

bool move_cursor_end(LineBuffer &buffer) {
    clamp_cursor(buffer);
    if (buffer.cursor == buffer.text.size()) {
        return false;
    }

    buffer.cursor = buffer.text.size();
    return true;
}

bool erase_before_cursor(LineBuffer &buffer) {
    clamp_cursor(buffer);
    return erase_range(buffer, buffer.cursor == 0 ? 0 : buffer.cursor - 1,
                       buffer.cursor);
}

bool erase_at_cursor(LineBuffer &buffer) {
    clamp_cursor(buffer);
    return erase_range(buffer, buffer.cursor, buffer.cursor + 1);
}

bool browse_history_up(LineBuffer &buffer,
                       const std::vector<std::string> &history,
                       HistoryBrowseState &history_state) {
    if (history.empty()) {
        return false;
    }

    if (!history_state.active) {
        history_state.draft = buffer.text;
        history_state.active = true;
        history_state.index = history.size() - 1;
    } else if (history_state.index > 0) {
        --history_state.index;
    }

    buffer.text = history[history_state.index];
    buffer.cursor = buffer.text.size();
    return true;
}

bool browse_history_down(LineBuffer &buffer,
                         const std::vector<std::string> &history,
                         HistoryBrowseState &history_state) {
    if (!history_state.active) {
        return false;
    }

    if (history_state.index + 1 < history.size()) {
        ++history_state.index;
        buffer.text = history[history_state.index];
    } else {
        reset_history_browse(history_state, history.size());
        buffer.text = history_state.draft;
    }

    buffer.cursor = buffer.text.size();
    return true;
}

} // namespace

bool apply_movement(LineBuffer &buffer, Movement movement) {
    switch (movement) {
    case Movement::Left:
        return move_cursor_left(buffer);
    case Movement::Right:
        return move_cursor_right(buffer);
    case Movement::WordLeft:
        return move_cursor_word_left(buffer);
    case Movement::WordRight:
        return move_cursor_word_right(buffer);
    case Movement::Home:
        return move_cursor_home(buffer);
    case Movement::End:
        return move_cursor_end(buffer);
    }

    return false;
}

bool restore_buffer(LineBuffer &buffer, const std::string &text,
                    size_t cursor) {
    const size_t clamped_cursor = cursor > text.size() ? text.size() : cursor;
    if (buffer.text == text && buffer.cursor == clamped_cursor) {
        return false;
    }

    buffer.text = text;
    buffer.cursor = clamped_cursor;
    return true;
}

bool insert_text(LineBuffer &buffer, const std::string &in) {
    if (in.empty()) {
        return false;
    }

    clamp_cursor(buffer);
    buffer.text.insert(buffer.cursor, in);
    buffer.cursor += in.length();
    return true;
}

bool replace_range(LineBuffer &buffer, size_t replace_begin, size_t replace_end,
                   const std::string &replacement) {
    clamp_cursor(buffer);

    size_t clamped_begin = replace_begin;
    size_t clamped_end = replace_end;
    clamp_range(buffer.text.size(), clamped_begin, clamped_end);

    const size_t new_cursor = clamped_begin + replacement.size();
    if (buffer.text.compare(clamped_begin, clamped_end - clamped_begin,
                            replacement) == 0 &&
        buffer.cursor == new_cursor) {
        return false;
    }

    buffer.text.replace(clamped_begin, clamped_end - clamped_begin,
                        replacement);
    buffer.cursor = new_cursor;
    return true;
}

bool replace_range_from_anchor(LineBuffer &buffer,
                               const std::string &anchor_text,
                               size_t replace_begin, size_t replace_end,
                               const std::string &replacement) {
    size_t clamped_begin = replace_begin;
    size_t clamped_end = replace_end;
    clamp_range(anchor_text.size(), clamped_begin, clamped_end);

    std::string replaced_text = anchor_text;
    replaced_text.replace(clamped_begin, clamped_end - clamped_begin,
                          replacement);
    const size_t new_cursor = clamped_begin + replacement.size();
    return restore_buffer(buffer, replaced_text, new_cursor);
}

bool apply_erase(LineBuffer &buffer, Erase erase_action) {
    switch (erase_action) {
    case Erase::BeforeCursor:
        return erase_before_cursor(buffer);
    case Erase::AtCursor:
        return erase_at_cursor(buffer);
    }

    return false;
}

KillResult apply_kill(LineBuffer &buffer, Kill kill_action) {
    clamp_cursor(buffer);

    size_t kill_begin = buffer.cursor;
    size_t kill_end = buffer.cursor;
    KillDirection direction = KillDirection::Forward;

    switch (kill_action) {
    case Kill::WordLeft:
        kill_begin = find_word_left_boundary(buffer);
        direction = KillDirection::Backward;
        break;
    case Kill::ToLineStart:
        kill_begin = 0;
        direction = KillDirection::Backward;
        break;
    case Kill::ToLineEnd:
        kill_end = buffer.text.size();
        direction = KillDirection::Forward;
        break;
    case Kill::WordRight:
        kill_end = find_word_right_boundary(buffer);
        direction = KillDirection::Forward;
        break;
    }

    KillResult result{};
    result.direction = direction;
    result.changed =
        erase_range(buffer, kill_begin, kill_end, &result.killed_text);
    if (!result.changed) {
        result.killed_text.clear();
    }
    return result;
}

bool apply_history_navigation(LineBuffer &buffer, HistoryNavigation navigation,
                              const std::vector<std::string> &history,
                              HistoryBrowseState &history_state) {
    switch (navigation) {
    case HistoryNavigation::Up:
        return browse_history_up(buffer, history, history_state);
    case HistoryNavigation::Down:
        return browse_history_down(buffer, history, history_state);
    }

    return false;
}

void reset_history_browse(HistoryBrowseState &history_state,
                          size_t history_size) {
    history_state.active = false;
    history_state.index = history_size;
}

} // namespace shell::input::editor_state
