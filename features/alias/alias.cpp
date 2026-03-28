#include "alias.hpp"

#include <cctype>
#include <iostream>
#include <string>

bool handle_alias_builtin(ShellState &state, const std::string &line) {
    if (line == "alias") {
        for (const auto &[name, value] : state.alias) {
            std::cout << name << "=\"" << value << "\"\n";
        }
        return true;
    }

    if (line.rfind("alias ", 0) != 0) {
        return false;
    }

    // expected format: alias name="value"
    std::string expr = line.substr(6);

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

    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        std::cerr << "alias: value must be in double quotes\n";
        return true;
    }

    value = value.substr(1, value.size() - 2);

    state.alias[name] = value;
    return true;
}

std::string expand_aliases(const ShellState &state, const std::string &line) {
    size_t first_space = line.find(' ');

    std::string first_word;
    std::string rest;

    if (first_space == std::string::npos) {
        first_word = line;
        rest = "";
    } else {
        first_word = line.substr(0, first_space);
        rest = line.substr(first_space);
    }

    auto it = state.alias.find(first_word);
    if (it == state.alias.end()) {
        return line;
    }

    return it->second + rest;
}
