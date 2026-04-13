#include "editor_state.hpp"

#include "../../../features/shell_text/shell_text.hpp"

namespace shell::input::editor_state {
namespace {

void clamp_cursor(LineBuffer &buffer) {
    if (buffer.cursor > buffer.text.size()) {
        buffer.cursor = buffer.text.size();
    }
}

void prepare_for_edit(HistoryBrowseState &history_state, size_t history_size) {
    if (!history_state.active) {
        return;
    }

    reset_history_browse(history_state, history_size);
}

} // namespace

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

    if (buffer.cursor == 0) {
        return false;
    }

    size_t cursor = buffer.cursor;
    while (cursor > 0 &&
           features::shell_text::is_shell_separator(buffer.text[cursor - 1])) {
        --cursor;
    }

    while (cursor > 0 &&
           !features::shell_text::is_shell_separator(buffer.text[cursor - 1])) {
        --cursor;
    }

    if (cursor == buffer.cursor) {
        return false;
    }

    buffer.cursor = cursor;
    return true;
}

bool move_cursor_word_right(LineBuffer &buffer) {
    clamp_cursor(buffer);

    const size_t size = buffer.text.size();
    if (buffer.cursor >= size) {
        return false;
    }

    size_t cursor = buffer.cursor;
    while (cursor < size &&
           features::shell_text::is_shell_separator(buffer.text[cursor])) {
        ++cursor;
    }

    while (cursor < size &&
           !features::shell_text::is_shell_separator(buffer.text[cursor])) {
        ++cursor;
    }

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

bool insert_character(LineBuffer &buffer, char ch,
                      HistoryBrowseState &history_state, size_t history_size) {
    clamp_cursor(buffer);
    prepare_for_edit(history_state, history_size);
    buffer.text.insert(buffer.cursor, 1, ch);
    ++buffer.cursor;
    return true;
}

bool erase_before_cursor(LineBuffer &buffer, HistoryBrowseState &history_state,
                         size_t history_size) {
    clamp_cursor(buffer);
    if (buffer.cursor == 0) {
        return false;
    }

    prepare_for_edit(history_state, history_size);
    buffer.text.erase(buffer.cursor - 1, 1);
    --buffer.cursor;
    return true;
}

bool erase_at_cursor(LineBuffer &buffer, HistoryBrowseState &history_state,
                     size_t history_size) {
    clamp_cursor(buffer);
    if (buffer.cursor >= buffer.text.size()) {
        return false;
    }

    prepare_for_edit(history_state, history_size);
    buffer.text.erase(buffer.cursor, 1);
    return true;
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

void reset_history_browse(HistoryBrowseState &history_state,
                          size_t history_size) {
    history_state.active = false;
    history_state.index = history_size;
}

} // namespace shell::input::editor_state
