#include "input.hpp"

#include <cerrno>
#include <cstdio>
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

#include "../../features/completion/completion.hpp"
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
void handle_escape_sequence(const shell::ShellState &state, std::string &buf,
                            size_t &cursor,
                            const std::vector<std::string> &hist,
                            size_t &hist_index, std::string &draft,
                            bool &browsing_history) {
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
        if (hist.empty()) {
            return;
        }

        if (!browsing_history) {
            draft = buf;
            browsing_history = true;
            hist_index = hist.size() - 1;
        } else if (hist_index > 0) {
            hist_index--;
        }

        buf = hist[hist_index];
        cursor = buf.size();
        shell::prompt::redraw_input_line(state, buf, cursor, false);
        break;
    }

    case 'B': { // Down
        if (!browsing_history) {
            return;
        }

        if (hist_index + 1 < hist.size()) {
            hist_index++;
            buf = hist[hist_index];
        } else {
            browsing_history = false;
            hist_index = hist.size();
            buf = draft;
        }

        cursor = buf.size();
        shell::prompt::redraw_input_line(state, buf, cursor, false);
        break;
    }

    case 'C': { // Right
        if (cursor < buf.size()) {
            cursor++;
            shell::prompt::redraw_input_line(state, buf, cursor, false);
        }
        break;
    }

    case 'D': { // Left
        if (cursor > 0) {
            cursor--;
            shell::prompt::redraw_input_line(state, buf, cursor, false);
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
            if (cursor < buf.size()) {
                buf.erase(cursor, 1);
                shell::prompt::redraw_input_line(state, buf, cursor, false);
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
    std::string buf;
    std::string draft;

    char ch = '\0';
    size_t cursor = 0;
    size_t hist_index = state.history.size();
    bool browsing_history = false;

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

        if (ch == '\033') {
            handle_escape_sequence(state, buf, cursor, state.history, hist_index,
                                   draft, browsing_history);
            continue;
        }

        if (ch == 1) { // Ctrl+A
            if (cursor > 0) {
                cursor = 0;
                shell::prompt::redraw_input_line(state, buf, cursor, false);
            }
            continue;
        }
        if (ch == 5) { // Ctrl+E
            size_t right = buf.size() - cursor;
            if (right > 0) {
                cursor = buf.size();
                shell::prompt::redraw_input_line(state, buf, cursor, false);
            }
            continue;
        }
        if (ch == 12) { // Ctrl+L
            const char *clear = "\033[2J\033[H";
            write(STDOUT_FILENO, clear, 7);
            shell::prompt::redraw_input_line(state, buf, cursor, true);
            continue;
        }

        if (ch == '\t') {
            features::handle_tab_completion(state, buf, cursor);
            continue;
        }

        if (ch == '\b' || ch == 127) { // backspace
            if (cursor > 0) {
                if (browsing_history) {
                    browsing_history = false;
                    hist_index = state.history.size();
                }

                buf.erase(cursor - 1, 1);
                cursor--;
                shell::prompt::redraw_input_line(state, buf, cursor, false);
            }
            continue;
        }

        // normal character insert
        if (browsing_history) {
            browsing_history = false;
            hist_index = state.history.size();
        }

        buf.insert(cursor, 1, ch);
        cursor++;
        shell::prompt::redraw_input_line(state, buf, cursor, false);
    }

    if (input_mode_enabled) {
        restore_input_mode(old_state);
    }
    result.line = buf;
    return result;
}

} // namespace shell::input
