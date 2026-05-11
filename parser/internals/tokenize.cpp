#include "tokenize.hpp"

#include <string>
#include <utility>
#include <vector>

namespace parser {
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
    case '\n':
        return true;
    default:
        return false;
    }
}

bool starts_comment(const std::string &current, char c, bool in_single_quote,
                    bool in_double_quote) {
    if (c != '#' || in_single_quote || in_double_quote) {
        return false;
    }

    return current.empty();
}

Token make_token(TokenKind kind, std::string text, SourceSpan span) {
    return Token{kind, std::move(text), span, span.start, span.end};
}

void flush_word(std::vector<Token> &tokens, std::string &current,
                size_t &current_start, size_t &current_end) {
    if (current_start == std::string::npos) {
        return;
    }

    tokens.push_back(make_token(TokenKind::Word, std::move(current),
                                SourceSpan{current_start, current_end}));
    current.clear();
    current_start = std::string::npos;
    current_end = std::string::npos;
}

} // namespace

bool is_redirect(TokenKind kind) {
    return kind == TokenKind::InputRedirect ||
           kind == TokenKind::OutputRedirect ||
           kind == TokenKind::AppendRedirect;
}

TokenizeResult tokenize_line(const std::string &line,
                             std::vector<Token> &tokens, TokenizeMode mode) {
    std::string current;
    size_t current_start = std::string::npos;
    size_t current_end = std::string::npos;

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escape = false;
    bool escape_in_double_quote = false;
    size_t single_quote_start = std::string::npos;
    size_t double_quote_start = std::string::npos;

    std::vector<diagnostics::Diagnostic> diagnostics{};

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (escape) {
            if (should_consume_backslash_escape(c, escape_in_double_quote)) {
                current.push_back(c);
            } else {
                current.push_back('\\');
                current.push_back(c);
            }
            current_end = i + 1;
            escape = false;
            continue;
        }

        if (c == '\\' && !in_single_quote) {
            if (current_start == std::string::npos) {
                current_start = i;
            }
            current_end = i + 1;
            escape = true;
            escape_in_double_quote = in_double_quote;
            continue;
        }

        if (c == '\'' && !in_double_quote) {
            if (current_start == std::string::npos) {
                current_start = i;
            }
            current_end = i + 1;
            if (in_single_quote) {
                single_quote_start = std::string::npos;
            } else {
                single_quote_start = i;
            }
            in_single_quote = !in_single_quote;
            continue;
        }

        if (c == '"' && !in_single_quote) {
            if (current_start == std::string::npos) {
                current_start = i;
            }
            current_end = i + 1;
            if (in_double_quote) {
                double_quote_start = std::string::npos;
            } else {
                double_quote_start = i;
            }
            in_double_quote = !in_double_quote;
            continue;
        }

        if (!in_single_quote && !in_double_quote && (c == ' ' || c == '\t')) {
            flush_word(tokens, current, current_start, current_end);
            continue;
        }

        if (starts_comment(current, c, in_single_quote, in_double_quote)) {
            flush_word(tokens, current, current_start, current_end);
            break;
        }

        if (!in_single_quote && !in_double_quote &&
            (c == '<' || c == '>' || c == '|' || c == '&' || c == ';')) {
            flush_word(tokens, current, current_start, current_end);

            if (c == '<') {
                tokens.push_back(make_token(TokenKind::InputRedirect, "<",
                                            SourceSpan{i, i + 1}));
                continue;
            }

            if (c == '|') {
                if (i + 1 < line.size() && line[i + 1] == '|') {
                    tokens.push_back(make_token(TokenKind::OrIf, "||",
                                                SourceSpan{i, i + 2}));
                    ++i;
                } else {
                    tokens.push_back(
                        make_token(TokenKind::Pipe, "|", SourceSpan{i, i + 1}));
                }
                continue;
            }

            if (c == ';') {
                tokens.push_back(
                    make_token(TokenKind::Sequence, ";", SourceSpan{i, i + 1}));
                continue;
            }

            if (c == '&') {
                if (i + 1 < line.size() && line[i + 1] == '&') {
                    tokens.push_back(make_token(TokenKind::AndIf, "&&",
                                                SourceSpan{i, i + 2}));
                    ++i;
                } else {
                    tokens.push_back(make_token(TokenKind::Background, "&",
                                                SourceSpan{i, i + 1}));
                }
                continue;
            }

            if (i + 1 < line.size() && line[i + 1] == '>') {
                tokens.push_back(make_token(TokenKind::AppendRedirect, ">>",
                                            SourceSpan{i, i + 2}));
                ++i;
            } else {
                tokens.push_back(make_token(TokenKind::OutputRedirect, ">",
                                            SourceSpan{i, i + 1}));
            }
            continue;
        }

        if (current_start == std::string::npos) {
            current_start = i;
        }

        current.push_back(c);
        current_end = i + 1;
    }

    if (escape) {
        current.push_back('\\');
        current_end = line.size();
    }

    if ((in_single_quote || in_double_quote)) {
        if (mode == TokenizeMode::Strict) {
            const size_t quote_start =
                in_single_quote ? single_quote_start : double_quote_start;
            const size_t span_start =
                quote_start == std::string::npos ? current_start : quote_start;
            diagnostics::add_error(diagnostics,
                                   SourceSpan{span_start, current_end},
                                   "syntax error: unmatched quote");
        } else {
            flush_word(tokens, current, current_start, current_end);
        }
        return TokenizeResult{false, in_single_quote, in_double_quote,
                              diagnostics};
    }

    flush_word(tokens, current, current_start, current_end);
    return TokenizeResult{};
}

} // namespace parser
