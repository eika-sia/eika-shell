#pragma once

#include <string>
#include <vector>

namespace parser {

enum class TokenKind {
    Word,
    InputRedirect,
    OutputRedirect,
    AppendRedirect,
    Pipe,
    AndIf,
    OrIf,
    Sequence,
    Background,
};

struct Token {
    TokenKind kind;
    std::string text;
    size_t raw_start = std::string::npos;
    size_t raw_end = std::string::npos;
};

enum class TokenizeMode {
    Strict,
    Relaxed,
};

struct TokenizeResult {
    bool ok = true;
    bool unmatched_single_quote = false;
    bool unmatched_double_quote = false;
};

bool is_redirect(TokenKind kind);
TokenizeResult tokenize_line(const std::string &line,
                             std::vector<Token> &tokens,
                             TokenizeMode mode = TokenizeMode::Strict);
} // namespace parser
