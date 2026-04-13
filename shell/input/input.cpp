#include "input.hpp"

#include <cstdio>
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>

#include "../../features/completion/completion.hpp"
#include "../prompt/prompt.hpp"
#include "../shell.hpp"
#include "./editor_state/editor_state.hpp"
#include "./key/key.hpp"

namespace shell::input {
namespace {

// helper funckije za manual handling stvari (enable/disable jer zelimo da drugi
// programi budu normalni)
bool enable_input_mode(struct termios &old_state) {
    if (tcgetattr(STDIN_FILENO, &old_state) == -1) {
        perror("tcgetattr");
        return false;
    }

    struct termios raw = old_state;

    raw.c_lflag &= ~ICANON;
    raw.c_lflag &= ~ECHO;

    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        return false;
    }

    return true;
}

void restore_input_mode(const struct termios &old_state) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_state) == -1) {
        perror("tcsetattr");
    }
}

void redraw_buffer(const shell::ShellState &state,
                   const editor_state::LineBuffer &buffer,
                   bool full_prompt = false) {
    shell::prompt::redraw_input_line(state, buffer.text, buffer.cursor,
                                     full_prompt);
}

} // namespace

InputResult read_non_interactive_command_line() {
    InputResult result{};

    if (!std::getline(std::cin, result.line)) {
        result.eof = true;
    }

    return result;
}

InputResult read_command_line(shell::ShellState &state) {
    if (!state.interactive) {
        return read_non_interactive_command_line();
    }

    InputResult result{};
    editor_state::LineBuffer buffer{};
    editor_state::HistoryBrowseState history_state{};
    history_state.index = state.history.size();

    struct termios old_state;
    const bool input_mode_enabled = enable_input_mode(old_state);

    while (true) {
        const key::KeyPress key_press = key::read_key();

        if (key_press.kind == key::KeyKind::ReadEof) {
            result.eof = true;
            break;
        }

        if (key_press.kind == key::KeyKind::Interrupted) {
            shell::prompt::finalize_interrupted_input_line();
            result.interrupted = true;
            break;
        }

        switch (key_press.kind) {
        case key::KeyKind::Enter:
            write(STDOUT_FILENO, "\n", 1);
            break;
        case key::KeyKind::CtrlD:
            if (buffer.text.empty()) {
                result.eof = true;
                break;
            }

            if (editor_state::erase_at_cursor(buffer, history_state,
                                              state.history.size())) {
                redraw_buffer(state, buffer);
            }
            continue;
        case key::KeyKind::ArrowUp:
            if (editor_state::browse_history_up(buffer, state.history,
                                                history_state)) {
                redraw_buffer(state, buffer);
            }
            continue;
        case key::KeyKind::ArrowDown:
            if (editor_state::browse_history_down(buffer, state.history,
                                                  history_state)) {
                redraw_buffer(state, buffer);
            }
            continue;
        case key::KeyKind::ArrowRight: {
            bool moved = key::has_modifier(key_press, key::KeyModCtrl)
                             ? editor_state::move_cursor_word_right(buffer)
                             : editor_state::move_cursor_right(buffer);
            if (moved) {
                redraw_buffer(state, buffer);
            }
            continue;
        }
        case key::KeyKind::ArrowLeft: {
            bool moved = key::has_modifier(key_press, key::KeyModCtrl)
                             ? editor_state::move_cursor_word_left(buffer)
                             : editor_state::move_cursor_left(buffer);
            if (moved) {
                redraw_buffer(state, buffer);
            }
            continue;
        }
        case key::KeyKind::Home:
        case key::KeyKind::Delete:
            if (key_press.kind == key::KeyKind::Home) {
                if (editor_state::move_cursor_home(buffer)) {
                    redraw_buffer(state, buffer);
                }
                continue;
            }

            if (editor_state::erase_at_cursor(buffer, history_state,
                                              state.history.size())) {
                redraw_buffer(state, buffer);
            }
            continue;
        case key::KeyKind::CtrlA:
            if (editor_state::move_cursor_home(buffer)) {
                redraw_buffer(state, buffer);
            }
            continue;
        case key::KeyKind::End:
        case key::KeyKind::CtrlE:
            if (editor_state::move_cursor_end(buffer)) {
                redraw_buffer(state, buffer);
            }
            continue;
        case key::KeyKind::CtrlL: {
            const char *clear = "\033[2J\033[H";
            write(STDOUT_FILENO, clear, 7);
            redraw_buffer(state, buffer, true);
            continue;
        }
        case key::KeyKind::Tab:
            features::handle_tab_completion(state, buffer.text, buffer.cursor);
            continue;
        case key::KeyKind::Backspace:
            if (editor_state::erase_before_cursor(buffer, history_state,
                                                  state.history.size())) {
                redraw_buffer(state, buffer);
            }
            continue;
        case key::KeyKind::Character:
            editor_state::insert_character(buffer, key_press.character,
                                           history_state, state.history.size());
            redraw_buffer(state, buffer);
            continue;
        case key::KeyKind::ReadEof:
        case key::KeyKind::Interrupted:
            break;
        case key::KeyKind::Ignored:
            continue;
        }

        break;
    }

    if (input_mode_enabled) {
        restore_input_mode(old_state);
    }
    result.line = buffer.text;
    return result;
}

} // namespace shell::input
