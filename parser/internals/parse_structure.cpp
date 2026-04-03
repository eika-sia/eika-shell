#include "internal.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace parser {

bool parse_pipeline_tokens(const std::vector<Token> &tokens,
                           const std::string &source, Pipeline &pipe) {
    pipe = Pipeline{};
    pipe.valid = false;

    if (tokens.empty()) {
        std::cerr << "syntax error: missing command\n";
        return false;
    }

    std::vector<Token> current_command_tokens;
    for (const Token &token : tokens) {
        if (token.kind != TokenKind::Pipe) {
            current_command_tokens.push_back(token);
            continue;
        }

        if (current_command_tokens.empty()) {
            std::cerr << "syntax error: empty pipeline stage\n";
            return false;
        }

        Command cmd{};
        if (!parse_simple_command(current_command_tokens, source, cmd)) {
            return false;
        }

        pipe.commands.push_back(cmd);
        current_command_tokens.clear();
    }

    if (current_command_tokens.empty()) {
        std::cerr << "syntax error: empty pipeline stage\n";
        return false;
    }

    Command cmd{};
    if (!parse_simple_command(current_command_tokens, source, cmd)) {
        return false;
    }

    pipe.commands.push_back(cmd);
    pipe.valid = true;
    return true;
}

bool parse_and_or_tokens(const std::vector<Token> &tokens,
                         const std::string &source,
                         ConditionalPipeline &and_or) {
    and_or = ConditionalPipeline{};
    and_or.valid = false;

    if (tokens.empty()) {
        std::cerr << "syntax error: missing command\n";
        return false;
    }

    std::vector<Token> current_pipeline_tokens;
    RunCondition next_condition = RunCondition::Always;

    for (const Token &token : tokens) {
        if (token.kind != TokenKind::AndIf && token.kind != TokenKind::OrIf) {
            current_pipeline_tokens.push_back(token);
            continue;
        }

        if (current_pipeline_tokens.empty()) {
            std::cerr << "syntax error: missing command before " << token.text
                      << "\n";
            return false;
        }

        Pipeline pipe{};
        if (!parse_pipeline_tokens(current_pipeline_tokens, source, pipe)) {
            return false;
        }

        pipe.run_condition = next_condition;
        and_or.pipelines.push_back(pipe);
        current_pipeline_tokens.clear();
        next_condition = (token.kind == TokenKind::AndIf)
                             ? RunCondition::IfPreviousSucceeded
                             : RunCondition::IfPreviousFailed;
    }

    if (current_pipeline_tokens.empty()) {
        std::cerr << "syntax error: missing command\n";
        return false;
    }

    Pipeline pipe{};
    if (!parse_pipeline_tokens(current_pipeline_tokens, source, pipe)) {
        return false;
    }

    pipe.run_condition = next_condition;
    and_or.pipelines.push_back(pipe);
    and_or.valid = true;
    return true;
}

} // namespace parser
