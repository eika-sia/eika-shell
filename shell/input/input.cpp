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

void handle_tab_completion(const shell::ShellState &state,
                           shell::prompt::InputRenderState &render_state,
                           editor_state::LineBuffer &buffer) {
    const features::CompletionResult completion =
        features::complete_at_cursor(state, buffer.text, buffer.cursor);

    switch (completion.action) {
    case features::CompletionAction::None:
        return;
    case features::CompletionAction::ReplaceToken:
        buffer.text.replace(completion.replace_begin,
                            completion.replace_end - completion.replace_begin,
                            completion.replacement);
        buffer.cursor =
            completion.replace_begin + completion.replacement.size();
        redraw_buffer(state, render_state, buffer);
        return;
    case features::CompletionAction::ShowCandidates:
        print_completion_candidates(completion.candidates);
        redraw_buffer(state, render_state, buffer, true);
        return;
    }
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
    history_state.index = state.history.size();

    InputSession input_session{};
    if (!begin_input_session(input_session)) {
        return read_interactive_fallback_command_line();
    }

    while (true) {
        const key::KeyPress key_press = key::read_key();

        if (key_press.kind == key::KeyKind::ReadEof) {
            result.eof = true;
            break;
        }

        if (key_press.kind == key::KeyKind::Interrupted) {
            shell::prompt::finalize_interrupted_input_line(render_state);
            result.interrupted = true;
            break;
        }

        switch (key_press.kind) {
        case key::KeyKind::Enter:
            shell::terminal::write_stdout_line("");
            break;
        case key::KeyKind::CtrlD:
            if (buffer.text.empty()) {
                result.eof = true;
                break;
            }

            if (editor_state::erase_at_cursor(buffer, history_state,
                                              state.history.size())) {
                redraw_buffer(state, render_state, buffer);
            }
            continue;
        case key::KeyKind::ArrowUp:
            if (editor_state::browse_history_up(buffer, state.history,
                                                history_state)) {
                redraw_buffer(state, render_state, buffer);
            }
            continue;
        case key::KeyKind::ArrowDown:
            if (editor_state::browse_history_down(buffer, state.history,
                                                  history_state)) {
                redraw_buffer(state, render_state, buffer);
            }
            continue;
        case key::KeyKind::ArrowRight: {
            bool moved = key::has_modifier(key_press, key::KeyModCtrl)
                             ? editor_state::move_cursor_word_right(buffer)
                             : editor_state::move_cursor_right(buffer);
            if (moved) {
                redraw_buffer(state, render_state, buffer);
            }
            continue;
        }
        case key::KeyKind::ArrowLeft: {
            bool moved = key::has_modifier(key_press, key::KeyModCtrl)
                             ? editor_state::move_cursor_word_left(buffer)
                             : editor_state::move_cursor_left(buffer);
            if (moved) {
                redraw_buffer(state, render_state, buffer);
            }
            continue;
        }
        case key::KeyKind::Home:
        case key::KeyKind::Delete:
            if (key_press.kind == key::KeyKind::Home) {
                if (editor_state::move_cursor_home(buffer)) {
                    redraw_buffer(state, render_state, buffer);
                }
                continue;
            }

            if (editor_state::erase_at_cursor(buffer, history_state,
                                              state.history.size())) {
                redraw_buffer(state, render_state, buffer);
            }
            continue;
        case key::KeyKind::CtrlA:
            if (editor_state::move_cursor_home(buffer)) {
                redraw_buffer(state, render_state, buffer);
            }
            continue;
        case key::KeyKind::End:
        case key::KeyKind::CtrlE:
            if (editor_state::move_cursor_end(buffer)) {
                redraw_buffer(state, render_state, buffer);
            }
            continue;
        case key::KeyKind::CtrlL: {
            shell::terminal::write_stdout("\033[2J\033[H");
            redraw_buffer(state, render_state, buffer, true);
            continue;
        }
        case key::KeyKind::Tab:
            handle_tab_completion(state, render_state, buffer);
            continue;
        case key::KeyKind::Backspace:
            if (editor_state::erase_before_cursor(buffer, history_state,
                                                  state.history.size())) {
                redraw_buffer(state, render_state, buffer);
            }
            continue;
        case key::KeyKind::Character:
            editor_state::insert_character(buffer, key_press.character,
                                           history_state, state.history.size());
            redraw_buffer(state, render_state, buffer);
            continue;
        case key::KeyKind::Resized:
            redraw_buffer(state, render_state, buffer);
            continue;
        case key::KeyKind::ReadEof:
        case key::KeyKind::Interrupted:
            break;
        case key::KeyKind::Ignored:
            continue;
        }

        break;
    }

    result.line = buffer.text;
    return result;
}

} // namespace shell::input
