#include "parser.hpp"

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ast.hpp"
#include "parser2.hpp"

namespace parser {
namespace {

std::string source_slice(const std::string &source, SourceSpan span) {
    if (span.start > source.size()) {
        return "";
    }

    const size_t end = std::min(span.end, source.size());
    if (end < span.start) {
        return "";
    }

    return source.substr(span.start, end - span.start);
}

Command lower_simple_command(
    const ast::SimpleCommand &simple, const std::string &source,
    const std::vector<diagnostics::Diagnostic> &diagnostics, bool parse_ok) {
    Command command{};
    command.raw = source_slice(source, simple.span);
    command.valid = parse_ok;

    if (!parse_ok) {
        command.diagnostics = diagnostics;
    }

    if (simple.invocation) {
        for (const ast::Word &word : simple.invocation->words) {
            command.args.push_back(word.text);
        }
    }

    for (const ast::Assignment &assignment : simple.assignments) {
        command.assignments.push_back(
            Assignment{assignment.name.text, assignment.value.text});
    }

    // The old AST stores only one input and one output redirect. Preserve the
    // practical shell-like behavior: the last redirect of each class wins.
    for (const ast::Redirect &redirect : simple.redirects) {
        switch (redirect.kind) {
        case ast::RedirectKind::Input:
            command.input_file = redirect.target.text;
            break;
        case ast::RedirectKind::Output:
            command.output_file = redirect.target.text;
            command.append_output = false;
            break;
        case ast::RedirectKind::AppendOutput:
            command.output_file = redirect.target.text;
            command.append_output = true;
            break;
        }
    }

    if (simple.invocation && !simple.invocation->words.empty()) {
        const ast::Word &name = simple.invocation->words.front();
        command.command_name_offset = name.span.start;
        command.command_name_length = name.span.end - name.span.start;
    }

    return command;
}

ConditionalChain
lower_command_chain(const ast::CommandChain &chain, const std::string &source,
                    const std::vector<diagnostics::Diagnostic> &diagnostics,
                    bool parse_ok) {
    ConditionalChain lowered{};
    lowered.background = chain.background;
    lowered.valid = parse_ok;

    if (!parse_ok) {
        lowered.diagnostics = diagnostics;
    }

    for (const ast::ConditionalPipeline &conditional : chain.pipelines) {
        Pipeline pipeline{};
        pipeline.valid = parse_ok;
        switch (conditional.condition) {
        case ast::ChainCondition::Always:
            pipeline.run_condition = RunCondition::Always;
            break;
        case ast::ChainCondition::IfPreviousSucceeded:
            pipeline.run_condition = RunCondition::IfPreviousSucceeded;
            break;
        case ast::ChainCondition::IfPreviousFailed:
            pipeline.run_condition = RunCondition::IfPreviousFailed;
            break;
        }

        if (!parse_ok) {
            pipeline.diagnostics = diagnostics;
        }

        for (const ast::SimpleCommand &simple : conditional.pipeline.commands) {
            pipeline.commands.push_back(
                lower_simple_command(simple, source, diagnostics, parse_ok));
        }

        lowered.pipelines.push_back(std::move(pipeline));
    }

    return lowered;
}

} // namespace

CommandList parse_command_line(const std::string &line) {
    ast::ParseResult parsed = parse_program(line);

    CommandList out{};
    out.valid = parsed.ok;

    if (!parsed.ok) {
        out.diagnostics = parsed.diagnostics;
    }

    for (const ast::StatementPtr &statement : parsed.program.statements) {
        if (!statement) {
            continue;
        }

        if (const auto *chain =
                std::get_if<ast::CommandChain>(&statement->node)) {
            out.conditional_chains.push_back(
                lower_command_chain(*chain, line, parsed.diagnostics,
                                    parsed.ok));
            continue;
        }

        diagnostics::add_error(
            out.diagnostics, statement->span,
            "compat: statement cannot be represented as a command line");
        out.valid = false;
    }

    if (out.conditional_chains.empty() && !parsed.program.statements.empty()) {
        out.valid = false;
    }

    return out;
}

Pipeline parse_pipeline(const std::string &line) {
    CommandList list = parse_command_line(line);

    if (!list.valid || list.conditional_chains.empty() ||
        list.conditional_chains.front().pipelines.empty()) {
        Pipeline pipeline{};
        pipeline.valid = false;
        pipeline.diagnostics = std::move(list.diagnostics);
        return pipeline;
    }

    Pipeline pipeline =
        std::move(list.conditional_chains.front().pipelines.front());

    // Old parse_pipeline() represented one pipeline only. If the input contains
    // additional conditional pipelines or additional command chains, flag that.
    if (list.conditional_chains.size() > 1 ||
        list.conditional_chains.front().pipelines.size() > 1) {
        pipeline.valid = false;
        diagnostics::add_error(pipeline.diagnostics, SourceSpan{0, line.size()},
                               "compat: input contains more than one pipeline");
    }

    return pipeline;
}

Command parse_command(const std::string &line) {
    Pipeline pipeline = parse_pipeline(line);

    if (!pipeline.valid || pipeline.commands.empty()) {
        Command command{};
        command.valid = false;
        command.diagnostics = std::move(pipeline.diagnostics);
        return command;
    }

    Command command = std::move(pipeline.commands.front());

    // Old parse_command() represented one simple command only. If the input
    // contains a pipe, flag it rather than silently discarding commands.
    if (pipeline.commands.size() > 1) {
        command.valid = false;
        diagnostics::add_error(command.diagnostics, SourceSpan{0, line.size()},
                               "compat: input contains more than one command");
    }

    return command;
}

} // namespace parser
