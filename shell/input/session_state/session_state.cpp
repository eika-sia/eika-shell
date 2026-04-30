#include "session_state.hpp"

namespace shell::input::session_state {
namespace {

void prepare_for_buffer_edit(EditorSessionState &session, size_t history_size) {
    if (!session.history.active) {
        return;
    }

    editor_state::reset_history_browse(session.history, history_size);
}

size_t latest_ring_index(const KillRingState &kill_ring) {
    return kill_ring.entries.empty() ? 0 : kill_ring.entries.size() - 1;
}

void trim_kill_ring(KillRingState &kill_ring) {
    if (kill_ring.max_entries == 0 ||
        kill_ring.entries.size() <= kill_ring.max_entries) {
        return;
    }

    const size_t overflow = kill_ring.entries.size() - kill_ring.max_entries;
    kill_ring.entries.erase(kill_ring.entries.begin(),
                            kill_ring.entries.begin() + overflow);
}

void reset_kill_chain(KillRingState &kill_ring) {
    kill_ring.last_command_was_kill = false;
}

void invalidate_yank(KillRingState &kill_ring) { kill_ring.yank = {}; }

void record_kill(KillRingState &kill_ring, const std::string &killed_text,
                 editor_state::KillDirection direction) {
    if (killed_text.empty()) {
        return;
    }

    invalidate_yank(kill_ring);

    if (kill_ring.entries.empty() || !kill_ring.last_command_was_kill ||
        kill_ring.last_kill_direction != direction) {
        kill_ring.entries.push_back(killed_text);
    } else if (direction == editor_state::KillDirection::Forward) {
        kill_ring.entries.back() += killed_text;
    } else {
        kill_ring.entries.back() = killed_text + kill_ring.entries.back();
    }

    trim_kill_ring(kill_ring);
    kill_ring.last_command_was_kill = true;
    kill_ring.last_kill_direction = direction;
}

const std::string *latest_kill(const KillRingState &kill_ring) {
    if (kill_ring.entries.empty()) {
        return nullptr;
    }

    return &kill_ring.entries.back();
}

const std::string *begin_yank(KillRingState &kill_ring, size_t replace_begin) {
    const std::string *entry = latest_kill(kill_ring);
    if (entry == nullptr) {
        invalidate_yank(kill_ring);
        return nullptr;
    }

    kill_ring.yank.active = true;
    kill_ring.yank.ring_index = latest_ring_index(kill_ring);
    kill_ring.yank.replace_begin = replace_begin;
    kill_ring.yank.replace_end = replace_begin + entry->size();
    return entry;
}

const std::string *rotate_yank(KillRingState &kill_ring) {
    if (!kill_ring.yank.active || kill_ring.entries.size() < 2) {
        return nullptr;
    }

    if (kill_ring.yank.ring_index == 0) {
        kill_ring.yank.ring_index = kill_ring.entries.size() - 1;
    } else {
        --kill_ring.yank.ring_index;
    }

    const std::string &entry = kill_ring.entries[kill_ring.yank.ring_index];
    kill_ring.yank.replace_end = kill_ring.yank.replace_begin + entry.size();
    return &entry;
}

void prepare_for_mutating_edit(EditorSessionState &session, size_t history_size,
                               bool should_invalidate_yank = true) {
    reset_kill_chain(session.kill_ring);
    if (should_invalidate_yank) {
        invalidate_yank(session.kill_ring);
    }
    prepare_for_buffer_edit(session, history_size);
}

BufferSnapshot capture_snapshot(const editor_state::LineBuffer &buffer) {
    return BufferSnapshot{buffer.text, buffer.cursor > buffer.text.size()
                                           ? buffer.text.size()
                                           : buffer.cursor};
}

inline bool restore_snapshot(editor_state::LineBuffer &buffer,
                             const BufferSnapshot &snapshot) {
    return editor_state::restore_buffer(buffer, snapshot.text, snapshot.cursor);
}

inline void invalidate_redo(UndoState &undo_state) {
    undo_state.redo_stack.clear();
}

inline void close_undo_group(UndoState &undo_state) {
    undo_state.open_group = UndoGroupKind::None;
}

void clear_transient_preview_states(EditorSessionState &session) {
    session.completion = {};
}

void reset_transient_state_for_restore(EditorSessionState &session,
                                       size_t history_size) {
    if (session.history.active)
        editor_state::reset_history_browse(session.history, history_size);

    clear_transient_previews(session);

    reset_kill_chain(session.kill_ring);

    invalidate_yank(session.kill_ring);

    close_undo_group(session.undo);
}

void record_successful_edit(EditorSessionState &session, BufferSnapshot before,
                            UndoGroupKind group, bool keep_group_open) {
    const bool continuing_group =
        keep_group_open && session.undo.open_group == group;

    if (!continuing_group) {
        invalidate_redo(session.undo);
        session.undo.undo_stack.push_back(before);
    }

    session.undo.open_group = keep_group_open ? group : UndoGroupKind::None;
}

bool completion_matches_anchor(const CompletionSelectionState &completion,
                               const editor_state::LineBuffer &buffer) {
    const size_t anchor_cursor =
        completion.anchor.cursor > completion.anchor.text.size()
            ? completion.anchor.text.size()
            : completion.anchor.cursor;

    return buffer.text == completion.anchor.text &&
           buffer.cursor == anchor_cursor;
}

bool is_typed_group_separator(const std::string &text) {
    return text.size() == 1 && (text[0] == ' ' || text[0] == '\t');
}

UndoGroupKind typed_insert_group_kind(const std::string &text) {
    return is_typed_group_separator(text) ? UndoGroupKind::InsertBlank
                                          : UndoGroupKind::InsertText;
}

} // namespace

void initialize_editor_session(EditorSessionState &session,
                               size_t history_size) {
    session.history.index = history_size;
    session.undo = {};
}

void note_non_kill_command(EditorSessionState &session,
                           bool should_invalidate_yank) {
    reset_kill_chain(session.kill_ring);
    if (should_invalidate_yank) {
        invalidate_yank(session.kill_ring);
    }

    close_undo_group(session.undo);
}

TransientPreviewKind
active_transient_preview_kind(const EditorSessionState &session) {
    if (session.completion.active) {
        return TransientPreviewKind::Completion;
    }

    return TransientPreviewKind::None;
}

void clear_transient_previews(EditorSessionState &session) {
    clear_transient_preview_states(session);
}

bool insert_typed_text(EditorSessionState &session,
                       editor_state::LineBuffer &buffer, size_t history_size,
                       const std::string &text) {
    if (text.empty())
        return false;

    BufferSnapshot before = capture_snapshot(buffer);
    prepare_for_mutating_edit(session, history_size);
    bool changed = editor_state::insert_text(buffer, text);

    if (!changed)
        return false;

    record_successful_edit(session, before, typed_insert_group_kind(text),
                           true);
    return true;
}

bool insert_pasted_text(EditorSessionState &session,
                        editor_state::LineBuffer &buffer, size_t history_size,
                        const std::string &text) {
    if (text.empty())
        return false;

    note_non_kill_command(session);
    BufferSnapshot before = capture_snapshot(buffer);
    prepare_for_mutating_edit(session, history_size);
    bool changed = editor_state::insert_text(buffer, text);

    if (!changed)
        return false;

    record_successful_edit(session, before, UndoGroupKind::InsertText, false);
    return true;
}

bool replace_range(EditorSessionState &session,
                   editor_state::LineBuffer &buffer, size_t history_size,
                   size_t replace_begin, size_t replace_end,
                   const std::string &replacement) {
    note_non_kill_command(session);
    BufferSnapshot before = capture_snapshot(buffer);
    prepare_for_mutating_edit(session, history_size);
    bool changed = editor_state::replace_range(buffer, replace_begin,
                                               replace_end, replacement);

    if (!changed)
        return false;

    record_successful_edit(session, before, UndoGroupKind::None, false);
    return true;
}

bool apply_erase(EditorSessionState &session, editor_state::LineBuffer &buffer,
                 size_t history_size, editor_state::Erase erase_action) {
    BufferSnapshot before = capture_snapshot(buffer);
    prepare_for_mutating_edit(session, history_size);
    UndoGroupKind chosen_group =
        erase_action == editor_state::Erase::BeforeCursor
            ? UndoGroupKind::Backspace
            : UndoGroupKind::Delete;
    bool changed = editor_state::apply_erase(buffer, erase_action);

    if (!changed)
        return false;

    record_successful_edit(session, before, chosen_group, true);
    return true;
}

editor_state::KillResult apply_kill(EditorSessionState &session,
                                    editor_state::LineBuffer &buffer,
                                    size_t history_size,
                                    editor_state::Kill kill_action) {
    BufferSnapshot before = capture_snapshot(buffer);
    prepare_for_buffer_edit(session, history_size);

    const editor_state::KillResult result =
        editor_state::apply_kill(buffer, kill_action);
    if (result.changed) {
        record_kill(session.kill_ring, result.killed_text, result.direction);
        UndoGroupKind chosen_group =
            kill_action == editor_state::Kill::ToLineStart ||
                    kill_action == editor_state::Kill::WordLeft
                ? UndoGroupKind::KillBackward
                : UndoGroupKind::KillForward;
        record_successful_edit(session, before, chosen_group, true);
    }

    return result;
}

bool yank_latest(EditorSessionState &session, editor_state::LineBuffer &buffer,
                 size_t history_size) {
    BufferSnapshot before = capture_snapshot(buffer);
    reset_kill_chain(session.kill_ring);

    const size_t replace_begin = buffer.cursor;
    const std::string *entry = begin_yank(session.kill_ring, replace_begin);
    if (entry == nullptr) {
        return false;
    }

    prepare_for_buffer_edit(session, history_size);
    if (!editor_state::insert_text(buffer, *entry)) {
        invalidate_yank(session.kill_ring);
        return false;
    }

    record_successful_edit(session, before, UndoGroupKind::None, false);
    return true;
}

bool yank_pop(EditorSessionState &session, editor_state::LineBuffer &buffer,
              size_t history_size) {
    BufferSnapshot before = capture_snapshot(buffer);
    reset_kill_chain(session.kill_ring);

    const size_t replace_begin = session.kill_ring.yank.replace_begin;
    const size_t replace_end = session.kill_ring.yank.replace_end;
    const std::string *entry = rotate_yank(session.kill_ring);
    if (entry == nullptr) {
        return false;
    }

    prepare_for_buffer_edit(session, history_size);
    if (!editor_state::replace_range(buffer, replace_begin, replace_end,
                                     *entry)) {
        invalidate_yank(session.kill_ring);
        return false;
    }

    record_successful_edit(session, before, UndoGroupKind::None, false);
    return true;
}

bool apply_history_navigation(EditorSessionState &session,
                              editor_state::LineBuffer &buffer,
                              editor_state::HistoryNavigation navigation,
                              const std::vector<std::string> &history) {
    BufferSnapshot before = capture_snapshot(buffer);
    note_non_kill_command(session);
    bool changed = editor_state::apply_history_navigation(
        buffer, navigation, history, session.history);
    if (!changed)
        return false;

    record_successful_edit(session, before, UndoGroupKind::None, false);
    return true;
}

void begin_completion_selection(
    EditorSessionState &session, const editor_state::LineBuffer &buffer,
    size_t replace_begin, size_t replace_end,
    std::vector<std::string> candidates,
    std::vector<features::CompletionDisplayCandidate> display_candidates) {
    session.completion = {};

    session.completion.active = true;
    session.completion.preview_active = false;
    session.completion.anchor = capture_snapshot(buffer);
    session.completion.replace_begin = replace_begin;
    session.completion.replace_end = replace_end;
    session.completion.candidates = std::move(candidates);
    session.completion.display_candidates = std::move(display_candidates);
    session.completion.selected_index = 0;
}

bool step_completion_selection(EditorSessionState &session,
                               editor_state::LineBuffer &buffer,
                               size_t history_size, bool reverse) {
    if (!session.completion.active || session.completion.candidates.empty())
        return false;
    note_non_kill_command(session);

    prepare_for_buffer_edit(session, history_size);

    if (!session.completion.preview_active) {
        session.completion.preview_active = true;
        if (reverse) {
            session.completion.selected_index =
                session.completion.candidates.size() - 1;
        }
    } else {
        if (reverse) {
            if (session.completion.selected_index == 0) {
                session.completion.selected_index =
                    session.completion.candidates.size() - 1;
            } else {
                --session.completion.selected_index;
            }
        } else {
            session.completion.selected_index++;
            session.completion.selected_index %=
                session.completion.candidates.size();
        }
    }

    return editor_state::replace_range_from_anchor(
        buffer, session.completion.anchor.text,
        session.completion.replace_begin, session.completion.replace_end,
        session.completion.candidates[session.completion.selected_index]);
}

bool cancel_completion_selection(EditorSessionState &session,
                                 editor_state::LineBuffer &buffer,
                                 size_t history_size) {
    if (!session.completion.active)
        return false;

    if (session.completion.preview_active) {
        prepare_for_buffer_edit(session, history_size);
        bool changed = restore_snapshot(buffer, session.completion.anchor);
        session.completion = {};
        return changed;
    }

    session.completion = {};
    return false;
}

bool accept_completion_selection(EditorSessionState &session,
                                 editor_state::LineBuffer &buffer,
                                 size_t history_size) {
    if (!session.completion.active) {
        return false;
    }

    close_undo_group(session.undo);

    if (!session.completion.preview_active ||
        completion_matches_anchor(session.completion, buffer)) {
        session.completion = {};
        return false;
    }

    reset_kill_chain(session.kill_ring);
    invalidate_yank(session.kill_ring);
    if (session.history.active) {
        editor_state::reset_history_browse(session.history, history_size);
    }

    invalidate_redo(session.undo);
    session.undo.undo_stack.push_back(session.completion.anchor);

    session.completion = {};
    return true;
}

bool cancel_active_preview(EditorSessionState &session,
                           editor_state::LineBuffer &buffer,
                           size_t history_size) {
    switch (active_transient_preview_kind(session)) {
    case TransientPreviewKind::Completion:
        return cancel_completion_selection(session, buffer, history_size);
    case TransientPreviewKind::None:
        break;
    }

    return false;
}

bool accept_active_preview(EditorSessionState &session,
                           editor_state::LineBuffer &buffer,
                           size_t history_size) {
    switch (active_transient_preview_kind(session)) {
    case TransientPreviewKind::Completion:
        return accept_completion_selection(session, buffer, history_size);
    case TransientPreviewKind::None:
        break;
    }

    return false;
}

bool undo(EditorSessionState &session, editor_state::LineBuffer &buffer,
          size_t history_size) {
    if (session.undo.undo_stack.empty())
        return false;

    BufferSnapshot current = capture_snapshot(buffer);
    BufferSnapshot target = session.undo.undo_stack.back();
    session.undo.undo_stack.pop_back();
    session.undo.redo_stack.push_back(current);

    reset_transient_state_for_restore(session, history_size);

    return restore_snapshot(buffer, target);
}

bool redo(EditorSessionState &session, editor_state::LineBuffer &buffer,
          size_t history_size) {
    if (session.undo.redo_stack.empty())
        return false;

    BufferSnapshot current = capture_snapshot(buffer);
    BufferSnapshot target = session.undo.redo_stack.back();
    session.undo.redo_stack.pop_back();
    session.undo.undo_stack.push_back(current);

    reset_transient_state_for_restore(session, history_size);

    return restore_snapshot(buffer, target);
}

} // namespace shell::input::session_state
