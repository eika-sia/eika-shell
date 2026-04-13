#include "input.hpp"

#include <cerrno>
#include <cstdio>
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>

#include "../../features/completion/completion.hpp"
#include "./editor_state/editor_state.hpp"
#include "../prompt/prompt.hpp"
#include "../shell.hpp"

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

// \033 escape sequence parsing (za strelice je lagano samo pomoicemo cursor
// (isto esc seq) lijevo desno)
void handle_escape_sequence(
    const shell::ShellState &state, editor_state::LineBuffer &buffer,
    editor_state::HistoryBrowseState &history_state) {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) <= 0)
        return;
    if (read(STDIN_FILENO, &seq[1], 1) <= 0)
        return;

    if (seq[0] != '[') {
        return;
    }

    switch (seq[1]) {
    case 'A': { // Up
        if (editor_state::browse_history_up(buffer, state.history,
                                            history_state)) {
            shell::prompt::redraw_input_line(state, buffer.text, buffer.cursor,
                                             false);
        }
        break;
    }

    case 'B': { // Down
        if (editor_state::browse_history_down(buffer, state.history,
                                              history_state)) {
            shell::prompt::redraw_input_line(state, buffer.text, buffer.cursor,
                                             false);
        }
        break;
    }

    case 'C': { // Right
        if (editor_state::move_cursor_right(buffer)) {
            shell::prompt::redraw_input_line(state, buffer.text, buffer.cursor,
                                             false);
        }
        break;
    }

    case 'D': { // Left
        if (editor_state::move_cursor_left(buffer)) {
            shell::prompt::redraw_input_line(state, buffer.text, buffer.cursor,
                                             false);
        }
        break;
    }
    default:
        break;
    }

    if (seq[1] == '3') {
        if (read(STDIN_FILENO, &seq[2], 1) <= 0)
            return;

        if (seq[2] == '~') {
            if (editor_state::erase_at_cursor(buffer, history_state,
                                              state.history.size())) {
                shell::prompt::redraw_input_line(state, buffer.text,
                                                 buffer.cursor, false);
            }
        }
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

InputResult read_command_line(shell::ShellState &state) {
    if (!state.interactive) {
        return read_non_interactive_command_line();
    }

    InputResult result{};
    editor_state::LineBuffer buffer{};
    editor_state::HistoryBrowseState history_state{};
    history_state.index = state.history.size();

    char ch = '\0';
    struct termios old_state;
    const bool input_mode_enabled = enable_input_mode(old_state);

    while (true) {
        ssize_t n = read(STDIN_FILENO, &ch, 1);

        if (n == 0) {
            result.eof = true;
            break;
        }

        if (n < 0) {
            if (errno == EINTR) {
                shell::prompt::finalize_interrupted_input_line();
                result.interrupted = true;
                break;
            }
            continue;
        }

        if (ch == '\n' || ch == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            break;
        }

        if (ch == 4) { // Ctrl+D
            if (buffer.text.empty()) {
                result.eof = true;
                break;
            }

            if (editor_state::erase_at_cursor(buffer, history_state,
                                              state.history.size())) {
                shell::prompt::redraw_input_line(state, buffer.text,
                                                 buffer.cursor, false);
            }
            continue;
        }

        if (ch == '\033') {
            handle_escape_sequence(state, buffer, history_state);
            continue;
        }

        if (ch == 1) { // Ctrl+A
            if (editor_state::move_cursor_home(buffer)) {
                shell::prompt::redraw_input_line(state, buffer.text,
                                                 buffer.cursor, false);
            }
            continue;
        }
        if (ch == 5) { // Ctrl+E
            if (editor_state::move_cursor_end(buffer)) {
                shell::prompt::redraw_input_line(state, buffer.text,
                                                 buffer.cursor, false);
            }
            continue;
        }
        if (ch == 12) { // Ctrl+L
            const char *clear = "\033[2J\033[H";
            write(STDOUT_FILENO, clear, 7);
            shell::prompt::redraw_input_line(state, buffer.text, buffer.cursor,
                                             true);
            continue;
        }

        if (ch == '\t') {
            features::handle_tab_completion(state, buffer.text, buffer.cursor);
            continue;
        }

        if (ch == '\b' || ch == 127) { // backspace
            if (editor_state::erase_before_cursor(buffer, history_state,
                                                  state.history.size())) {
                shell::prompt::redraw_input_line(state, buffer.text,
                                                 buffer.cursor, false);
            }
            continue;
        }

        // normal character insert
        editor_state::insert_character(buffer, ch, history_state,
                                       state.history.size());
        shell::prompt::redraw_input_line(state, buffer.text, buffer.cursor,
                                         false);
    }

    if (input_mode_enabled) {
        restore_input_mode(old_state);
    }
    result.line = buffer.text;
    return result;
}

} // namespace shell::input
