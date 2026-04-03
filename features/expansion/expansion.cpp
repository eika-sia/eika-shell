#include "expansion.hpp"

#include <cstdlib>
#include <string>
#include <vector>

#include "../../builtins/env/env.hpp"
#include "../../shell/shell.hpp"
#include "../shell_text/shell_text.hpp"

namespace features {
namespace {

bool can_expand_tilde_at(const std::string &line, size_t i) {
    if (line[i] != '~') {
        return false;
    }

    const bool at_word_start =
        (i == 0) || shell_text::is_shell_separator(line[i - 1]);
    if (!at_word_start) {
        return false;
    }

    if (i + 1 >= line.size()) {
        return true;
    }

    const char next = line[i + 1];
    return next == '/' || shell_text::is_shell_separator(next);
}

std::string expand_tilde(const shell::ShellState &state,
                         const std::string &line) {
    const shell::ShellVariable *home =
        builtins::env::find_variable(state, "HOME");
    if (home == nullptr) {
        return line;
    }

    std::vector<shell_text::Replacement> replacements;
    shell_text::for_each_unescaped_position(
        line, [&](size_t &i, const shell_text::ScanState &scan_state) {
            if (scan_state.in_single_quote || scan_state.in_double_quote ||
                !can_expand_tilde_at(line, i)) {
                return true;
            }

            replacements.push_back(
                shell_text::Replacement{i, i + 1, home->value});
            return true;
        });

    return shell_text::apply_replacements(line, replacements);
}

bool reparse_command(parser::Command &cmd, std::string expanded) {
    cmd = parser::parse_command(expanded);
    return cmd.valid;
}

} // namespace

bool expand_command(shell::ShellState &state, parser::Command &cmd) {
    if (!reparse_command(cmd,
                         builtins::env::expand_variables(state, cmd.raw))) {
        return false;
    }

    return reparse_command(cmd, expand_tilde(state, cmd.raw));
}

} // namespace features
