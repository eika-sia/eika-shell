#include "alias.hpp"

#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>

namespace builtins {

namespace {

int run_alias_set(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() != 2) {
        std::cerr << "alias: invalid format\n";
        return 1;
    }

    std::string expr = cmd.args[1];

    size_t eq_pos = expr.find('=');
    if (eq_pos == std::string::npos) {
        std::cerr << "alias: invalid format\n";
        return 1;
    }

    std::string name = expr.substr(0, eq_pos);
    std::string value = expr.substr(eq_pos + 1);

    if (name.empty()) {
        std::cerr << "alias: empty name\n";
        return 1;
    }

    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            std::cerr << "alias: invalid name\n";
            return 1;
        }
    }

    state.alias[name] = value;
    return 0;
}

int run_alias_unset(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() != 2) {
        std::cerr << "unalias: invalid format\n";
        return 1;
    }

    std::string name = cmd.args[1];

    if (state.alias.count(name) == 0) {
        std::cerr << "unalias: alias doesn't exist\n";
        return 1;
    }

    state.alias.erase(name);

    return 0;
}

std::string expand_alias_in_command_text(const shell::ShellState &state,
                                         const parser::Command &cmd,
                                         bool &changed) {
    if (!cmd.valid || cmd.args.empty() ||
        cmd.command_name_offset == std::string::npos) {
        return cmd.raw;
    }

    const auto it = state.alias.find(cmd.args[0]);
    if (it == state.alias.end()) {
        return cmd.raw;
    }

    changed = true;

    std::string expanded = cmd.raw;
    expanded.replace(cmd.command_name_offset, cmd.command_name_length,
                     it->second);
    return expanded;
}

bool append_conditional_chain_text(const shell::ShellState &state,
                                   const parser::ConditionalChain &chain,
                                   std::string &expanded_text, bool &changed) {
    for (size_t i = 0; i < chain.pipelines.size(); ++i) {
        const parser::Pipeline &pipe = chain.pipelines[i];

        if (i > 0) {
            switch (pipe.run_condition) {
            case parser::RunCondition::IfPreviousSucceeded:
                expanded_text += " && ";
                break;
            case parser::RunCondition::IfPreviousFailed:
                expanded_text += " || ";
                break;
            case parser::RunCondition::Always:
                std::cerr << "alias: invalid conditional chain\n";
                return false;
            }
        }

        for (size_t j = 0; j < pipe.commands.size(); ++j) {
            if (j > 0) {
                expanded_text += " | ";
            }

            expanded_text +=
                expand_alias_in_command_text(state, pipe.commands[j], changed);
        }
    }

    return true;
}

bool build_expanded_list_text(const shell::ShellState &state,
                              const parser::CommandList &list,
                              std::string &expanded_text, bool &changed) {
    expanded_text.clear();
    changed = false;

    for (size_t i = 0; i < list.conditional_chains.size(); ++i) {
        const parser::ConditionalChain &chain = list.conditional_chains[i];
        if (i > 0) {
            expanded_text += chain.background ? " & " : " ; ";
        }

        if (!append_conditional_chain_text(state, chain, expanded_text,
                                           changed)) {
            return false;
        }
    }

    return true;
}

} // namespace

int run_alias_list(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args.size() != 1) {
        std::cerr << "how did we get here part alias?\n";
        return 1;
    }

    for (const auto &[name, value] : state.alias) {
        std::cout << name << "=\"" << value << "\"\n";
    }

    return 0;
}

int run_alias_manage(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args[0] == "alias")
        return run_alias_set(state, cmd);
    if (cmd.args[0] == "unalias")
        return run_alias_unset(state, cmd);

    std::cerr << "alias: what??";
    return 1;
}

bool expand_aliases(const shell::ShellState &state, parser::CommandList &list) {
    // Aliases stay textual so they can inject shell operators
    // reparsing each step esnures structure and validity while the depth cap
    // stops loops.
    for (size_t depth = 0; depth < 16; ++depth) {
        if (!list.valid) {
            return false;
        }

        bool changed = false;
        std::string expanded_text;
        if (!build_expanded_list_text(state, list, expanded_text, changed)) {
            return false;
        }

        if (!changed) {
            return true;
        }

        parser::CommandList expanded =
            parser::parse_command_line(expanded_text);
        if (!expanded.valid) {
            return false;
        }

        list = std::move(expanded);
    }

    std::cerr << "alias: expansion loop detected\n";
    return false;
}

} // namespace builtins
