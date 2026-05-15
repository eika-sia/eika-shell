#include "parser.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../shell/diagnostics/diagnostics.hpp"
#include "./assignments/assignment.hpp"
#include "./lexer/lexer.hpp"
#include "ast.hpp"

namespace parser {
namespace {

struct ParserState {
    const std::string &source;
    std::vector<Token> tokens;
    size_t current = 0;
    bool ok = true;
    std::vector<shell::diagnostics::Diagnostic> diagnostics;
};

SourceSpan eof_span(const ParserState &p) {
    return SourceSpan{p.source.size(), p.source.size()};
}

SourceSpan error_span(const ParserState &p) {
    return p.tokens.empty() || p.current >= p.tokens.size()
               ? eof_span(p)
               : p.tokens[p.current].span;
}

const Token &peek(const ParserState &p) {
    if (p.current < p.tokens.size()) {
        return p.tokens[p.current];
    }

    return p.tokens.back();
}

const Token &previous(const ParserState &p) {
    if (p.current == 0) {
        return p.tokens.front();
    }

    return p.tokens[p.current - 1];
}

bool at_end(const ParserState &p) {
    return p.tokens.empty() || peek(p).kind == TokenKind::EndOfFile;
}

bool check(const ParserState &p, TokenKind kind) {
    return !p.tokens.empty() && peek(p).kind == kind;
}

const Token &advance(ParserState &p) {
    if (p.current < p.tokens.size()) {
        ++p.current;
    }

    return previous(p);
}

bool match(ParserState &p, TokenKind kind) {
    if (!check(p, kind)) {
        return false;
    }

    advance(p);
    return true;
}

void add_error(ParserState &p, SourceSpan span, std::string message) {
    p.ok = false;
    shell::diagnostics::add_error(p.diagnostics, span, std::move(message));
}

std::optional<Token> consume(ParserState &p, TokenKind kind,
                             const std::string &message) {
    if (check(p, kind)) {
        return advance(p);
    }

    add_error(p, error_span(p), message);
    return std::nullopt;
}

bool check_keyword(const ParserState &p, const std::string &keyword) {
    return !at_end(p) && peek(p).kind == TokenKind::Word &&
           peek(p).text == keyword;
}

bool check_any_keyword(const ParserState &p,
                       const std::vector<std::string> &keywords) {
    if (at_end(p) || peek(p).kind != TokenKind::Word) {
        return false;
    }

    for (const std::string &keyword : keywords) {
        if (peek(p).text == keyword) {
            return true;
        }
    }

    return false;
}

std::optional<Token> consume_keyword(ParserState &p, const std::string &keyword,
                                     const std::string &message) {
    if (check_keyword(p, keyword)) {
        return advance(p);
    }

    add_error(p, error_span(p), message);
    return std::nullopt;
}

bool is_reserved_word(const std::string &text) {
    return text == "fn" || text == "if" || text == "else" || text == "while" ||
           text == "for" || text == "in" || text == "with" || text == "hook" ||
           text == "return" || text == "break" || text == "continue" ||
           text == "end";
}

std::optional<Token> consume_identifier(ParserState &p,
                                        const std::string &message) {
    if (check(p, TokenKind::Word) && !is_reserved_word(peek(p).text)) {
        return advance(p);
    }

    add_error(p, error_span(p), message);
    return std::nullopt;
}

bool is_terminator(TokenKind kind) {
    return kind == TokenKind::Newline || kind == TokenKind::Semicolon;
}

bool check_terminator(const ParserState &p) {
    return !at_end(p) && is_terminator(peek(p).kind);
}

std::optional<SourceSpan> consume_terminators(ParserState &p) {
    if (!check_terminator(p)) {
        return std::nullopt;
    }

    SourceSpan span = advance(p).span;
    while (check_terminator(p)) {
        span = ast::merge_spans(span, advance(p).span);
    }

    return span;
}

void skip_terminators(ParserState &p) {
    while (check_terminator(p)) {
        advance(p);
    }
}

void synchronize_statement(ParserState &p,
                           const std::vector<std::string> &stop_keywords) {
    while (!at_end(p)) {
        if (check_terminator(p)) {
            consume_terminators(p);
            return;
        }

        if (check_any_keyword(p, stop_keywords)) {
            return;
        }

        advance(p);
    }
}

SourceSpan previous_span_or_eof(const ParserState &p) {
    return p.current == 0 ? eof_span(p) : previous(p).span;
}

ast::Identifier make_identifier(const Token &token) {
    return ast::Identifier{token.text, token.raw_text, token.span};
}

ast::Word make_word(const Token &token) {
    return ast::Word{token.text, token.raw_text, token.span};
}

void include_span(std::optional<SourceSpan> &span, SourceSpan next) {
    span = span ? ast::merge_spans(*span, next) : next;
}

std::optional<ast::Word> parse_word(ParserState &p,
                                    const std::string &message) {
    if (!check(p, TokenKind::Word)) {
        add_error(p, error_span(p), message);
        return std::nullopt;
    }

    return make_word(advance(p));
}

std::optional<ast::Assignment> parse_assignment(ParserState &p) {
    if (!check(p, TokenKind::Assignment)) {
        return std::nullopt;
    }

    Token token = advance(p);
    const size_t equals = token.text.find('=');
    if (equals == std::string::npos) {
        add_error(p, token.span, "internal: malformed assignment token");
        return std::nullopt;
    }

    const size_t raw_equals = token.raw_text.find('=');
    const std::string name_text = token.text.substr(0, equals);
    const std::string value_text = token.text.substr(equals + 1);
    const std::string name_raw = raw_equals == std::string::npos
                                     ? name_text
                                     : token.raw_text.substr(0, raw_equals);
    const std::string value_raw = raw_equals == std::string::npos
                                      ? value_text
                                      : token.raw_text.substr(raw_equals + 1);

    ast::Assignment assignment{};
    assignment.name = ast::Identifier{
        name_text, name_raw,
        SourceSpan{token.span.start, token.span.start + equals}};
    assignment.value =
        ast::Word{value_text, value_raw,
                  SourceSpan{token.span.start + equals + 1, token.span.end}};
    assignment.equals_span =
        SourceSpan{token.span.start + equals, token.span.start + equals + 1};
    assignment.span = token.span;
    return assignment;
}

std::optional<ast::Assignment> assignment_from_word(const ast::Word &word) {
    std::string name;
    std::string value;
    if (!split_assignment_expression(word.text, name, value)) {
        return std::nullopt;
    }

    const size_t equals = word.text.find('=');
    const size_t raw_equals = word.raw_text.find('=');
    const size_t span_equals =
        raw_equals == std::string::npos ? equals : raw_equals;

    ast::Assignment assignment{};
    assignment.name = ast::Identifier{
        name,
        raw_equals == std::string::npos ? name
                                        : word.raw_text.substr(0, raw_equals),
        SourceSpan{word.span.start, word.span.start + span_equals}};
    assignment.value = ast::Word{
        value,
        raw_equals == std::string::npos ? value
                                        : word.raw_text.substr(raw_equals + 1),
        SourceSpan{word.span.start + span_equals + 1, word.span.end}};
    assignment.equals_span = SourceSpan{word.span.start + span_equals,
                                        word.span.start + span_equals + 1};
    assignment.span = word.span;
    return assignment;
}

std::optional<ast::Redirect> parse_redirect(ParserState &p) {
    if (at_end(p) || !is_redirect(peek(p).kind)) {
        return std::nullopt;
    }

    Token op = advance(p);
    std::optional<ast::Word> target =
        parse_word(p, "syntax: expected word after redirect operator");
    if (!target) {
        return std::nullopt;
    }

    ast::Redirect redirect{};
    switch (op.kind) {
    case TokenKind::InputRedirect:
        redirect.kind = ast::RedirectKind::Input;
        break;
    case TokenKind::OutputRedirect:
        redirect.kind = ast::RedirectKind::Output;
        break;
    case TokenKind::AppendRedirect:
        redirect.kind = ast::RedirectKind::AppendOutput;
        break;
    default:
        break;
    }

    redirect.operator_span = op.span;
    redirect.target = std::move(*target);
    redirect.span = ast::merge_spans(op.span, redirect.target.span);
    return redirect;
}

bool is_command_boundary(const ParserState &p) {
    return at_end(p) || check_terminator(p) || check(p, TokenKind::Pipe) ||
           check(p, TokenKind::AndIf) || check(p, TokenKind::OrIf) ||
           check(p, TokenKind::Background) || check_keyword(p, "else") ||
           check_keyword(p, "end");
}

void parse_call_arguments(ParserState &p, ast::CommandInvocation &invocation,
                          std::optional<SourceSpan> &command_span) {
    Token open = advance(p);
    invocation.open_paren_span = open.span;
    invocation.span = ast::merge_spans(invocation.span, open.span);
    include_span(command_span, open.span);

    if (match(p, TokenKind::RightParen)) {
        invocation.close_paren_span = previous(p).span;
        invocation.span = ast::merge_spans(invocation.span, previous(p).span);
        include_span(command_span, previous(p).span);
        return;
    }

    while (!at_end(p) && !check_terminator(p)) {
        std::optional<ast::Word> arg =
            parse_word(p, "syntax: expected function call argument");
        if (!arg) {
            return;
        }

        include_span(command_span, arg->span);
        invocation.span = ast::merge_spans(invocation.span, arg->span);
        invocation.words.push_back(std::move(*arg));

        if (!match(p, TokenKind::Comma)) {
            break;
        }

        include_span(command_span, previous(p).span);
        if (check(p, TokenKind::RightParen)) {
            add_error(p, peek(p).span,
                      "syntax: expected function call argument after ','");
            break;
        }
    }

    std::optional<Token> close =
        consume(p, TokenKind::RightParen,
                "syntax: expected ')' after function call arguments");
    if (close) {
        invocation.close_paren_span = close->span;
        invocation.span = ast::merge_spans(invocation.span, close->span);
        include_span(command_span, close->span);
    }
}

void parse_command_word(ParserState &p, ast::SimpleCommand &command,
                        std::optional<SourceSpan> &span) {
    ast::Word word = make_word(advance(p));
    include_span(span, word.span);

    if (!command.invocation) {
        const bool paren_call = check(p, TokenKind::LeftParen) &&
                                word.span.end == peek(p).span.start;
        ast::CommandInvocation invocation{};
        invocation.style = paren_call ? ast::CommandInvocationStyle::ParenCall
                                      : ast::CommandInvocationStyle::ShellWords;
        invocation.span = word.span;
        invocation.words.push_back(std::move(word));
        command.invocation = std::move(invocation);

        if (paren_call) {
            parse_call_arguments(p, *command.invocation, span);
        }
        return;
    }

    command.invocation->span =
        ast::merge_spans(command.invocation->span, word.span);
    command.invocation->words.push_back(std::move(word));
}

void parse_with_prefix(ParserState &p, ast::SimpleCommand &command,
                       std::optional<SourceSpan> &span) {
    Token keyword = advance(p);
    command.explicit_with = true;
    include_span(span, keyword.span);

    size_t assignment_count = 0;
    while (!is_command_boundary(p)) {
        if (std::optional<ast::Assignment> assignment = parse_assignment(p)) {
            include_span(span, assignment->span);
            command.assignments.push_back(std::move(*assignment));
            ++assignment_count;
            continue;
        }

        if (!check(p, TokenKind::Word)) {
            break;
        }

        ast::Word word = make_word(peek(p));
        std::optional<ast::Assignment> assignment = assignment_from_word(word);
        if (!assignment) {
            break;
        }

        if (!is_valid_variable_name(assignment->name.text)) {
            add_error(p, assignment->name.span,
                      "with: expected valid assignment name");
            advance(p);
            continue;
        }

        advance(p);
        include_span(span, assignment->span);
        command.assignments.push_back(std::move(*assignment));
        ++assignment_count;
    }

    if (assignment_count == 0) {
        add_error(p, error_span(p), "with: expected assignment before command");
    }

    if (is_command_boundary(p) || !check(p, TokenKind::Word)) {
        add_error(
            p, is_command_boundary(p) ? previous_span_or_eof(p) : error_span(p),
            "with: expected command after assignments");
    }
}

std::optional<ast::SimpleCommand> parse_simple_command(ParserState &p) {
    ast::SimpleCommand command{};
    std::optional<SourceSpan> span;

    if (check_keyword(p, "with")) {
        parse_with_prefix(p, command, span);
    }

    while (!is_command_boundary(p)) {
        if (command.invocation &&
            command.invocation->style ==
                ast::CommandInvocationStyle::ParenCall &&
            !is_redirect(peek(p).kind)) {
            break;
        }

        if (std::optional<ast::Assignment> assignment = parse_assignment(p)) {
            include_span(span, assignment->span);
            command.assignments.push_back(std::move(*assignment));
            continue;
        }

        if (std::optional<ast::Redirect> redirect = parse_redirect(p)) {
            include_span(span, redirect->span);
            command.redirects.push_back(std::move(*redirect));
            continue;
        }

        if (!check(p, TokenKind::Word)) {
            break;
        }

        parse_command_word(p, command, span);
    }

    if (!span) {
        return std::nullopt;
    }

    command.span = *span;
    return command;
}

std::optional<ast::Pipeline> parse_pipeline(ParserState &p) {
    std::optional<ast::SimpleCommand> first = parse_simple_command(p);
    if (!first) {
        return std::nullopt;
    }

    ast::Pipeline pipeline{};
    pipeline.span = first->span;
    pipeline.commands.push_back(std::move(*first));

    while (match(p, TokenKind::Pipe)) {
        SourceSpan pipe_span = previous(p).span;
        std::optional<ast::SimpleCommand> next = parse_simple_command(p);
        if (!next) {
            add_error(p, error_span(p), "syntax: expected command after '|'");
            break;
        }

        pipeline.pipe_spans.push_back(pipe_span);
        pipeline.span = ast::merge_spans(pipeline.span, next->span);
        pipeline.commands.push_back(std::move(*next));
    }

    return pipeline;
}

ast::CommandChain parse_command_chain(ParserState &p) {
    ast::CommandChain chain{};

    std::optional<ast::Pipeline> first = parse_pipeline(p);
    if (!first) {
        add_error(p, error_span(p), "syntax: expected command");
        chain.span = at_end(p)
                         ? eof_span(p)
                         : SourceSpan{peek(p).span.start, peek(p).span.start};
        return chain;
    }

    ast::ConditionalPipeline first_conditional{};
    first_conditional.pipeline = std::move(*first);
    first_conditional.span = first_conditional.pipeline.span;
    chain.span = first_conditional.span;
    chain.pipelines.push_back(std::move(first_conditional));

    while (check(p, TokenKind::AndIf) || check(p, TokenKind::OrIf)) {
        Token op = advance(p);
        std::optional<ast::Pipeline> next = parse_pipeline(p);
        if (!next) {
            add_error(p, error_span(p),
                      op.kind == TokenKind::AndIf
                          ? "syntax: expected command after '&&'"
                          : "syntax: expected command after '||'");
            break;
        }

        ast::ConditionalPipeline conditional{};
        conditional.condition = op.kind == TokenKind::AndIf
                                    ? ast::ChainCondition::IfPreviousSucceeded
                                    : ast::ChainCondition::IfPreviousFailed;
        conditional.operator_span = op.span;
        conditional.pipeline = std::move(*next);
        conditional.span = ast::merge_spans(op.span, conditional.pipeline.span);

        chain.span = ast::merge_spans(chain.span, conditional.span);
        chain.pipelines.push_back(std::move(conditional));
    }

    if (match(p, TokenKind::Background)) {
        chain.background = true;
        chain.background_span = previous(p).span;
        chain.span = ast::merge_spans(chain.span, previous(p).span);
    }

    return chain;
}

ast::Block parse_block(ParserState &p);

std::vector<ast::Identifier> parse_params(ParserState &p) {
    std::vector<ast::Identifier> params;
    if (check(p, TokenKind::RightParen)) {
        return params;
    }

    while (!at_end(p)) {
        std::optional<Token> param =
            consume_identifier(p, "syntax: expected parameter name");
        if (!param) {
            return params;
        }

        params.push_back(make_identifier(*param));
        if (!match(p, TokenKind::Comma)) {
            break;
        }

        if (check(p, TokenKind::RightParen)) {
            add_error(p, peek(p).span,
                      "syntax: expected parameter name after ','");
            break;
        }
    }

    return params;
}

ast::StatementPtr parse_function_declaration(ParserState &p) {
    ast::FunctionDecl func{};

    std::optional<Token> keyword =
        consume_keyword(p, "fn", "syntax: expected 'fn'");
    if (!keyword) {
        return nullptr;
    }
    func.keyword_span = keyword->span;

    std::optional<Token> name =
        consume_identifier(p, "syntax: expected function name");
    if (!name) {
        return nullptr;
    }
    func.name = make_identifier(*name);

    if (!consume(p, TokenKind::LeftParen,
                 "syntax: expected '(' to start parameter list")) {
        return nullptr;
    }

    func.params = parse_params(p);

    if (!consume(p, TokenKind::RightParen,
                 "syntax: expected ')' to end parameter list")) {
        return nullptr;
    }

    if (!consume_terminators(p)) {
        add_error(p, error_span(p),
                  "expected newline or ';' after function declaration");
        return nullptr;
    }

    func.body = parse_block(p);

    std::optional<Token> end =
        consume_keyword(p, "end", "syntax: expected 'end' after function body");
    func.end_span = end ? end->span : previous_span_or_eof(p);
    func.span = ast::merge_spans(func.keyword_span, func.end_span);
    const SourceSpan span = func.span;
    return ast::make_statement(std::move(func), span);
}

ast::StatementPtr parse_if_statement(ParserState &p) {
    ast::IfStmt stmt{};

    std::optional<Token> keyword =
        consume_keyword(p, "if", "syntax: expected 'if'");
    if (!keyword) {
        return nullptr;
    }
    stmt.keyword_span = keyword->span;

    stmt.condition = parse_command_chain(p);
    if (!consume_terminators(p)) {
        add_error(p, error_span(p),
                  "syntax: expected newline or ';' after if condition");
        return nullptr;
    }

    stmt.then_block = parse_block(p);
    if (check_keyword(p, "else")) {
        stmt.else_span = advance(p).span;
        if (!consume_terminators(p)) {
            add_error(p, error_span(p),
                      "syntax: expected newline or ';' after else");
            return nullptr;
        }
        stmt.else_block = parse_block(p);
    }

    std::optional<Token> end =
        consume_keyword(p, "end", "syntax: expected 'end' after if statement");
    stmt.end_span = end ? end->span : previous_span_or_eof(p);
    stmt.span = ast::merge_spans(stmt.keyword_span, stmt.end_span);
    const SourceSpan span = stmt.span;
    return ast::make_statement(std::move(stmt), span);
}

ast::StatementPtr parse_while_statement(ParserState &p) {
    ast::WhileStmt stmt{};

    std::optional<Token> keyword =
        consume_keyword(p, "while", "syntax: expected 'while'");
    if (!keyword) {
        return nullptr;
    }
    stmt.keyword_span = keyword->span;

    stmt.condition = parse_command_chain(p);
    if (!consume_terminators(p)) {
        add_error(p, error_span(p),
                  "syntax: expected newline or ';' after while condition");
        return nullptr;
    }

    stmt.body = parse_block(p);

    std::optional<Token> end = consume_keyword(
        p, "end", "syntax: expected 'end' after while statement");
    stmt.end_span = end ? end->span : previous_span_or_eof(p);
    stmt.span = ast::merge_spans(stmt.keyword_span, stmt.end_span);
    const SourceSpan span = stmt.span;
    return ast::make_statement(std::move(stmt), span);
}

ast::StatementPtr parse_for_statement(ParserState &p) {
    ast::ForStmt stmt{};

    std::optional<Token> keyword =
        consume_keyword(p, "for", "syntax: expected 'for'");
    if (!keyword) {
        return nullptr;
    }
    stmt.keyword_span = keyword->span;

    std::optional<Token> variable =
        consume_identifier(p, "syntax: expected loop variable");
    if (!variable) {
        return nullptr;
    }
    stmt.variable = make_identifier(*variable);

    std::optional<Token> in =
        consume_keyword(p, "in", "syntax: expected 'in' after loop variable");
    if (!in) {
        return nullptr;
    }
    stmt.in_span = in->span;

    while (!at_end(p) && !check_terminator(p) && check(p, TokenKind::Word)) {
        stmt.words.push_back(make_word(advance(p)));
    }

    if (!consume_terminators(p)) {
        add_error(p, error_span(p),
                  "syntax: expected newline or ';' after for word list");
        return nullptr;
    }

    stmt.body = parse_block(p);

    std::optional<Token> end =
        consume_keyword(p, "end", "syntax: expected 'end' after for statement");
    stmt.end_span = end ? end->span : previous_span_or_eof(p);
    stmt.span = ast::merge_spans(stmt.keyword_span, stmt.end_span);
    const SourceSpan span = stmt.span;
    return ast::make_statement(std::move(stmt), span);
}

ast::StatementPtr parse_hook_statement(ParserState &p) {
    ast::HookStmt stmt{};

    std::optional<Token> keyword =
        consume_keyword(p, "hook", "syntax: expected 'hook'");
    if (!keyword) {
        return nullptr;
    }
    stmt.keyword_span = keyword->span;

    std::optional<Token> event =
        consume_identifier(p, "syntax: expected hook event name");
    if (!event) {
        return nullptr;
    }
    stmt.event = make_identifier(*event);

    std::optional<Token> target =
        consume_identifier(p, "syntax: expected hook target name");
    if (!target) {
        return nullptr;
    }
    stmt.target = make_identifier(*target);
    stmt.span = ast::merge_spans(stmt.keyword_span, stmt.target.span);
    const SourceSpan span = stmt.span;
    return ast::make_statement(std::move(stmt), span);
}

ast::StatementPtr parse_return_statement(ParserState &p) {
    ast::ReturnStmt stmt{};

    std::optional<Token> keyword =
        consume_keyword(p, "return", "syntax: expected 'return'");
    if (!keyword) {
        return nullptr;
    }
    stmt.keyword_span = keyword->span;

    if (check(p, TokenKind::Word)) {
        stmt.value = make_word(advance(p));
    }

    stmt.span = stmt.value
                    ? ast::merge_spans(stmt.keyword_span, stmt.value->span)
                    : stmt.keyword_span;
    const SourceSpan span = stmt.span;
    return ast::make_statement(std::move(stmt), span);
}

ast::StatementPtr parse_break_statement(ParserState &p) {
    std::optional<Token> keyword =
        consume_keyword(p, "break", "syntax: expected 'break'");
    if (!keyword) {
        return nullptr;
    }

    ast::BreakStmt stmt{keyword->span, keyword->span};
    const SourceSpan span = stmt.span;
    return ast::make_statement(std::move(stmt), span);
}

ast::StatementPtr parse_continue_statement(ParserState &p) {
    std::optional<Token> keyword =
        consume_keyword(p, "continue", "syntax: expected 'continue'");
    if (!keyword) {
        return nullptr;
    }

    ast::ContinueStmt stmt{keyword->span, keyword->span};
    const SourceSpan span = stmt.span;
    return ast::make_statement(std::move(stmt), span);
}

ast::StatementPtr parse_statement(ParserState &p) {
    if (check_keyword(p, "fn")) {
        return parse_function_declaration(p);
    }
    if (check_keyword(p, "if")) {
        return parse_if_statement(p);
    }
    if (check_keyword(p, "while")) {
        return parse_while_statement(p);
    }
    if (check_keyword(p, "for")) {
        return parse_for_statement(p);
    }
    if (check_keyword(p, "hook")) {
        return parse_hook_statement(p);
    }
    if (check_keyword(p, "return")) {
        return parse_return_statement(p);
    }
    if (check_keyword(p, "break")) {
        return parse_break_statement(p);
    }
    if (check_keyword(p, "continue")) {
        return parse_continue_statement(p);
    }

    ast::CommandChain chain = parse_command_chain(p);
    if (chain.pipelines.empty()) {
        return nullptr;
    }

    const SourceSpan span = chain.span;
    return ast::make_statement(std::move(chain), span);
}

std::vector<ast::StatementPtr>
parse_statement_list(ParserState &p,
                     const std::vector<std::string> &stop_keywords = {}) {
    std::vector<ast::StatementPtr> statements;
    skip_terminators(p);

    while (!at_end(p) && !check_any_keyword(p, stop_keywords)) {
        const size_t before_statement = p.current;
        ast::StatementPtr statement = parse_statement(p);
        if (!statement) {
            synchronize_statement(p, stop_keywords);
            if (p.current == before_statement && !at_end(p) &&
                !check_any_keyword(p, stop_keywords)) {
                advance(p);
            }
            skip_terminators(p);
            continue;
        }

        statements.push_back(std::move(statement));

        if (at_end(p) || check_any_keyword(p, stop_keywords)) {
            break;
        }

        if (!consume_terminators(p)) {
            const size_t before_recovery = p.current;
            add_error(p, error_span(p),
                      "expected newline or ';' after statement");
            synchronize_statement(p, stop_keywords);
            if (p.current == before_recovery && !at_end(p) &&
                !check_any_keyword(p, stop_keywords)) {
                advance(p);
            }
        }

        skip_terminators(p);
    }

    return statements;
}

ast::Block parse_block(ParserState &p) {
    const SourceSpan start =
        at_end(p) ? eof_span(p)
                  : SourceSpan{peek(p).span.start, peek(p).span.start};

    ast::Block block{};
    block.statements = parse_statement_list(p, {"else", "end"});
    block.span = block.statements.empty()
                     ? start
                     : ast::merge_spans(start, block.statements.back()->span);
    return block;
}

} // namespace

ast::ParseResult parse_program(const std::string &source) {
    LexResult lexer_res = lex(source);
    ast::Program program{};
    program.span = SourceSpan{0, source.size()};

    if (!lexer_res.ok) {
        return ast::ParseResult{std::move(program), false,
                                std::move(lexer_res.diagnostics)};
    }

    ParserState p = {source, std::move(lexer_res.tokens), 0, true,
                     std::move(lexer_res.diagnostics)};

    program.statements = parse_statement_list(p);
    skip_terminators(p);

    if (!consume(p, TokenKind::EndOfFile, "expected end of file")) {
        synchronize_statement(p, {});
    }

    return ast::ParseResult{std::move(program), p.ok, std::move(p.diagnostics)};
}

} // namespace parser
