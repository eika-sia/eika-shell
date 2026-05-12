#include "alias.hpp"

#include <cctype>
#include <iostream>
#include <string>
#include <utility>

#include "../../parser/parser.hpp"
#include "../../shell/diagnostics/diagnostics.hpp"

namespace builtins {

namespace {

int run_alias_set(shell::ShellState &state,
                  const parser::ast::SimpleCommand &cmd,
                  std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (!cmd.invocation || cmd.invocation->words.size() != 2) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "alias: invalid format");
        return 1;
    }

    std::string expr = cmd.invocation->words[1].text;

    size_t eq_pos = expr.find('=');
    if (eq_pos == std::string::npos) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "alias: invalid format");
        return 1;
    }

    std::string name = expr.substr(0, eq_pos);
    std::string value = expr.substr(eq_pos + 1);

    if (name.empty()) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "alias: empty name");
        return 1;
    }

    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            shell::diagnostics::add_error(diagnostics, cmd.span,
                                          "alias: invalid name");
            return 1;
        }
    }

    state.alias[name] = value;
    return 0;
}

int run_alias_unset(shell::ShellState &state,
                    const parser::ast::SimpleCommand &cmd,
                    std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (!cmd.invocation || cmd.invocation->words.size() != 2) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "unalias: invalid format");
        return 1;
    }

    std::string name = cmd.invocation->words[1].text;

    if (state.alias.count(name) == 0) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "unalias: alias doesn't exist");
        return 1;
    }

    state.alias.erase(name);

    return 0;
}

void append_part(std::string &out, const std::string &part) {
    if (part.empty()) {
        return;
    }

    if (!out.empty()) {
        out += ' ';
    }

    out += part;
}

void append_invocation(const parser::ast::CommandInvocation &invocation,
                       std::string &out) {
    if (invocation.words.empty()) {
        return;
    }

    if (invocation.style == parser::ast::CommandInvocationStyle::ParenCall) {
        std::string call = invocation.words.front().raw_text + "(";
        for (size_t i = 1; i < invocation.words.size(); ++i) {
            if (i > 1) {
                call += ", ";
            }
            call += invocation.words[i].raw_text;
        }
        call += ")";
        append_part(out, call);
        return;
    }

    for (const parser::ast::Word &word : invocation.words) {
        append_part(out, word.raw_text);
    }
}

void append_invocation_with_aliases(
    const shell::ShellState &state,
    const parser::ast::CommandInvocation &invocation, std::string &out,
    bool &changed) {
    if (invocation.words.empty() ||
        invocation.style != parser::ast::CommandInvocationStyle::ShellWords) {
        append_invocation(invocation, out);
        return;
    }

    const parser::ast::Word &name = invocation.words.front();
    if (name.raw_text != name.text) {
        append_invocation(invocation, out);
        return;
    }

    const auto alias = state.alias.find(name.text);
    if (alias == state.alias.end()) {
        append_invocation(invocation, out);
        return;
    }

    changed = true;
    append_part(out, alias->second);

    for (size_t i = 1; i < invocation.words.size(); ++i) {
        append_part(out, invocation.words[i].raw_text);
    }
}

void append_simple_command(const shell::ShellState &state,
                           const parser::ast::SimpleCommand &cmd,
                           std::string &out, bool &changed) {
    for (const parser::ast::Assignment &assignment : cmd.assignments) {
        append_part(out,
                    assignment.name.raw_text + "=" + assignment.value.raw_text);
    }

    if (cmd.invocation) {
        append_invocation_with_aliases(state, *cmd.invocation, out, changed);
    }

    for (const parser::ast::Redirect &redirect : cmd.redirects) {
        switch (redirect.kind) {
        case parser::ast::RedirectKind::Input:
            append_part(out, "<");
            break;
        case parser::ast::RedirectKind::Output:
            append_part(out, ">");
            break;
        case parser::ast::RedirectKind::AppendOutput:
            append_part(out, ">>");
            break;
        }
        append_part(out, redirect.target.raw_text);
    }
}

std::string expanded_chain_text(const shell::ShellState &state,
                                const parser::ast::CommandChain &chain,
                                bool &changed) {
    std::string text;

    for (size_t i = 0; i < chain.pipelines.size(); ++i) {
        const parser::ast::ConditionalPipeline &conditional =
            chain.pipelines[i];

        if (i > 0) {
            switch (conditional.condition) {
            case parser::ast::ChainCondition::IfPreviousSucceeded:
                text += " && ";
                break;
            case parser::ast::ChainCondition::IfPreviousFailed:
                text += " || ";
                break;
            case parser::ast::ChainCondition::Always:
                break;
            }
        }

        for (size_t j = 0; j < conditional.pipeline.commands.size(); ++j) {
            if (j > 0) {
                text += " | ";
            }

            append_simple_command(state, conditional.pipeline.commands[j], text,
                                  changed);
        }
    }

    if (chain.background) {
        text += " &";
    }

    return text;
}

std::string
expanded_chains_text(const shell::ShellState &state,
                     const std::vector<parser::ast::CommandChain> &chains,
                     bool &changed) {
    std::string text;

    for (const parser::ast::CommandChain &chain : chains) {
        if (!text.empty()) {
            text += "; ";
        }

        text += expanded_chain_text(state, chain, changed);
    }

    return text;
}

} // namespace

int run_alias_list(shell::ShellState &state,
                   const parser::ast::SimpleCommand &cmd,
                   std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (!cmd.invocation || cmd.invocation->words.size() != 1) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "alias: internal dispatch error");
        return 1;
    }

    for (const auto &[name, value] : state.alias) {
        std::cout << name << "=\"" << value << "\"\n";
    }

    return 0;
}

int run_alias_manage(shell::ShellState &state,
                     const parser::ast::SimpleCommand &cmd,
                     std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (!cmd.invocation || cmd.invocation->words.empty()) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "alias: invalid command");
        return 1;
    }

    if (cmd.invocation->words[0].text == "alias")
        return run_alias_set(state, cmd, diagnostics);
    if (cmd.invocation->words[0].text == "unalias")
        return run_alias_unset(state, cmd, diagnostics);

    shell::diagnostics::add_error(diagnostics, cmd.span,
                                  "alias: internal dispatch error");
    return 1;
}

bool expand_aliases(const shell::ShellState &state,
                    const parser::ast::CommandChain &chain,
                    std::vector<parser::ast::CommandChain> &chains,
                    std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    // Aliases stay textual so they can inject shell operators
    // reparsing each step ensures structure and validity while the depth cap
    // stops loops.
    chains = {chain};

    for (size_t depth = 0; depth < 16; ++depth) {
        bool changed = false;
        const std::string expanded_text =
            expanded_chains_text(state, chains, changed);
        if (!changed) {
            return true;
        }

        parser::ast::ParseResult expanded =
            parser::parse_program(expanded_text);
        if (!expanded.ok) {
            shell::diagnostics::print_diagnostics(expanded_text,
                                                  expanded.diagnostics);
            return false;
        }

        std::vector<parser::ast::CommandChain> next_chains;
        next_chains.reserve(expanded.program.statements.size());

        for (parser::ast::StatementPtr &statement :
             expanded.program.statements) {
            auto *expanded_chain =
                std::get_if<parser::ast::CommandChain>(&statement->node);
            if (expanded_chain == nullptr) {
                shell::diagnostics::add_error(
                    diagnostics, chain.span,
                    "alias expansion did not produce a command");
                return false;
            }

            next_chains.push_back(std::move(*expanded_chain));
        }

        chains = std::move(next_chains);
    }

    shell::diagnostics::add_error(diagnostics, chain.span,
                                  "alias: expansion loop detected");
    return false;
}

} // namespace builtins
