#include "input.hpp"

#include <cstdio>
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

#include "../../features/completion/completion.hpp"
#include "../prompt/prompt.hpp"
#include "../shell.hpp"
#include "../signals/signals.hpp"
#include "../terminal/terminal.hpp"
#include "./editor_state/editor_state.hpp"
#include "./key/key.hpp"

namespace shell::input {
namespace {

enum class KeyHandlingResult {
    ContinueLoop,
    FinishInput,
    Ignore,
};

struct InputSession {
    struct termios saved_state_{};
    bool active = false;

    ~InputSession() {
        if (!active) {
            return;
        }

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_state_) == -1) {
            perror("tcsetattr");
        }
        active = false;
    }
};

bool begin_input_session(InputSession &session) {
    session.active = false;

    if (tcgetattr(STDIN_FILENO, &session.saved_state_) == -1) {
        perror("tcgetattr");
        return false;
    }

    struct termios raw = session.saved_state_;
    raw.c_lflag &= ~ICANON;
    raw.c_lflag &= ~ECHO;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        return false;
    }

    session.active = true;
    return true;
}

void redraw_buffer(const shell::ShellState &state,
                   shell::prompt::InputRenderState &render_state,
                   const editor_state::LineBuffer &buffer,
                   bool full_prompt = false) {
    shell::prompt::redraw_input_line(render_state, state, buffer.text,
                                     buffer.cursor, full_prompt);
}

void print_completion_candidates(const std::vector<std::string> &candidates) {
    shell::terminal::write_stdout_line("");
    for (const std::string &candidate : candidates) {
        shell::terminal::write_stdout(candidate + "  ");
    }
    shell::terminal::write_stdout_line("");
}

std::string normalize_paste_for_single_line(const std::string &text) {
    std::string normalized;
    normalized.reserve(text.size());

    for (unsigned char ch : text) {
        switch (ch) {
        case '\r':
        case '\n':
        case '\t':
        case '\v':
        case '\f':
            normalized.push_back(' ');
            break;
        default:
            if (ch >= 32U && ch != 127U) {
                normalized.push_back(static_cast<char>(ch));
            }
            break;
        }
    }

    return normalized;
}

void redraw_if_changed(const shell::ShellState &state,
                       shell::prompt::InputRenderState &render_state,
                       const editor_state::LineBuffer &buffer, bool changed,
                       bool full_prompt = false) {
    if (!changed) {
        return;
    }

    redraw_buffer(state, render_state, buffer, full_prompt);
}

void apply_movement_and_redraw(const shell::ShellState &state,
                               shell::prompt::InputRenderState &render_state,
                               editor_state::LineBuffer &buffer,
                               editor_state::Movement movement) {
    redraw_if_changed(state, render_state, buffer,
                      editor_state::apply_movement(buffer, movement));
}

void apply_erase_and_redraw(const shell::ShellState &state,
                            shell::prompt::InputRenderState &render_state,
                            editor_state::LineBuffer &buffer,
                            editor_state::HistoryBrowseState &history_state,
                            size_t history_size,
                            editor_state::Erase erase_action) {
    redraw_if_changed(
        state, render_state, buffer,
        editor_state::apply_erase(buffer, erase_action, history_state,
                                  history_size));
}

void apply_kill_and_redraw(const shell::ShellState &state,
                           shell::prompt::InputRenderState &render_state,
                           editor_state::LineBuffer &buffer,
                           editor_state::HistoryBrowseState &history_state,
                           size_t history_size,
                           editor_state::Kill kill_action) {
    redraw_if_changed(
        state, render_state, buffer,
        editor_state::apply_kill(buffer, kill_action, history_state,
                                 history_size));
}

void yank_kill_buffer_and_redraw(
    const shell::ShellState &state,
    shell::prompt::InputRenderState &render_state,
    editor_state::LineBuffer &buffer,
    editor_state::HistoryBrowseState &history_state, size_t history_size) {
    redraw_if_changed(state, render_state, buffer,
                      editor_state::yank_kill_buffer(buffer, history_state,
                                                     history_size));
}

void apply_history_navigation_and_redraw(
    const shell::ShellState &state,
    shell::prompt::InputRenderState &render_state,
    editor_state::LineBuffer &buffer,
    editor_state::HistoryBrowseState &history_state,
    editor_state::HistoryNavigation navigation) {
    redraw_if_changed(
        state, render_state, buffer,
        editor_state::apply_history_navigation(buffer, navigation, state.history,
                                               history_state));
}

void replace_range_and_redraw(const shell::ShellState &state,
                              shell::prompt::InputRenderState &render_state,
                              editor_state::LineBuffer &buffer,
                              editor_state::HistoryBrowseState &history_state,
                              size_t history_size, size_t replace_begin,
                              size_t replace_end,
                              const std::string &replacement) {
    redraw_if_changed(state, render_state, buffer,
                      editor_state::replace_range(
                          buffer, replace_begin, replace_end, replacement,
                          history_state, history_size));
}

void insert_input_text(const shell::ShellState &state,
                       shell::prompt::InputRenderState &render_state,
                       editor_state::LineBuffer &buffer,
                       editor_state::HistoryBrowseState &history_state,
                       size_t history_size, const std::string &text) {
    const bool changed =
        editor_state::insert_text(buffer, text, history_state, history_size);
    redraw_if_changed(state, render_state, buffer, changed);
}

void handle_tab_completion(const shell::ShellState &state,
                           shell::prompt::InputRenderState &render_state,
                           editor_state::LineBuffer &buffer,
                           editor_state::HistoryBrowseState &history_state,
                           size_t history_size) {
    const features::CompletionResult completion =
        features::complete_at_cursor(state, buffer.text, buffer.cursor);

    switch (completion.action) {
    case features::CompletionAction::None:
        return;
    case features::CompletionAction::ReplaceToken:
        replace_range_and_redraw(state, render_state, buffer, history_state,
                                 history_size, completion.replace_begin,
                                 completion.replace_end,
                                 completion.replacement);
        return;
    case features::CompletionAction::ShowCandidates:
        print_completion_candidates(completion.candidates);
        redraw_buffer(state, render_state, buffer, true);
        return;
    }
}

KeyHandlingResult
handle_character_key(const shell::ShellState &state,
                     shell::prompt::InputRenderState &render_state,
                     editor_state::LineBuffer &buffer,
                     editor_state::HistoryBrowseState &history_state,
                     size_t history_size, const key::InputEvent &event,
                     InputResult &result) {
    if (event.key != key::EditorKey::Character) {
        return KeyHandlingResult::Ignore;
    }

    const char binding = event.key_character;
    if (key::has_modifier(event, key::KeyModCtrl)) {
        switch (binding) {
        case 'a':
            apply_movement_and_redraw(state, render_state, buffer,
                                      editor_state::Movement::Home);
            return KeyHandlingResult::ContinueLoop;
        case 'd':
            if (buffer.text.empty()) {
                result.eof = true;
                return KeyHandlingResult::FinishInput;
            }

            apply_erase_and_redraw(state, render_state, buffer, history_state,
                                   history_size, editor_state::Erase::AtCursor);
            return KeyHandlingResult::ContinueLoop;
        case 'e':
            apply_movement_and_redraw(state, render_state, buffer,
                                      editor_state::Movement::End);
            return KeyHandlingResult::ContinueLoop;
        case 'k':
            apply_kill_and_redraw(state, render_state, buffer, history_state,
                                  history_size, editor_state::Kill::ToLineEnd);
            return KeyHandlingResult::ContinueLoop;
        case 'l':
            shell::terminal::write_stdout("\033[2J\033[H");
            redraw_buffer(state, render_state, buffer, true);
            return KeyHandlingResult::ContinueLoop;
        case 'u':
            apply_kill_and_redraw(state, render_state, buffer, history_state,
                                  history_size,
                                  editor_state::Kill::ToLineStart);
            return KeyHandlingResult::ContinueLoop;
        case 'w':
            apply_kill_and_redraw(state, render_state, buffer, history_state,
                                  history_size, editor_state::Kill::WordLeft);
            return KeyHandlingResult::ContinueLoop;
        case 'y':
            yank_kill_buffer_and_redraw(state, render_state, buffer,
                                        history_state, history_size);
            return KeyHandlingResult::ContinueLoop;
        default:
            break;
        }
    }

    if (key::has_modifier(event, key::KeyModAlt)) {
        switch (binding) {
        case 'b':
            apply_movement_and_redraw(state, render_state, buffer,
                                      editor_state::Movement::WordLeft);
            return KeyHandlingResult::ContinueLoop;
        case 'f':
            apply_movement_and_redraw(state, render_state, buffer,
                                      editor_state::Movement::WordRight);
            return KeyHandlingResult::ContinueLoop;
        case 'd':
            apply_kill_and_redraw(state, render_state, buffer, history_state,
                                  history_size, editor_state::Kill::WordRight);
            return KeyHandlingResult::ContinueLoop;
        default:
            break;
        }
    }

    return KeyHandlingResult::Ignore;
}

KeyHandlingResult
handle_special_key(const shell::ShellState &state,
                   shell::prompt::InputRenderState &render_state,
                   editor_state::LineBuffer &buffer,
                   editor_state::HistoryBrowseState &history_state,
                   size_t history_size, const key::InputEvent &event) {
    switch (event.key) {
    case key::EditorKey::Character:
        return KeyHandlingResult::Ignore;
    case key::EditorKey::Escape:
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::Enter:
        shell::terminal::write_stdout_line("");
        return KeyHandlingResult::FinishInput;
    case key::EditorKey::Tab:
        handle_tab_completion(state, render_state, buffer, history_state,
                              history_size);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::Backspace:
        apply_erase_and_redraw(state, render_state, buffer, history_state,
                               history_size, editor_state::Erase::BeforeCursor);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::Delete:
        apply_erase_and_redraw(state, render_state, buffer, history_state,
                               history_size, editor_state::Erase::AtCursor);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::ArrowUp:
        apply_history_navigation_and_redraw(
            state, render_state, buffer, history_state,
            editor_state::HistoryNavigation::Up);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::ArrowDown:
        apply_history_navigation_and_redraw(
            state, render_state, buffer, history_state,
            editor_state::HistoryNavigation::Down);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::ArrowRight:
        apply_movement_and_redraw(
            state, render_state, buffer,
            key::has_modifier(event, key::KeyModCtrl)
                ? editor_state::Movement::WordRight
                : editor_state::Movement::Right);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::ArrowLeft:
        apply_movement_and_redraw(
            state, render_state, buffer,
            key::has_modifier(event, key::KeyModCtrl)
                ? editor_state::Movement::WordLeft
                : editor_state::Movement::Left);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::Home:
        apply_movement_and_redraw(state, render_state, buffer,
                                  editor_state::Movement::Home);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::End:
        apply_movement_and_redraw(state, render_state, buffer,
                                  editor_state::Movement::End);
        return KeyHandlingResult::ContinueLoop;
    }

    return KeyHandlingResult::Ignore;
}

KeyHandlingResult
handle_key_event(const shell::ShellState &state,
                 shell::prompt::InputRenderState &render_state,
                 editor_state::LineBuffer &buffer,
                 editor_state::HistoryBrowseState &history_state,
                 size_t history_size, const key::InputEvent &event,
                 InputResult &result) {
    const KeyHandlingResult character_result = handle_character_key(
        state, render_state, buffer, history_state, history_size, event,
        result);
    if (character_result != KeyHandlingResult::Ignore) {
        return character_result;
    }

    return handle_special_key(state, render_state, buffer, history_state,
                              history_size, event);
}

InputResult read_interactive_fallback_command_line() {
    InputResult result{};

    while (true) {
        if (std::getline(std::cin, result.line)) {
            return result;
        }

        if (shell::signals::g_input_interrupted != 0) {
            shell::signals::g_input_interrupted = 0;
            std::cin.clear();
            result.interrupted = true;
            return result;
        }

        if (shell::signals::g_resize_pending != 0) {
            shell::signals::g_resize_pending = 0;
            std::cin.clear();
            continue;
        }

        if (std::cin.eof()) {
            result.eof = true;
            return result;
        }

        std::cin.clear();
        result.eof = true;
        return result;
    }
}

} // namespace

InputResult read_non_interactive_command_line() {
    InputResult result{};

    if (!std::getline(std::cin, result.line)) {
        result.eof = true;
    }

    return result;
}

InputResult read_command_line(shell::ShellState &state,
                              shell::prompt::InputRenderState &render_state) {
    if (!state.interactive) {
        return read_non_interactive_command_line();
    }

    InputResult result{};
    editor_state::LineBuffer buffer{};
    editor_state::HistoryBrowseState history_state{};
    const size_t history_size = state.history.size();
    history_state.index = history_size;

    InputSession input_session{};
    if (!begin_input_session(input_session)) {
        return read_interactive_fallback_command_line();
    }

    while (true) {
        const key::InputEvent event = key::read_event();

        if (event.kind == key::InputEventKind::ReadEof) {
            result.eof = true;
            break;
        }

        if (event.kind == key::InputEventKind::Interrupted) {
            shell::prompt::finalize_interrupted_input_line(render_state);
            result.interrupted = true;
            break;
        }

        switch (event.kind) {
        case key::InputEventKind::TextInput:
            insert_input_text(state, render_state, buffer, history_state,
                              history_size, event.text);
            continue;
        case key::InputEventKind::Paste:
            insert_input_text(state, render_state, buffer, history_state,
                              history_size,
                              normalize_paste_for_single_line(event.text));
            continue;
        case key::InputEventKind::Key: {
            switch (handle_key_event(state, render_state, buffer, history_state,
                                     history_size, event, result)) {
            case KeyHandlingResult::ContinueLoop:
            case KeyHandlingResult::Ignore:
                continue;
            case KeyHandlingResult::FinishInput:
                break;
            }
            break;
        }
        case key::InputEventKind::Resized:
            redraw_buffer(state, render_state, buffer);
            continue;
        case key::InputEventKind::Ignored:
            continue;
        default:
            break;
        }

        break;
    }

    result.line = buffer.text;
    return result;
}

} // namespace shell::input
