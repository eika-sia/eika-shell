#include "expansion.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "../../builtins/env/env.hpp"
#include "../../shell/shell.hpp"

namespace features {
namespace {

bool should_consume_backslash_escape(char c, bool in_double_quote) {
    if (in_double_quote) {
        return c == '"' || c == '\\' || c == '$' || c == '!' || c == '\n';
    }

    switch (c) {
    case ' ':
    case '\t':
    case '\\':
    case '\'':
    case '"':
    case '$':
    case '!':
    case '~':
    case '|':
    case '&':
    case ';':
    case '<':
    case '>':
    case '(':
    case ')':
    case '[':
    case ']':
    case ',':
    case '=':
    case '\n':
        return true;
    default:
        return false;
    }
}

bool can_expand_tilde(const std::string &raw, size_t i, bool at_word_start) {
    if (!at_word_start || raw[i] != '~') {
        return false;
    }

    return i + 1 == raw.size() || raw[i + 1] == '/';
}

std::vector<std::string> expand_fields(const shell::ShellState &state,
                                       const parser::ast::Word &word) {
    std::vector<std::string> fields;
    std::string current;
    bool has_current = false;
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool at_word_start = true;
    const std::string &raw = word.raw_text;

    for (size_t i = 0; i < raw.size();) {
        const char c = raw[i];

        if (c == '\\' && !in_single_quote) {
            if (i + 1 >= raw.size()) {
                current.push_back('\\');
                has_current = true;
                at_word_start = false;
                ++i;
                continue;
            }

            const char next = raw[i + 1];
            if (next == '\n') {
                i += 2;
                continue;
            }

            if (should_consume_backslash_escape(next, in_double_quote)) {
                current.push_back(next);
            } else {
                current.push_back('\\');
                current.push_back(next);
            }

            has_current = true;
            at_word_start = false;
            i += 2;
            continue;
        }

        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            has_current = true;
            at_word_start = false;
            ++i;
            continue;
        }

        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            has_current = true;
            at_word_start = false;
            ++i;
            continue;
        }

        if (!in_single_quote && !in_double_quote &&
            can_expand_tilde(raw, i, at_word_start)) {
            const shell::ShellVariable *home =
                builtins::env::find_variable(state, "HOME");
            if (home != nullptr) {
                current += home->value;
                has_current = true;
                at_word_start = false;
                ++i;
                continue;
            }
        }

        if (!in_single_quote && c == '$') {
            size_t end = i;
            const std::string value =
                builtins::env::expand_variable_reference(state, raw, i, end);
            if (end == i + 1 && value == "$") {
                current.push_back('$');
                has_current = true;
                at_word_start = false;
                ++i;
                continue;
            }

            if (!in_double_quote) {
                for (char value_char : value) {
                    if (std::isspace(static_cast<unsigned char>(value_char))) {
                        if (has_current) {
                            fields.push_back(std::move(current));
                            current.clear();
                            has_current = false;
                        }
                        continue;
                    }

                    current.push_back(value_char);
                    has_current = true;
                }
            } else {
                current += value;
                has_current = true;
            }

            at_word_start = false;
            i = end;
            continue;
        }

        current.push_back(c);
        has_current = true;
        at_word_start = false;
        ++i;
    }

    if (has_current) {
        fields.push_back(std::move(current));
    }

    return fields;
}

void expand_invocation(const shell::ShellState &state,
                       parser::ast::CommandInvocation &invocation) {
    std::vector<parser::ast::Word> words;

    for (const parser::ast::Word &word : invocation.words) {
        for (std::string &field : expand_fields(state, word)) {
            parser::ast::Word expanded = word;
            expanded.text = std::move(field);
            words.push_back(std::move(expanded));
        }
    }

    invocation.words = std::move(words);
}

bool expand_redirect(const shell::ShellState &state,
                     parser::ast::Redirect &redirect,
                     std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    std::vector<std::string> fields = expand_fields(state, redirect.target);
    if (fields.size() != 1) {
        shell::diagnostics::add_error(diagnostics, redirect.target.span,
                                      "ambiguous redirect");
        return false;
    }

    redirect.target.text = std::move(fields.front());
    return true;
}

bool expand_command(shell::ShellState &state, parser::ast::SimpleCommand &cmd,
                    std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation) {
        expand_invocation(state, *cmd.invocation);
        if (cmd.invocation->words.empty()) {
            cmd.invocation.reset();
        }
    }

    bool ok = true;
    for (parser::ast::Redirect &redirect : cmd.redirects) {
        ok = expand_redirect(state, redirect, diagnostics) && ok;
    }

    return ok;
}

} // namespace

bool expand_pipeline(shell::ShellState &state, parser::ast::Pipeline &pipeline,
                     std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    bool ok = true;
    for (parser::ast::SimpleCommand &cmd : pipeline.commands) {
        ok = expand_command(state, cmd, diagnostics) && ok;
    }

    return ok;
}

} // namespace features
