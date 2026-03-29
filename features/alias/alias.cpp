#include "alias.hpp"

#include <cctype>
#include <iostream>
#include <string>

bool handle_alias_builtin(ShellState &state, const Command &cmd) {
    if (cmd.args.empty() || cmd.args[0] != "alias") {
        return false;
    }

    if (cmd.args.size() == 1) {
        for (const auto &[name, value] : state.alias) {
            std::cout << name << "=\"" << value << "\"\n";
        }
        return true;
    }

    if (cmd.args.size() != 2) {
        std::cerr << "alias: invalid format\n";
        return true;
    }

    std::string expr = cmd.args[1];

    size_t eq_pos = expr.find('=');
    if (eq_pos == std::string::npos) {
        std::cerr << "alias: invalid format\n";
        return true;
    }

    std::string name = expr.substr(0, eq_pos);
    std::string value = expr.substr(eq_pos + 1);

    if (name.empty()) {
        std::cerr << "alias: empty name\n";
        return true;
    }

    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            std::cerr << "alias: invalid name\n";
            return true;
        }
    }

    state.alias[name] = value;
    return true;
}

bool expand_aliases(const ShellState &state, Command &cmd) {
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

        cmd = parse_command(expanded);
        if (!cmd.valid) {
            return false;
        }
    }

    std::cerr << "alias: expansion loop detected\n";
    return false;
}
