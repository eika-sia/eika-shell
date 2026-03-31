#include "input.hpp"

#include <cerrno>
#include <cstdio>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

#include "../../features/completion/completion.hpp"
#include "../prompt/prompt.hpp"

namespace shell::input {
namespace {

// helper funckije za manual handling stvari (enable/disable jer zelimo da drugi
// programi budu normalni)
void enable_input_mode(struct termios &old_state) {
    if (tcgetattr(STDIN_FILENO, &old_state) == -1) {
        perror("tcgetattr");
        return;
    }

    struct termios raw = old_state;

    raw.c_lflag &= ~ICANON;
    raw.c_lflag &= ~ECHO;

    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
    }
}

void restore_input_mode(const struct termios &old_state) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_state) == -1) {
        perror("tcsetattr");
    }
}

// svaki escape char moramo redrawat cijelu liniju :(
// posljedica takvog inputa je
void redraw_line(const std::string &line, size_t cursor) {
    write(STDOUT_FILENO, "\r", 1);
    const char *clear = "\033[2K";
    write(STDOUT_FILENO, clear, 4);
    std::string prompt = shell::prompt::build_prompt_prefix();
    write(STDOUT_FILENO, prompt.c_str(), prompt.size());
    write(STDOUT_FILENO, line.c_str(), line.size());

    size_t right_edge = line.size();
    if (right_edge > cursor) {
        std::string move_left =
            "\033[" + std::to_string(right_edge - cursor) + "D";
        write(STDOUT_FILENO, move_left.c_str(), move_left.size());
    }
}

// \033 escape sequence parsing (za strelice je lagano samo pomoicemo cursor
// (isto esc seq) lijevo desno)
void handle_escape_sequence(std::string &buf, size_t &cursor,
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
        redraw_line(buf, cursor);
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
        redraw_line(buf, cursor);
        break;
    }

    case 'C': { // Right
        if (cursor < buf.size()) {
            cursor++;
            const char *move = "\033[1C";
            write(STDOUT_FILENO, move, 4);
        }
        break;
    }

    case 'D': { // Left
        if (cursor > 0) {
            cursor--;
            const char *move = "\033[1D";
            write(STDOUT_FILENO, move, 4);
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
                redraw_line(buf, cursor);
            }
        }
    }
}

} // namespace

InputResult read_command_line(std::vector<std::string> &hist) {
    InputResult result{};
    std::string buf;
    std::string draft;

    char ch = '\0';
    size_t cursor = 0;
    size_t hist_index = hist.size();
    bool browsing_history = false;

    struct termios old_state;
    enable_input_mode(old_state);

    while (true) {
        ssize_t n = read(STDIN_FILENO, &ch, 1);

        if (n == 0) {
            result.eof = true;
            break;
        }

        if (n < 0) {
            if (errno == EINTR) {
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
            handle_escape_sequence(buf, cursor, hist, hist_index, draft,
                                   browsing_history);
            continue;
        }

        if (ch == 1) { // Ctrl+A
            if (cursor > 0) {
                std::string move = "\033[" + std::to_string(cursor) + "D";
                write(STDOUT_FILENO, move.c_str(), move.size());
                cursor = 0;
            }
            continue;
        }
        if (ch == 5) { // Ctrl+E
            size_t right = buf.size() - cursor;
            if (right > 0) {
                std::string move = "\033[" + std::to_string(right) + "C";
                write(STDOUT_FILENO, move.c_str(), move.size());
                cursor = buf.size();
            }
            continue;
        }
        if (ch == 12) { // Ctrl+L
            const char *clear = "\033[2J\033[H";
            write(STDOUT_FILENO, clear, 7);

            std::string prompt = shell::prompt::build_prompt();

            write(STDOUT_FILENO, prompt.c_str(), prompt.size());
            write(STDOUT_FILENO, buf.c_str(), buf.size());

            size_t right_edge = buf.size();
            if (right_edge > cursor) {
                std::string move =
                    "\033[" + std::to_string(right_edge - cursor) + "D";
                write(STDOUT_FILENO, move.c_str(), move.size());
            }

            continue;
        }

        if (ch == '\t') {
            features::handle_tab_completion(buf, cursor);
            continue;
        }

        if (ch == '\b' || ch == 127) { // backspace
            if (cursor > 0) {
                if (browsing_history) {
                    browsing_history = false;
                    hist_index = hist.size();
                }

                buf.erase(cursor - 1, 1);
                cursor--;
                redraw_line(buf, cursor);
            }
            continue;
        }

        // normal character insert
        if (browsing_history) {
            browsing_history = false;
            hist_index = hist.size();
        }

        buf.insert(cursor, 1, ch);
        cursor++;
        redraw_line(buf, cursor);
    }

    restore_input_mode(old_state);
    result.line = buf;
    return result;
}

} // namespace shell::input
