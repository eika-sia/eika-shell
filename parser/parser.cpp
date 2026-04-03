#include "internals/internal.hpp"

#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

#include "../shell/shell.hpp"

namespace parser {
namespace {

bool tokenize_work_line(const std::string &line, std::string &work,
                        std::vector<Token> &tokens) {
    work = shell::trim(line);
    if (work.empty()) {
        return false;
    }

    if (!tokenize_line(work, tokens)) {
        return false;
    }

    return !tokens.empty();
}

bool is_one_of(TokenKind kind, std::initializer_list<TokenKind> disallowed) {
    for (TokenKind blocked : disallowed) {
        if (kind == blocked) {
            return true;
        }
    }

    return false;
}

bool reject_tokens(const std::vector<Token> &tokens,
                   std::initializer_list<TokenKind> disallowed) {
    for (const Token &token : tokens) {
        if (is_one_of(token.kind, disallowed)) {
            std::cerr << "syntax error: unexpected token " << token.text
                      << "\n";
            return false;
        }
    }

    return true;
}

bool parse_command_tokens_checked(const std::vector<Token> &tokens,
                                  const std::string &source, Command &cmd) {
    if (!reject_tokens(tokens,
                       {TokenKind::Pipe, TokenKind::AndIf, TokenKind::OrIf,
                        TokenKind::Sequence, TokenKind::Background})) {
        return false;
    }

    return parse_simple_command(tokens, source, cmd);
}

bool parse_pipeline_tokens_checked(const std::vector<Token> &tokens,
                                   const std::string &source, Pipeline &pipe) {
    if (!reject_tokens(tokens, {TokenKind::AndIf, TokenKind::OrIf,
                                TokenKind::Sequence, TokenKind::Background})) {
        return false;
    }

    return parse_pipeline_tokens(tokens, source, pipe);
}

bool parse_conditional_tokens_checked(const std::vector<Token> &tokens,
                                      const std::string &source,
                                      ConditionalChain &chain) {
    if (!reject_tokens(tokens, {TokenKind::Sequence, TokenKind::Background})) {
        return false;
    }

    return parse_and_or_tokens(tokens, source, chain);
}

bool parse_command_line_tokens(const std::vector<Token> &tokens,
                               const std::string &source, CommandList &list) {
    // `;` and `&` split toplevel chains
    // `&&` and `||` stay inside one chain and are parsed one level down.
    std::vector<Token> current_chain_tokens;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token &token = tokens[i];

        if (token.kind != TokenKind::Sequence &&
            token.kind != TokenKind::Background) {
            current_chain_tokens.push_back(token);
            continue;
        }

        if (current_chain_tokens.empty()) {
            if (token.kind == TokenKind::Sequence && i == tokens.size() - 1) {
                list.valid = true;
                return true;
            }

            std::cerr << "syntax error: missing command before " << token.text
                      << "\n";
            return false;
        }

        ConditionalChain chain{};
        if (!parse_conditional_tokens_checked(current_chain_tokens, source,
                                              chain)) {
            return false;
        }

        chain.background = (token.kind == TokenKind::Background);
        list.conditional_chains.push_back(chain);
        current_chain_tokens.clear();
    }

    if (current_chain_tokens.empty()) {
        if (!tokens.empty() && (tokens.back().kind == TokenKind::Sequence ||
                                tokens.back().kind == TokenKind::Background)) {
            list.valid = true;
            return true;
        }

        std::cerr << "syntax error: missing command\n";
        return false;
    }

    ConditionalChain chain{};
    if (!parse_conditional_tokens_checked(current_chain_tokens, source,
                                          chain)) {
        return false;
    }

    list.conditional_chains.push_back(chain);
    list.valid = true;
    return true;
}

} // namespace

Command parse_command(const std::string &line) {
    Command cmd{};
    cmd.valid = false;

    std::string work;
    std::vector<Token> tokens;
    if (!tokenize_work_line(line, work, tokens)) {
        return cmd;
    }

    parse_command_tokens_checked(tokens, work, cmd);
    return cmd;
}

Pipeline parse_pipeline(const std::string &line) {
    Pipeline pipe{};
    pipe.valid = false;

    std::string work;
    std::vector<Token> tokens;
    if (!tokenize_work_line(line, work, tokens)) {
        return pipe;
    }

    parse_pipeline_tokens_checked(tokens, work, pipe);
    return pipe;
}

CommandList parse_command_line(const std::string &line) {
    CommandList list{};
    list.valid = false;

    std::string work;
    std::vector<Token> tokens;
    if (!tokenize_work_line(line, work, tokens)) {
        return list;
    }

    parse_command_line_tokens(tokens, work, list);
    return list;
}

} // namespace parser
