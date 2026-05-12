#pragma once

#include <string>
#include <vector>

#include "../../shell/diagnostics/diagnostics.hpp"
#include "../source.hpp"

namespace parser {

enum class TokenKind {
    Word,
    Assignment,
    Newline,
    Semicolon,
    Pipe,
    AndIf,
    OrIf,
    Background,
    InputRedirect,
    OutputRedirect,
    AppendRedirect,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    Comma,
    EndOfFile,
};

struct Token {
    TokenKind kind;
    std::string text;
    std::string raw_text;
    SourceSpan span;
};

enum class LexMode {
    Strict,
    Relaxed,
};

struct LexResult {
    bool ok = true;
    bool unmatched_single_quote = false;
    bool unmatched_double_quote = false;
    std::vector<Token> tokens;
    std::vector<shell::diagnostics::Diagnostic> diagnostics;
};

bool is_redirect(TokenKind kind);
LexResult lex(const std::string &source, LexMode mode = LexMode::Strict);

} // namespace parser
