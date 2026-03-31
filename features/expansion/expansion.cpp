#include "expansion.hpp"

#include <cstdlib>
#include <string>

#include "../../builtins/alias/alias.hpp"
#include "../../shell/shell.hpp"
#include "../env/env.hpp"

namespace features {
namespace {

bool is_word_separator(char c) {
    return c == ' ' || c == '\t' || c == '|' || c == ';' || c == '&' ||
           c == '<' || c == '>';
}

bool can_expand_tilde_at(const std::string &line, size_t i) {
    if (line[i] != '~') {
        return false;
    }

    const bool at_word_start = (i == 0) || is_word_separator(line[i - 1]);
    if (!at_word_start) {
        return false;
    }

    if (i + 1 >= line.size()) {
        return true;
    }

    const char next = line[i + 1];
    return next == '/' || is_word_separator(next);
}

std::string expand_tilde(const std::string &line) {
    std::string expanded;
    expanded.reserve(line.size());

    const char *home = std::getenv("HOME");

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escape = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (escape) {
            expanded.push_back(c);
            escape = false;
            continue;
        }

        if (c == '\\' && !in_single_quote) {
            expanded.push_back(c);
            escape = true;
            continue;
        }

        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            expanded.push_back(c);
            continue;
        }

        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            expanded.push_back(c);
            continue;
        }

        if (!in_single_quote && !in_double_quote &&
            can_expand_tilde_at(line, i) && home != nullptr) {
            expanded += home;
            continue;
        }

        expanded.push_back(c);
    }

    return expanded;
}

bool reparse_command(parser::Command &cmd, std::string expanded) {
    cmd = parser::parse_command(expanded);
    return cmd.valid;
}

} // namespace

bool expand_command(shell::ShellState &state, parser::Command &cmd) {
    if (!builtins::expand_aliases(state, cmd)) {
        return false;
    }

    if (!reparse_command(cmd, features::expand_environment_variables(cmd.raw))) {
        return false;
    }

    return reparse_command(cmd, expand_tilde(cmd.raw));
}

} // namespace features
