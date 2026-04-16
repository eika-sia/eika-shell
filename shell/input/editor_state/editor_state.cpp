#include "editor_state.hpp"

#include <cctype>

namespace shell::input::editor_state {
namespace {

void clamp_cursor(LineBuffer &buffer) {
    if (buffer.cursor > buffer.text.size()) {
        buffer.cursor = buffer.text.size();
    }
}

bool is_editor_word_character(unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '_';
}

size_t find_word_left_boundary(const LineBuffer &buffer) {
    size_t cursor = buffer.cursor;
    while (cursor > 0 && !is_editor_word_character(static_cast<unsigned char>(
                             buffer.text[cursor - 1]))) {
        --cursor;
    }

    while (cursor > 0 && is_editor_word_character(static_cast<unsigned char>(
                             buffer.text[cursor - 1]))) {
        --cursor;
    }

    return cursor;
}

size_t find_word_right_boundary(const LineBuffer &buffer) {
    size_t cursor = buffer.cursor;
    const size_t size = buffer.text.size();
    while (cursor < size &&
           !is_editor_word_character(
               static_cast<unsigned char>(buffer.text[cursor]))) {
        ++cursor;
    }

    while (cursor < size && is_editor_word_character(static_cast<unsigned char>(
                                buffer.text[cursor]))) {
        ++cursor;
    }

    return cursor;
}

bool erase_range(LineBuffer &buffer, size_t erase_begin, size_t erase_end,
                 std::string *erased_text = nullptr) {
    clamp_cursor(buffer);

    const size_t clamped_begin =
        erase_begin > buffer.text.size() ? buffer.text.size() : erase_begin;
    size_t clamped_end =
        erase_end > buffer.text.size() ? buffer.text.size() : erase_end;
    if (clamped_end < clamped_begin) {
        clamped_end = clamped_begin;
    }

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

    const size_t clamped_begin =
        replace_begin > buffer.text.size() ? buffer.text.size() : replace_begin;
    size_t clamped_end =
        replace_end > buffer.text.size() ? buffer.text.size() : replace_end;
    if (clamped_end < clamped_begin) {
        clamped_end = clamped_begin;
    }

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
    result.changed = erase_range(buffer, kill_begin, kill_end,
                                 &result.killed_text);
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
