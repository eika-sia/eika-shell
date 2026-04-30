#pragma once

#include <string>
#include <vector>

#include "../../../features/completion/completion.hpp"
#include "../editor_state/editor_state.hpp"

namespace shell::input::session_state {

struct YankState {
    bool active = false;
    size_t ring_index = 0;
    size_t replace_begin = 0;
    size_t replace_end = 0;
};

struct KillRingState {
    std::vector<std::string> entries;
    size_t max_entries = 32;
    bool last_command_was_kill = false;
    editor_state::KillDirection last_kill_direction =
        editor_state::KillDirection::Forward;
    YankState yank;
};

struct BufferSnapshot {
    std::string text;
    size_t cursor = 0;
};

struct CompletionSelectionState {
    bool active = false;
    bool preview_active = false;
    BufferSnapshot anchor;
    size_t replace_begin = 0;
    size_t replace_end = 0;
    std::vector<std::string> candidates;
    std::vector<features::CompletionDisplayCandidate> display_candidates;
    size_t selected_index = 0;
};

enum class TransientPreviewKind {
    None,
    Completion,
};

enum UndoGroupKind {
    None,
    InsertText,
    InsertBlank,
    Backspace,
    Delete,
    KillForward,
    KillBackward
};

struct UndoState {
    std::vector<BufferSnapshot> undo_stack;
    std::vector<BufferSnapshot> redo_stack;
    UndoGroupKind open_group = UndoGroupKind::None;
};

struct EditorSessionState {
    editor_state::HistoryBrowseState history;
    KillRingState kill_ring;
    CompletionSelectionState completion;
    UndoState undo;
};

void initialize_editor_session(EditorSessionState &session,
                               size_t history_size);
void note_non_kill_command(EditorSessionState &session,
                           bool invalidate_yank = true);
TransientPreviewKind
active_transient_preview_kind(const EditorSessionState &session);
void clear_transient_previews(EditorSessionState &session);
bool cancel_active_preview(EditorSessionState &session,
                           editor_state::LineBuffer &buffer,
                           size_t history_size);
bool accept_active_preview(EditorSessionState &session,
                           editor_state::LineBuffer &buffer,
                           size_t history_size);

bool insert_typed_text(EditorSessionState &session,
                       editor_state::LineBuffer &buffer, size_t history_size,
                       const std::string &text);
bool insert_pasted_text(EditorSessionState &session,
                        editor_state::LineBuffer &buffer, size_t history_size,
                        const std::string &text);
bool replace_range(EditorSessionState &session,
                   editor_state::LineBuffer &buffer, size_t history_size,
                   size_t replace_begin, size_t replace_end,
                   const std::string &replacement);
bool apply_erase(EditorSessionState &session, editor_state::LineBuffer &buffer,
                 size_t history_size, editor_state::Erase erase_action);
editor_state::KillResult apply_kill(EditorSessionState &session,
                                    editor_state::LineBuffer &buffer,
                                    size_t history_size,
                                    editor_state::Kill kill_action);
bool yank_latest(EditorSessionState &session, editor_state::LineBuffer &buffer,
                 size_t history_size);
bool yank_pop(EditorSessionState &session, editor_state::LineBuffer &buffer,
              size_t history_size);
bool apply_history_navigation(EditorSessionState &session,
                              editor_state::LineBuffer &buffer,
                              editor_state::HistoryNavigation navigation,
                              const std::vector<std::string> &history);

void begin_completion_selection(
    EditorSessionState &session, const editor_state::LineBuffer &buffer,
    size_t replace_begin, size_t replace_end,
    std::vector<std::string> candidates,
    std::vector<features::CompletionDisplayCandidate> display_candidates);
bool step_completion_selection(EditorSessionState &session,
                               editor_state::LineBuffer &buffer,
                               size_t history_size, bool reverse = false);
bool cancel_completion_selection(EditorSessionState &session,
                                 editor_state::LineBuffer &buffer,
                                 size_t history_size);
bool accept_completion_selection(EditorSessionState &session,
                                 editor_state::LineBuffer &buffer,
                                 size_t history_size);

bool undo(EditorSessionState &session, editor_state::LineBuffer &,
          size_t history_size);
bool redo(EditorSessionState &session, editor_state::LineBuffer &,
          size_t history_size);

} // namespace shell::input::session_state
