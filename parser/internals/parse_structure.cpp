#include "internal.hpp"

#include <string>
#include <vector>

namespace parser {

bool parse_pipeline_tokens(const std::vector<Token> &tokens,
                           const std::string &source, Pipeline &pipe,
                           std::vector<diagnostics::Diagnostic> &diagnostics) {
    pipe = Pipeline{};
    pipe.valid = false;

    if (tokens.empty()) {
        diagnostics::add_error(diagnostics, SourceSpan{0, 0},
                               "syntax error: missing command");
        return false;
    }

    std::vector<Token> current_command_tokens;
    SourceSpan last_pipe_span{0, 0};
    for (const Token &token : tokens) {
        if (token.kind != TokenKind::Pipe) {
            current_command_tokens.push_back(token);
            continue;
        }

        last_pipe_span = token.span;
        if (current_command_tokens.empty()) {
            diagnostics::add_error(diagnostics, token.span,
                                   "syntax error: empty pipeline stage");
            return false;
        }

        Command cmd{};
        if (!parse_simple_command(current_command_tokens, source, cmd,
                                  diagnostics)) {
            return false;
        }

        pipe.commands.push_back(cmd);
        current_command_tokens.clear();
    }

    if (current_command_tokens.empty()) {
        diagnostics::add_error(diagnostics, last_pipe_span,
                               "syntax error: empty pipeline stage");
        return false;
    }

    Command cmd{};
    if (!parse_simple_command(current_command_tokens, source, cmd,
                              diagnostics)) {
        return false;
    }

    pipe.commands.push_back(cmd);
    pipe.valid = true;
    return true;
}

bool parse_and_or_tokens(const std::vector<Token> &tokens,
                         const std::string &source, ConditionalChain &chain,
                         std::vector<diagnostics::Diagnostic> &diagnostics) {
    chain = ConditionalChain{};
    chain.valid = false;

    if (tokens.empty()) {
        diagnostics::add_error(diagnostics, SourceSpan{0, 0},
                               "syntax error: missing command");
        return false;
    }

    std::vector<Token> current_pipeline_tokens;
    RunCondition next_condition = RunCondition::Always;
    SourceSpan last_condition_span{0, 0};

    for (const Token &token : tokens) {
        if (token.kind != TokenKind::AndIf && token.kind != TokenKind::OrIf) {
            current_pipeline_tokens.push_back(token);
            continue;
        }

        if (current_pipeline_tokens.empty()) {
            diagnostics::add_error(diagnostics, token.span,
                                   "syntax error: missing command before " +
                                       token.text);
            return false;
        }

        Pipeline pipe{};
        if (!parse_pipeline_tokens(current_pipeline_tokens, source, pipe,
                                   diagnostics)) {
            return false;
        }

        pipe.run_condition = next_condition;
        chain.pipelines.push_back(pipe);
        current_pipeline_tokens.clear();
        last_condition_span = token.span;
        next_condition = (token.kind == TokenKind::AndIf)
                             ? RunCondition::IfPreviousSucceeded
                             : RunCondition::IfPreviousFailed;
    }

    if (current_pipeline_tokens.empty()) {
        diagnostics::add_error(diagnostics, last_condition_span,
                               "syntax error: missing command");
        return false;
    }

    Pipeline pipe{};
    if (!parse_pipeline_tokens(current_pipeline_tokens, source, pipe,
                               diagnostics)) {
        return false;
    }

    pipe.run_condition = next_condition;
    chain.pipelines.push_back(pipe);
    chain.valid = true;
    return true;
}

} // namespace parser
