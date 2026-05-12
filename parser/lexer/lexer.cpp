#include "lexer.hpp"
#include "../assignments/assignment.hpp"

#include <string>
#include <utility>
#include <vector>

namespace parser {
namespace {

enum class LexicalCommandContext {
    CommandPrefix,
    AfterCommandWord,
};

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

bool starts_comment(const std::string &current, char c, bool in_single_quote,
                    bool in_double_quote) {
    if (c != '#' || in_single_quote || in_double_quote) {
        return false;
    }

    return current.empty();
}

bool is_assignment_token_text(const std::string &text) {
    std::string name;
    std::string value;
    return is_assignment_word(text, name, value);
}

Token make_token(TokenKind kind, std::string text, const std::string &source,
                 SourceSpan span) {
    return Token{kind, std::move(text),
                 source.substr(span.start, span.end - span.start), span};
}

void reset_command_context(LexicalCommandContext &context,
                           bool &expecting_redirect_target) {
    context = LexicalCommandContext::CommandPrefix;
    expecting_redirect_target = false;
}

void mark_command_word_seen_if_needed(LexicalCommandContext &context,
                                      bool expecting_redirect_target) {
    if (!expecting_redirect_target &&
        context == LexicalCommandContext::CommandPrefix) {
        context = LexicalCommandContext::AfterCommandWord;
    }
}

void flush_word(std::vector<Token> &tokens, std::string &current,
                const std::string &source, size_t &current_start,
                size_t &current_end, LexicalCommandContext &context,
                bool &expecting_redirect_target) {
    if (current_start == std::string::npos) {
        return;
    }

    const bool is_assignment =
        !expecting_redirect_target &&
        context == LexicalCommandContext::CommandPrefix &&
        is_assignment_token_text(current);
    const TokenKind kind =
        is_assignment ? TokenKind::Assignment : TokenKind::Word;

    tokens.push_back(make_token(kind, std::move(current), source,
                                SourceSpan{current_start, current_end}));
    if (!is_assignment) {
        mark_command_word_seen_if_needed(context, expecting_redirect_target);
    }

    if (expecting_redirect_target) {
        expecting_redirect_target = false;
    }

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

LexResult lex(const std::string &source, LexMode mode) {
    LexResult result{};

    std::string current;
    size_t current_start = std::string::npos;
    size_t current_end = std::string::npos;

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escape = false;
    bool escape_in_double_quote = false;
    size_t single_quote_start = std::string::npos;
    size_t double_quote_start = std::string::npos;
    LexicalCommandContext command_context =
        LexicalCommandContext::CommandPrefix;
    bool expecting_redirect_target = false;

    for (size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];

        if (escape) {
            if (c == '\n') {
                // Backslash-newline is a line continuation: it affects the raw
                // span but disappears from the logical token text.
                current_end = i + 1;
                escape = false;
                continue;
            }

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
            flush_word(result.tokens, current, source, current_start,
                       current_end, command_context, expecting_redirect_target);
            continue;
        }

        if (starts_comment(current, c, in_single_quote, in_double_quote)) {
            flush_word(result.tokens, current, source, current_start,
                       current_end, command_context, expecting_redirect_target);
            while (i + 1 < source.size() && source[i + 1] != '\n') {
                ++i;
            }
            continue;
        }

        if (!in_single_quote && !in_double_quote && c == '\n') {
            flush_word(result.tokens, current, source, current_start,
                       current_end, command_context, expecting_redirect_target);
            result.tokens.push_back(make_token(TokenKind::Newline, "\n", source,
                                               SourceSpan{i, i + 1}));
            reset_command_context(command_context, expecting_redirect_target);
            continue;
        }

        if (!in_single_quote && !in_double_quote &&
            (c == '<' || c == '>' || c == '|' || c == '&' || c == ';' ||
             c == '(' || c == ')' || c == '[' || c == ']' || c == ',')) {
            flush_word(result.tokens, current, source, current_start,
                       current_end, command_context, expecting_redirect_target);

            if (c == '<') {
                result.tokens.push_back(make_token(TokenKind::InputRedirect,
                                                   "<", source,
                                                   SourceSpan{i, i + 1}));
                expecting_redirect_target = true;
                continue;
            }

            if (c == '|') {
                if (i + 1 < source.size() && source[i + 1] == '|') {
                    result.tokens.push_back(make_token(
                        TokenKind::OrIf, "||", source, SourceSpan{i, i + 2}));
                    ++i;
                } else {
                    result.tokens.push_back(make_token(
                        TokenKind::Pipe, "|", source, SourceSpan{i, i + 1}));
                }
                reset_command_context(command_context,
                                      expecting_redirect_target);
                continue;
            }

            if (c == ';') {
                result.tokens.push_back(make_token(
                    TokenKind::Semicolon, ";", source, SourceSpan{i, i + 1}));
                reset_command_context(command_context,
                                      expecting_redirect_target);
                continue;
            }

            if (c == '&') {
                if (i + 1 < source.size() && source[i + 1] == '&') {
                    result.tokens.push_back(make_token(
                        TokenKind::AndIf, "&&", source, SourceSpan{i, i + 2}));
                    ++i;
                } else {
                    result.tokens.push_back(make_token(TokenKind::Background,
                                                       "&", source,
                                                       SourceSpan{i, i + 1}));
                }
                reset_command_context(command_context,
                                      expecting_redirect_target);
                continue;
            }

            if (c == '(') {
                result.tokens.push_back(make_token(
                    TokenKind::LeftParen, "(", source, SourceSpan{i, i + 1}));
                reset_command_context(command_context,
                                      expecting_redirect_target);
                continue;
            }

            if (c == ')') {
                result.tokens.push_back(make_token(
                    TokenKind::RightParen, ")", source, SourceSpan{i, i + 1}));
                reset_command_context(command_context,
                                      expecting_redirect_target);
                continue;
            }

            if (c == '[') {
                result.tokens.push_back(make_token(
                    TokenKind::LeftBracket, "[", source, SourceSpan{i, i + 1}));
                mark_command_word_seen_if_needed(command_context,
                                                 expecting_redirect_target);
                continue;
            }

            if (c == ']') {
                result.tokens.push_back(make_token(TokenKind::RightBracket, "]",
                                                   source,
                                                   SourceSpan{i, i + 1}));
                mark_command_word_seen_if_needed(command_context,
                                                 expecting_redirect_target);
                continue;
            }

            if (c == ',') {
                result.tokens.push_back(make_token(
                    TokenKind::Comma, ",", source, SourceSpan{i, i + 1}));
                continue;
            }

            if (i + 1 < source.size() && source[i + 1] == '>') {
                result.tokens.push_back(make_token(TokenKind::AppendRedirect,
                                                   ">>", source,
                                                   SourceSpan{i, i + 2}));
                ++i;
            } else {
                result.tokens.push_back(make_token(TokenKind::OutputRedirect,
                                                   ">", source,
                                                   SourceSpan{i, i + 1}));
            }
            expecting_redirect_target = true;
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
        current_end = source.size();
    }

    if (in_single_quote || in_double_quote) {
        if (mode == LexMode::Strict) {
            const size_t quote_start =
                in_single_quote ? single_quote_start : double_quote_start;
            const size_t span_start =
                quote_start == std::string::npos ? current_start : quote_start;
            shell::diagnostics::add_error(result.diagnostics,
                                          SourceSpan{span_start, current_end},
                                          "syntax error: unmatched quote");
        } else {
            flush_word(result.tokens, current, source, current_start,
                       current_end, command_context, expecting_redirect_target);
        }

        result.ok = false;
        result.unmatched_single_quote = in_single_quote;
        result.unmatched_double_quote = in_double_quote;
        return result;
    }

    flush_word(result.tokens, current, source, current_start, current_end,
               command_context, expecting_redirect_target);
    result.tokens.push_back(
        make_token(TokenKind::EndOfFile, "", source,
                   SourceSpan{source.size(), source.size()}));
    return result;
}

} // namespace parser
