#include "internals/internal.hpp"
#include "lexer.hpp"

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace parser {
namespace {

bool is_one_of(TokenKind kind, std::initializer_list<TokenKind> disallowed) {
    for (TokenKind blocked : disallowed) {
        if (kind == blocked) {
            return true;
        }
    }

    return false;
}

bool reject_tokens(const std::vector<Token> &tokens,
                   std::initializer_list<TokenKind> disallowed,
                   std::vector<diagnostics::Diagnostic> &diagnostics) {
    for (const Token &token : tokens) {
        if (is_one_of(token.kind, disallowed)) {
            diagnostics::add_error(diagnostics, token.span,
                                   "syntax error: unexpected token " +
                                       token.text);
            return false;
        }
    }

    return true;
}

bool parse_command_tokens_checked(
    const std::vector<Token> &tokens, const std::string &source, Command &cmd,
    std::vector<diagnostics::Diagnostic> &diagnostics) {
    if (!reject_tokens(tokens,
                       {TokenKind::Pipe, TokenKind::AndIf, TokenKind::OrIf,
                        TokenKind::Semicolon, TokenKind::Newline,
                        TokenKind::Background, TokenKind::EndOfFile},
                       diagnostics)) {
        return false;
    }

    return parse_simple_command(tokens, source, cmd, diagnostics);
}

bool parse_pipeline_tokens_checked(
    const std::vector<Token> &tokens, const std::string &source, Pipeline &pipe,
    std::vector<diagnostics::Diagnostic> &diagnostics) {
    if (!reject_tokens(tokens,
                       {TokenKind::AndIf, TokenKind::OrIf,
                        TokenKind::Semicolon, TokenKind::Newline,
                        TokenKind::Background, TokenKind::EndOfFile},
                       diagnostics)) {
        return false;
    }

    return parse_pipeline_tokens(tokens, source, pipe, diagnostics);
}

bool parse_conditional_tokens_checked(
    const std::vector<Token> &tokens, const std::string &source,
    ConditionalChain &chain,
    std::vector<diagnostics::Diagnostic> &diagnostics) {
    if (!reject_tokens(tokens,
                       {TokenKind::Semicolon, TokenKind::Newline,
                        TokenKind::Background, TokenKind::EndOfFile},
                       diagnostics)) {
        return false;
    }

    return parse_and_or_tokens(tokens, source, chain, diagnostics);
}

bool parse_command_line_tokens(
    const std::vector<Token> &tokens, const std::string &source,
    CommandList &list, std::vector<diagnostics::Diagnostic> &diagnostics) {
    // `;` and `&` split toplevel chains
    // `&&` and `||` stay inside one chain and are parsed one level down.
    std::vector<Token> current_chain_tokens;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token &token = tokens[i];

        if (token.kind == TokenKind::EndOfFile) {
            break;
        }

        if (token.kind != TokenKind::Semicolon &&
            token.kind != TokenKind::Newline &&
            token.kind != TokenKind::Background) {
            current_chain_tokens.push_back(token);
            continue;
        }

        if (current_chain_tokens.empty()) {
            if (token.kind == TokenKind::Newline) {
                continue;
            }

            if (token.kind == TokenKind::Semicolon && i == tokens.size() - 2 &&
                tokens.back().kind == TokenKind::EndOfFile) {
                list.valid = true;
                return true;
            }

            diagnostics::add_error(diagnostics, token.span,
                                   "syntax error: missing command before " +
                                       token.text);
            return false;
        }

        ConditionalChain chain{};
        if (!parse_conditional_tokens_checked(current_chain_tokens, source,
                                              chain, diagnostics)) {
            return false;
        }

        chain.background = (token.kind == TokenKind::Background);
        list.conditional_chains.push_back(chain);
        current_chain_tokens.clear();
    }

    if (current_chain_tokens.empty()) {
        if (!tokens.empty()) {
            list.valid = true;
            return true;
        }

        diagnostics::add_error(diagnostics, SourceSpan{0, 0},
                               "syntax error: missing command");
        return false;
    }

    ConditionalChain chain{};
    if (!parse_conditional_tokens_checked(current_chain_tokens, source, chain,
                                          diagnostics)) {
        return false;
    }

    list.conditional_chains.push_back(chain);
    list.valid = true;
    return true;
}

void drop_end_of_file_token(std::vector<Token> &tokens) {
    if (!tokens.empty() && tokens.back().kind == TokenKind::EndOfFile) {
        tokens.pop_back();
    }
}

} // namespace

Command parse_command(const std::string &line) {
    Command cmd{};
    cmd.valid = false;
    std::vector<diagnostics::Diagnostic> diagnostics;

    std::vector<Token> tokens;
    LexResult lex_result = lex(line);
    if (!lex_result.ok) {
        cmd.diagnostics = std::move(lex_result.diagnostics);
        return cmd;
    }

    tokens = std::move(lex_result.tokens);
    drop_end_of_file_token(tokens);
    if (tokens.empty()) {
        return cmd;
    }

    parse_command_tokens_checked(tokens, line, cmd, diagnostics);
    cmd.diagnostics = diagnostics;
    return cmd;
}

Pipeline parse_pipeline(const std::string &line) {
    Pipeline pipe{};
    pipe.valid = false;
    std::vector<diagnostics::Diagnostic> diagnostics;

    std::vector<Token> tokens;
    LexResult lex_result = lex(line);
    if (!lex_result.ok) {
        pipe.diagnostics = std::move(lex_result.diagnostics);
        return pipe;
    }

    tokens = std::move(lex_result.tokens);
    drop_end_of_file_token(tokens);
    if (tokens.empty()) {
        return pipe;
    }

    parse_pipeline_tokens_checked(tokens, line, pipe, diagnostics);
    pipe.diagnostics = diagnostics;
    return pipe;
}

CommandList parse_command_line(const std::string &line) {
    CommandList list{};
    list.valid = false;
    std::vector<diagnostics::Diagnostic> diagnostics;

    std::vector<Token> tokens;
    LexResult lex_result = lex(line);
    if (!lex_result.ok) {
        list.diagnostics = std::move(lex_result.diagnostics);
        return list;
    }

    tokens = std::move(lex_result.tokens);
    if (tokens.size() == 1 && tokens.front().kind == TokenKind::EndOfFile) {
        list.valid = true;
        return list;
    }

    parse_command_line_tokens(tokens, line, list, diagnostics);
    list.diagnostics = diagnostics;
    return list;
}

} // namespace parser
