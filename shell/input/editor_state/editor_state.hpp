#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace shell::input::editor_state {

struct LineBuffer {
    std::string text;
    size_t cursor = 0;
};

struct HistoryBrowseState {
    bool active = false;
    size_t index = 0;
    std::string draft;
};

bool move_cursor_left(LineBuffer &buffer);
bool move_cursor_right(LineBuffer &buffer);
bool move_cursor_word_left(LineBuffer &buffer);
bool move_cursor_word_right(LineBuffer &buffer);
bool move_cursor_home(LineBuffer &buffer);
bool move_cursor_end(LineBuffer &buffer);

bool insert_text(LineBuffer &buffer, const std::string &in,
                 HistoryBrowseState &history_state, size_t history_size);
bool erase_before_cursor(LineBuffer &buffer, HistoryBrowseState &history_state,
                         size_t history_size);
bool erase_at_cursor(LineBuffer &buffer, HistoryBrowseState &history_state,
                     size_t history_size);

bool browse_history_up(LineBuffer &buffer,
                       const std::vector<std::string> &history,
                       HistoryBrowseState &history_state);
bool browse_history_down(LineBuffer &buffer,
                         const std::vector<std::string> &history,
                         HistoryBrowseState &history_state);

void reset_history_browse(HistoryBrowseState &history_state,
                          size_t history_size);

} // namespace shell::input::editor_state
