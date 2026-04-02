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
    }

    state.alias.erase(name);

    return 0;
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

bool expand_aliases(const shell::ShellState &state, parser::Command &cmd) {
    for (size_t depth = 0; depth < 16; ++depth) {
        if (!cmd.valid || cmd.args.empty() ||
            cmd.command_name_offset == std::string::npos) {
            return cmd.valid;
        }

        if (state.alias.find(cmd.args[0]) == state.alias.end()) {
            return true;
        }

        std::string expanded = cmd.raw;
        expanded.replace(cmd.command_name_offset, cmd.command_name_length,
                         state.alias.at(cmd.args[0]));

        cmd = parser::parse_command(expanded);
        if (!cmd.valid) {
            return false;
        }
    }

    std::cerr << "alias: expansion loop detected\n";
    return false;
}

} // namespace builtins
