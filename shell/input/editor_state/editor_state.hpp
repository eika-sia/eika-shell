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

enum class Movement {
    Left,
    Right,
    WordLeft,
    WordRight,
    Home,
    End,
};

enum class Erase {
    BeforeCursor,
    AtCursor,
};

enum class HistoryNavigation {
    Up,
    Down,
};

bool apply_movement(LineBuffer &buffer, Movement movement);

bool insert_text(LineBuffer &buffer, const std::string &in,
                 HistoryBrowseState &history_state, size_t history_size);
bool replace_range(LineBuffer &buffer, size_t replace_begin, size_t replace_end,
                   const std::string &replacement,
                   HistoryBrowseState &history_state, size_t history_size);
bool apply_erase(LineBuffer &buffer, Erase erase_action,
                 HistoryBrowseState &history_state, size_t history_size);

bool apply_history_navigation(LineBuffer &buffer, HistoryNavigation navigation,
                              const std::vector<std::string> &history,
                              HistoryBrowseState &history_state);

void reset_history_browse(HistoryBrowseState &history_state,
                          size_t history_size);

} // namespace shell::input::editor_state
