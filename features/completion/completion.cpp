#include "completion.hpp"
#include "path_completion.hpp"

#include "../shell_text/shell_text.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace features {
namespace {

size_t get_current_token_start(const std::string &buf, size_t cursor) {
    if (cursor > buf.size()) {
        cursor = buf.size();
    }

    size_t start = cursor;
    while (start > 0 && !shell_text::is_shell_separator(buf[start - 1])) {
        --start;
    }

    return start;
}

size_t get_current_token_end(const std::string &buf, size_t cursor) {
    if (cursor > buf.size()) {
        cursor = buf.size();
    }

    size_t end = cursor;
    while (end < buf.size() && !shell_text::is_shell_separator(buf[end])) {
        ++end;
    }

    return end;
}

bool is_command_position(const std::string &buf, size_t cursor) {
    size_t start = get_current_token_start(buf, cursor);

    while (start > 0 && (buf[start - 1] == ' ' || buf[start - 1] == '\t')) {
        --start;
    }

    if (start == 0) {
        return true;
    }

    const char prev = buf[start - 1];
    return prev == '|' || prev == ';' || prev == '&';
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

} // namespace

CompletionResult complete_at_cursor(const shell::ShellState &state,
                                    const std::string &buf, size_t cursor) {
    size_t token_start = get_current_token_start(buf, cursor);
    size_t token_end = get_current_token_end(buf, cursor);
    const bool command_position = is_command_position(buf, cursor);

    std::string token = buf.substr(token_start, token_end - token_start);

    std::vector<std::string> matches;
    if (features::looks_like_path_token(token)) {
        matches = features::complete_path_token(state, token, command_position);
    } else if (command_position) {
        matches = features::complete_command_token(state, token);
    } else {
        matches = features::complete_path_token(state, token);
    }

    if (matches.empty()) {
        return {};
    }

    std::sort(matches.begin(), matches.end());

    if (matches.size() == 1) {
        return CompletionResult{CompletionAction::ReplaceToken, token_start,
                                token_end, matches[0], {}};
    }

    std::string lcp = longest_common_prefix(matches);
    if (lcp.size() > token.size()) {
        return CompletionResult{CompletionAction::ReplaceToken, token_start,
                                token_end, lcp, {}};
    }

    return CompletionResult{CompletionAction::ShowCandidates, token_start,
                            token_end, "", std::move(matches)};
}

} // namespace features
