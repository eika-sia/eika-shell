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

enum class Kill {
    WordLeft,
    ToLineStart,
    ToLineEnd,
    WordRight,
};

enum class KillDirection {
    Forward,
    Backward,
};

struct KillResult {
    bool changed = false;
    std::string killed_text;
    KillDirection direction = KillDirection::Forward;
};

bool apply_movement(LineBuffer &buffer, Movement movement);

bool insert_text(LineBuffer &buffer, const std::string &in);
bool replace_range(LineBuffer &buffer, size_t replace_begin, size_t replace_end,
                   const std::string &replacement);
bool apply_erase(LineBuffer &buffer, Erase erase_action);
KillResult apply_kill(LineBuffer &buffer, Kill kill_action);

bool apply_history_navigation(LineBuffer &buffer, HistoryNavigation navigation,
                              const std::vector<std::string> &history,
                              HistoryBrowseState &history_state);

void reset_history_browse(HistoryBrowseState &history_state,
                          size_t history_size);

} // namespace shell::input::editor_state
