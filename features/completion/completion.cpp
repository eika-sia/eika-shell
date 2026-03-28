#include "completion.hpp"
#include "path_completion.hpp"

#include "../../shell/prompt/prompt.hpp"

#include <algorithm>
#include <string>
#include <unistd.h>
#include <vector>

size_t get_current_token_start(const std::string &buf, size_t cursor) {
    if (cursor > buf.size()) {
        cursor = buf.size();
    }

    size_t start = cursor;
    while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '\t') {
        --start;
    }

    return start;
}

size_t get_current_token_end(const std::string &buf, size_t cursor) {
    if (cursor > buf.size()) {
        cursor = buf.size();
    }

    size_t end = cursor;
    while (end < buf.size() && buf[end] != ' ' && buf[end] != '\t') {
        ++end;
    }

    return end;
}

bool is_command_position(const std::string &buf, size_t cursor) {
    size_t start = get_current_token_start(buf, cursor);

    for (size_t i = 0; i < start; ++i) {
        if (buf[i] != ' ' && buf[i] != '\t') {
            return false;
        }
    }

    return true;
}

std::string longest_common_prefix(const std::vector<std::string> &matches) {
    if (matches.empty()) {
        return "";
    }

    std::string prefix = matches[0];

    for (size_t i = 1; i < matches.size(); ++i) {
        size_t j = 0;
        while (j < prefix.size() && j < matches[i].size() &&
               prefix[j] == matches[i][j]) {
            ++j;
        }

        prefix = prefix.substr(0, j);

        if (prefix.empty()) {
            break;
        }
    }

    return prefix;
}

static void redraw_line(const std::string &buf, size_t cursor, bool full) {
    std::string prefix;
    if (full)
        prefix = build_prompt();
    else
        prefix = build_prompt_prefix();

    write(STDOUT_FILENO, "\r", 1);
    write(STDOUT_FILENO, "\033[2K", 4);

    write(STDOUT_FILENO, prefix.c_str(), prefix.size());
    write(STDOUT_FILENO, buf.c_str(), buf.size());

    size_t right_edge = buf.size();
    if (right_edge > cursor) {
        std::string move_left =
            "\033[" + std::to_string(right_edge - cursor) + "D";
        write(STDOUT_FILENO, move_left.c_str(), move_left.size());
    }
}

static void
print_completion_candidates(const std::vector<std::string> &matches) {
    write(STDOUT_FILENO, "\n", 1);
    for (const auto &m : matches) {
        write(STDOUT_FILENO, m.c_str(), m.size());
        write(STDOUT_FILENO, "  ", 2);
    }
    write(STDOUT_FILENO, "\n", 1);
}

void handle_tab_completion(std::string &buf, size_t &cursor) {
    size_t token_start = get_current_token_start(buf, cursor);
    size_t token_end = get_current_token_end(buf, cursor);

    std::string token = buf.substr(token_start, token_end - token_start);

    std::vector<std::string> matches;
    if (looks_like_path_token(token)) {
        matches = complete_path_token(token);
    } else if (is_command_position(buf, cursor)) {
        matches = complete_command_token(token);
    } else {
        matches = complete_path_token(token);
    }

    if (matches.empty()) {
        return;
    }

    std::sort(matches.begin(), matches.end());

    if (matches.size() == 1) {
        buf.replace(token_start, token_end - token_start + 1, matches[0] + " ");
        cursor = token_start + matches[0].size() + 1;
        redraw_line(buf, cursor, false);
        return;
    }

    std::string lcp = longest_common_prefix(matches);
    if (lcp.size() > token.size()) {
        buf.replace(token_start, token_end - token_start, lcp);
        cursor = token_start + lcp.size();
        redraw_line(buf, cursor, false);
        return;
    }

    print_completion_candidates(matches);
    redraw_line(buf, cursor, true);
}
