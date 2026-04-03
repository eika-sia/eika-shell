#include "internals/internal.hpp"

#include <iostream>
#include <string>
#include <vector>

#include "../shell/shell.hpp"

namespace parser {

Command parse_command(const std::string &line) {
    Command cmd{};
    cmd.valid = false;

    std::string work = shell::trim(line);
    if (work.empty()) {
        return cmd;
    }

    std::vector<Token> tokens;
    if (!tokenize_line(work, tokens)) {
        return cmd;
    }

    if (tokens.empty()) {
        return cmd;
    }

    for (const Token &token : tokens) {
        if (token.kind == TokenKind::Pipe || token.kind == TokenKind::AndIf ||
            token.kind == TokenKind::OrIf ||
            token.kind == TokenKind::Sequence ||
            token.kind == TokenKind::Background) {
            std::cerr << "syntax error: unexpected token " << token.text
                      << "\n";
            return cmd;
        }
    }

    parse_simple_command(tokens, work, cmd);
    return cmd;
}

Pipeline parse_pipeline(const std::string &line) {
    Pipeline pipe{};
    pipe.valid = false;

    std::string work = shell::trim(line);
    if (work.empty()) {
        return pipe;
    }

    std::vector<Token> tokens;
    if (!tokenize_line(work, tokens)) {
        return pipe;
    }

    if (tokens.empty()) {
        return pipe;
    }

    if (tokens.back().kind == TokenKind::Background) {
        pipe.background = true;
        tokens.pop_back();
    }

    if (tokens.empty()) {
        std::cerr << "syntax error: missing command\n";
        return pipe;
    }

    for (const Token &token : tokens) {
        if (token.kind == TokenKind::Sequence ||
            token.kind == TokenKind::Background ||
            token.kind == TokenKind::AndIf || token.kind == TokenKind::OrIf) {
            std::cerr << "syntax error: unexpected token " << token.text
                      << "\n";
            return pipe;
        }
    }

    if (!parse_pipeline_tokens(tokens, work, pipe)) {
        return pipe;
    }

    for (Command &cmd : pipe.commands) {
        cmd.background = pipe.background;
    }

    return pipe;
}

CommandList parse_command_line(const std::string &line) {
    CommandList list{};
    list.valid = false;

    std::string work = shell::trim(line);
    if (work.empty()) {
        return list;
    }

    std::vector<Token> tokens;
    if (!tokenize_line(work, tokens)) {
        return list;
    }

    if (tokens.empty()) {
        return list;
    }

    std::vector<Token> current_and_or_tokens;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token &token = tokens[i];

        if (token.kind != TokenKind::Sequence &&
            token.kind != TokenKind::Background) {
            current_and_or_tokens.push_back(token);
            continue;
        }

        if (current_and_or_tokens.empty()) {
            if (token.kind == TokenKind::Sequence && i == tokens.size() - 1) {
                list.valid = true;
                return list;
            }

            std::cerr << "syntax error: missing command before " << token.text
                      << "\n";
            return list;
        }

        ConditionalPipeline and_or{};
        if (!parse_and_or_tokens(current_and_or_tokens, work, and_or)) {
            return list;
        }

        and_or.background = (token.kind == TokenKind::Background);
        for (Pipeline &pipe : and_or.pipelines) {
            pipe.background = and_or.background;
            for (Command &cmd : pipe.commands) {
                cmd.background = pipe.background;
            }
        }

        list.and_or_pipelines.push_back(and_or);
        current_and_or_tokens.clear();
    }

    if (current_and_or_tokens.empty()) {
        if (!tokens.empty() && tokens.back().kind == TokenKind::Sequence) {
            list.valid = true;
            return list;
        }

        if (!tokens.empty() && tokens.back().kind == TokenKind::Background) {
            list.valid = true;
            return list;
        }

        std::cerr << "syntax error: missing command\n";
        return list;
    }

    ConditionalPipeline and_or{};
    if (!parse_and_or_tokens(current_and_or_tokens, work, and_or)) {
        return list;
    }

    list.and_or_pipelines.push_back(and_or);
    list.valid = true;
    return list;
}

} // namespace parser
