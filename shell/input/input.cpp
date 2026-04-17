#include "input.hpp"

#include <cstdio>
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

#include "../../features/completion/completion.hpp"
#include "../../features/completion/path_completion.hpp"
#include "../prompt/prompt.hpp"
#include "../shell.hpp"
#include "../signals/signals.hpp"
#include "../terminal/terminal.hpp"
#include "./editor_state/editor_state.hpp"
#include "./key/key.hpp"
#include "./session_state/session_state.hpp"

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

struct InputContext {
    const shell::ShellState &state;
    shell::prompt::InputRenderState &render_state;
    editor_state::LineBuffer &buffer;
    session_state::EditorSessionState &session;
    size_t history_size = 0;
    InputResult &result;
};

bool begin_input_session(InputSession &session) {
    session.active = false;

    if (tcgetattr(STDIN_FILENO, &session.saved_state_) == -1) {
        perror("tcgetattr");
        return false;
    }

    key::set_backspace_byte(
        static_cast<unsigned char>(session.saved_state_.c_cc[VERASE]));

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

void redraw_buffer(const InputContext &context, bool full_prompt = false) {
    shell::prompt::redraw_input_line(context.render_state, context.state,
                                     context.buffer.text, context.buffer.cursor,
                                     full_prompt);
}

std::string format_completion_candidate(const std::string &candidate) {
    if (candidate.empty()) {
        return candidate;
    }

    if (candidate.rfind("./", 0) == 0 || candidate.rfind("../", 0) == 0) {
        return candidate;
    }

    const size_t pos = candidate.find_last_of('/');
    if (pos == 0 || pos == std::string::npos) {
        return candidate;
    }

    if (pos == candidate.size() - 1) {
        return ".../" +
               features::get_basename_part(
                   candidate.substr(0, candidate.size() - 1)) +
               "/";
    }

    return ".../" + features::get_basename_part(candidate);
}

void print_completion_candidates(const std::vector<std::string> &candidates) {
    shell::terminal::write_stdout_line("");
    for (const std::string &candidate : candidates) {
        shell::terminal::write_stdout(format_completion_candidate(candidate) +
                                      "  ");
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

void redraw_if_changed(const InputContext &context, bool changed,
                       bool full_prompt = false) {
    if (!changed) {
        return;
    }

    redraw_buffer(context, full_prompt);
}

void apply_movement_and_redraw(InputContext &context,
                               editor_state::Movement movement) {
    redraw_if_changed(context,
                      editor_state::apply_movement(context.buffer, movement));
}

void apply_erase_and_redraw(InputContext &context,
                            editor_state::Erase erase_action) {
    redraw_if_changed(context, session_state::apply_erase(
                                   context.session, context.buffer,
                                   context.history_size, erase_action));
}

void apply_kill_and_redraw(InputContext &context,
                           editor_state::Kill kill_action) {
    redraw_if_changed(
        context, session_state::apply_kill(context.session, context.buffer,
                                           context.history_size, kill_action)
                     .changed);
}

void yank_kill_buffer_and_redraw(InputContext &context) {
    redraw_if_changed(
        context, session_state::yank_latest(context.session, context.buffer,
                                            context.history_size));
}

void yank_pop_and_redraw(InputContext &context) {
    redraw_if_changed(context,
                      session_state::yank_pop(context.session, context.buffer,
                                              context.history_size));
}

void apply_history_navigation_and_redraw(
    InputContext &context, editor_state::HistoryNavigation navigation) {
    redraw_if_changed(context, session_state::apply_history_navigation(
                                   context.session, context.buffer, navigation,
                                   context.state.history));
}

void replace_range_and_redraw(InputContext &context, size_t replace_begin,
                              size_t replace_end,
                              const std::string &replacement) {
    redraw_if_changed(context,
                      session_state::replace_range(
                          context.session, context.buffer, context.history_size,
                          replace_begin, replace_end, replacement));
}

void insert_input_text(InputContext &context, const std::string &text) {
    redraw_if_changed(
        context, session_state::insert_text(context.session, context.buffer,
                                            context.history_size, text));
}

void handle_tab_completion(InputContext &context) {
    const features::CompletionResult completion = features::complete_at_cursor(
        context.state, context.buffer.text, context.buffer.cursor);

    switch (completion.action) {
    case features::CompletionAction::None:
        session_state::note_non_kill_command(context.session);
        return;
    case features::CompletionAction::ReplaceToken:
        replace_range_and_redraw(context, completion.replace_begin,
                                 completion.replace_end,
                                 completion.replacement);
        return;
    case features::CompletionAction::ShowCandidates:
        session_state::note_non_kill_command(context.session);
        print_completion_candidates(completion.candidates);
        redraw_buffer(context, true);
        return;
    }
}

KeyHandlingResult handle_character_key(InputContext &context,
                                       const key::InputEvent &event) {
    if (event.key != key::EditorKey::Character) {
        return KeyHandlingResult::Ignore;
    }

    const char binding = event.key_character;
    if (key::has_modifier(event, key::KeyModCtrl)) {
        switch (binding) {
        case 'a':
            session_state::note_non_kill_command(context.session);
            apply_movement_and_redraw(context, editor_state::Movement::Home);
            return KeyHandlingResult::ContinueLoop;
        case 'd':
            if (context.buffer.text.empty()) {
                session_state::note_non_kill_command(context.session);
                context.result.eof = true;
                return KeyHandlingResult::FinishInput;
            }

            apply_erase_and_redraw(context, editor_state::Erase::AtCursor);
            return KeyHandlingResult::ContinueLoop;
        case 'e':
            session_state::note_non_kill_command(context.session);
            apply_movement_and_redraw(context, editor_state::Movement::End);
            return KeyHandlingResult::ContinueLoop;
        case 'k':
            apply_kill_and_redraw(context, editor_state::Kill::ToLineEnd);
            return KeyHandlingResult::ContinueLoop;
        case 'l':
            session_state::note_non_kill_command(context.session);
            shell::terminal::write_stdout("\033[2J\033[H");
            redraw_buffer(context, true);
            return KeyHandlingResult::ContinueLoop;
        case 'u':
            apply_kill_and_redraw(context, editor_state::Kill::ToLineStart);
            return KeyHandlingResult::ContinueLoop;
        case 'w':
            apply_kill_and_redraw(context, editor_state::Kill::WordLeft);
            return KeyHandlingResult::ContinueLoop;
        case 'y':
            yank_kill_buffer_and_redraw(context);
            return KeyHandlingResult::ContinueLoop;
        case 'b':
            session_state::note_non_kill_command(context.session);
            apply_movement_and_redraw(context, editor_state::Movement::Left);
            return KeyHandlingResult::ContinueLoop;
        case 'f':
            session_state::note_non_kill_command(context.session);
            apply_movement_and_redraw(context, editor_state::Movement::Right);
            return KeyHandlingResult::ContinueLoop;
        case 'p':
            apply_history_navigation_and_redraw(
                context, editor_state::HistoryNavigation::Up);
            return KeyHandlingResult::ContinueLoop;
        case 'n':
            apply_history_navigation_and_redraw(
                context, editor_state::HistoryNavigation::Down);
            return KeyHandlingResult::ContinueLoop;
        default:
            break;
        }
    }

    if (key::has_modifier(event, key::KeyModAlt)) {
        switch (binding) {
        case 'b':
            session_state::note_non_kill_command(context.session);
            apply_movement_and_redraw(context,
                                      editor_state::Movement::WordLeft);
            return KeyHandlingResult::ContinueLoop;
        case 'f':
            session_state::note_non_kill_command(context.session);
            apply_movement_and_redraw(context,
                                      editor_state::Movement::WordRight);
            return KeyHandlingResult::ContinueLoop;
        case 'd':
            apply_kill_and_redraw(context, editor_state::Kill::WordRight);
            return KeyHandlingResult::ContinueLoop;
        case 'y':
            yank_pop_and_redraw(context);
            return KeyHandlingResult::ContinueLoop;
        default:
            break;
        }
    }

    return KeyHandlingResult::Ignore;
}

KeyHandlingResult handle_special_key(InputContext &context,
                                     const key::InputEvent &event) {
    switch (event.key) {
    case key::EditorKey::Character:
        return KeyHandlingResult::Ignore;
    case key::EditorKey::Escape:
        session_state::note_non_kill_command(context.session);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::Enter:
        session_state::note_non_kill_command(context.session);
        shell::terminal::write_stdout_line("");
        return KeyHandlingResult::FinishInput;
    case key::EditorKey::Tab:
        handle_tab_completion(context);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::Backspace:
        if (key::has_modifier(event, key::KeyModAlt) ||
            key::has_modifier(event, key::KeyModCtrl)) {
            apply_kill_and_redraw(context, editor_state::Kill::WordLeft);
            return KeyHandlingResult::ContinueLoop;
        }

        apply_erase_and_redraw(context, editor_state::Erase::BeforeCursor);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::Delete:
        apply_erase_and_redraw(context, editor_state::Erase::AtCursor);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::ArrowUp:
        apply_history_navigation_and_redraw(
            context, editor_state::HistoryNavigation::Up);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::ArrowDown:
        apply_history_navigation_and_redraw(
            context, editor_state::HistoryNavigation::Down);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::ArrowRight:
        session_state::note_non_kill_command(context.session);
        apply_movement_and_redraw(context,
                                  key::has_modifier(event, key::KeyModCtrl)
                                      ? editor_state::Movement::WordRight
                                      : editor_state::Movement::Right);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::ArrowLeft:
        session_state::note_non_kill_command(context.session);
        apply_movement_and_redraw(context,
                                  key::has_modifier(event, key::KeyModCtrl)
                                      ? editor_state::Movement::WordLeft
                                      : editor_state::Movement::Left);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::Home:
        session_state::note_non_kill_command(context.session);
        apply_movement_and_redraw(context, editor_state::Movement::Home);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::End:
        session_state::note_non_kill_command(context.session);
        apply_movement_and_redraw(context, editor_state::Movement::End);
        return KeyHandlingResult::ContinueLoop;
    }

    return KeyHandlingResult::Ignore;
}

KeyHandlingResult handle_key_event(InputContext &context,
                                   const key::InputEvent &event) {
    const KeyHandlingResult character_result =
        handle_character_key(context, event);
    if (character_result != KeyHandlingResult::Ignore) {
        return character_result;
    }

    return handle_special_key(context, event);
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
    session_state::EditorSessionState session{};
    const size_t history_size = state.history.size();
    session_state::initialize_editor_session(session, history_size);
    InputContext context{state,   render_state, buffer,
                         session, history_size, result};

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
            insert_input_text(context, event.text);
            continue;
        case key::InputEventKind::Paste:
            insert_input_text(context,
                              normalize_paste_for_single_line(event.text));
            continue;
        case key::InputEventKind::Key: {
            switch (handle_key_event(context, event)) {
            case KeyHandlingResult::ContinueLoop:
            case KeyHandlingResult::Ignore:
                continue;
            case KeyHandlingResult::FinishInput:
                break;
            }
            break;
        }
        case key::InputEventKind::Resized:
            redraw_buffer(context);
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
