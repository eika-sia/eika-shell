#pragma once

#include "../shell/diagnostics/diagnostics.hpp"
#include "source.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace parser::ast {

struct Identifier {
    std::string text;
    std::string raw_text{};
    SourceSpan span{};
};

struct Word {
    std::string text;
    std::string raw_text{};
    SourceSpan span{};
};

struct Assignment {
    Identifier name;
    Word value;
    SourceSpan equals_span{};
    SourceSpan span{};
};

enum class RedirectKind {
    Input,
    Output,
    AppendOutput,
};

struct Redirect {
    RedirectKind kind = RedirectKind::Input;
    SourceSpan operator_span;
    Word target;
    SourceSpan span;
};

enum class CommandInvocationStyle {
    ShellWords,
    ParenCall,
};

struct CommandInvocation {
    CommandInvocationStyle style = CommandInvocationStyle::ShellWords;
    std::vector<Word> words;
    std::optional<SourceSpan> open_paren_span;
    std::optional<SourceSpan> close_paren_span;
    SourceSpan span;
};

struct SimpleCommand {
    bool explicit_with = false;
    std::vector<Assignment> assignments;
    std::optional<CommandInvocation> invocation;
    std::vector<Redirect> redirects;
    SourceSpan span;
};

struct Pipeline {
    std::vector<SimpleCommand> commands;
    std::vector<SourceSpan> pipe_spans;
    SourceSpan span;
};

enum class ChainCondition {
    Always,
    IfPreviousSucceeded,
    IfPreviousFailed,
};

struct ConditionalPipeline {
    ChainCondition condition = ChainCondition::Always;
    std::optional<SourceSpan> operator_span;
    Pipeline pipeline;
    SourceSpan span;
};

struct CommandChain {
    std::vector<ConditionalPipeline> pipelines;
    bool background = false;
    std::optional<SourceSpan> background_span;
    SourceSpan span;
};

struct Statement;
using StatementPtr = std::unique_ptr<Statement>;

struct Block {
    std::vector<StatementPtr> statements;
    SourceSpan span;
};

struct FunctionDecl {
    Identifier name;
    std::vector<Identifier> params;
    Block body;
    SourceSpan keyword_span;
    SourceSpan end_span;
    SourceSpan span;
};

struct IfStmt {
    CommandChain condition;
    Block then_block;
    std::optional<Block> else_block;
    SourceSpan keyword_span;
    std::optional<SourceSpan> else_span;
    SourceSpan end_span;
    SourceSpan span;
};

struct WhileStmt {
    CommandChain condition;
    Block body;
    SourceSpan keyword_span;
    SourceSpan end_span;
    SourceSpan span;
};

struct ForStmt {
    Identifier variable;
    std::vector<Word> words;
    Block body;
    SourceSpan keyword_span;
    SourceSpan in_span;
    SourceSpan end_span;
    SourceSpan span;
};

struct HookStmt {
    Identifier event;
    Identifier target;
    SourceSpan keyword_span;
    SourceSpan span;
};

struct ReturnStmt {
    std::optional<Word> value;
    SourceSpan keyword_span;
    SourceSpan span;
};

struct BreakStmt {
    SourceSpan keyword_span;
    SourceSpan span;
};

struct ContinueStmt {
    SourceSpan keyword_span;
    SourceSpan span;
};

using StatementNode =
    std::variant<FunctionDecl, IfStmt, WhileStmt, ForStmt, HookStmt, ReturnStmt,
                 BreakStmt, ContinueStmt, CommandChain>;

struct Statement {
    StatementNode node;
    SourceSpan span;
};

struct Program {
    std::vector<StatementPtr> statements;
    SourceSpan span;
};

struct ParseResult {
    Program program;
    bool ok = true;
    std::vector<shell::diagnostics::Diagnostic> diagnostics;
};

inline SourceSpan merge_spans(SourceSpan begin, SourceSpan end) {
    return SourceSpan{begin.start, end.end};
}

template <typename Node>
StatementPtr make_statement(Node node, SourceSpan span) {
    return std::make_unique<Statement>(
        Statement{StatementNode{std::move(node)}, span});
}

} // namespace parser::ast
