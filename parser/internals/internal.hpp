#pragma once

#include "../parser.hpp"

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

bool is_redirect(TokenKind kind);
bool tokenize_line(const std::string &line, std::vector<Token> &tokens);
bool parse_simple_command(const std::vector<Token> &tokens,
                          const std::string &source, Command &cmd);
bool parse_pipeline_tokens(const std::vector<Token> &tokens,
                           const std::string &source, Pipeline &pipe);
bool parse_and_or_tokens(const std::vector<Token> &tokens,
                         const std::string &source,
                         ConditionalPipeline &and_or);

} // namespace parser
