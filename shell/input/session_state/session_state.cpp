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

} // namespace

void initialize_editor_session(EditorSessionState &session,
                               size_t history_size) {
    session.history.index = history_size;
}

void note_non_kill_command(EditorSessionState &session,
                           bool should_invalidate_yank) {
    reset_kill_chain(session.kill_ring);
    if (should_invalidate_yank) {
        invalidate_yank(session.kill_ring);
    }
}

bool insert_text(EditorSessionState &session, editor_state::LineBuffer &buffer,
                 size_t history_size, const std::string &text) {
    note_non_kill_command(session);
    prepare_for_buffer_edit(session, history_size);
    return editor_state::insert_text(buffer, text);
}

bool replace_range(EditorSessionState &session,
                   editor_state::LineBuffer &buffer, size_t history_size,
                   size_t replace_begin, size_t replace_end,
                   const std::string &replacement) {
    note_non_kill_command(session);
    prepare_for_buffer_edit(session, history_size);
    return editor_state::replace_range(buffer, replace_begin, replace_end,
                                       replacement);
}

bool apply_erase(EditorSessionState &session, editor_state::LineBuffer &buffer,
                 size_t history_size, editor_state::Erase erase_action) {
    note_non_kill_command(session);
    prepare_for_buffer_edit(session, history_size);
    return editor_state::apply_erase(buffer, erase_action);
}

editor_state::KillResult apply_kill(EditorSessionState &session,
                                    editor_state::LineBuffer &buffer,
                                    size_t history_size,
                                    editor_state::Kill kill_action) {
    prepare_for_buffer_edit(session, history_size);

    const editor_state::KillResult result =
        editor_state::apply_kill(buffer, kill_action);
    if (result.changed) {
        record_kill(session.kill_ring, result.killed_text, result.direction);
    }

    return result;
}

bool yank_latest(EditorSessionState &session, editor_state::LineBuffer &buffer,
                 size_t history_size) {
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

    return true;
}

bool yank_pop(EditorSessionState &session, editor_state::LineBuffer &buffer,
              size_t history_size) {
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

    return true;
}

bool apply_history_navigation(EditorSessionState &session,
                              editor_state::LineBuffer &buffer,
                              editor_state::HistoryNavigation navigation,
                              const std::vector<std::string> &history) {
    note_non_kill_command(session);
    return editor_state::apply_history_navigation(buffer, navigation, history,
                                                  session.history);
}

void begin_completion_selection(EditorSessionState &session,
                                const editor_state::LineBuffer &buffer,
                                size_t replace_begin, size_t replace_end,
                                std::vector<std::string> candidates) {
    session.completion = {};

    session.completion = {
        true,          false,       buffer.text,           buffer.cursor,
        replace_begin, replace_end, std::move(candidates), 0};
}

bool cycle_completion_selection(EditorSessionState &session,
                                editor_state::LineBuffer &buffer,
                                size_t history_size) {
    if (!session.completion.active || session.completion.candidates.empty())
        return false;
    note_non_kill_command(session);

    prepare_for_buffer_edit(session, history_size);

    if (!session.completion.preview_active) {
        session.completion.preview_active = true;
    } else {
        session.completion.selected_index++;
        session.completion.selected_index %=
            session.completion.candidates.size();
    }

    return editor_state::replace_range_from_anchor(
        buffer, session.completion.anchor_text,
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
        bool changed =
            editor_state::restore_buffer(buffer, session.completion.anchor_text,
                                         session.completion.anchor_cursor);
        session.completion = {};
        return changed;
    }

    session.completion = {};
    return false;
}

void confirm_completion_selection(EditorSessionState &session) {
    session.completion = {};
}

} // namespace shell::input::session_state
