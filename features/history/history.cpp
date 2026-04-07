#include "history.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>

#include "../../builtins/env/env.hpp"
#include "../shell_text/shell_text.hpp"

namespace features {
namespace {

enum class HistoryExpansionResult {
    NoMatch,
    Applied,
    Error,
};

enum class HistoryNumberParseResult {
    NoMatch,
    Parsed,
    Error,
};

std::string resolve_history_path(const shell::ShellState &state) {
    const shell::ShellVariable *home =
        builtins::env::find_variable(state, "HOME");
    if (home == nullptr || home->value.empty()) {
        return "";
    }

    return home->value + "/.eshrc_history";
}

bool history_file_exists(const std::string &path) {
    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

HistoryNumberParseResult parse_history_number(const std::string &line,
                                              size_t start, size_t &end,
                                              int &num) {
    end = start;
    if (end < line.size() && line[end] == '-') {
        ++end;
    }

    if (end >= line.size() ||
        !std::isdigit(static_cast<unsigned char>(line[end]))) {
        return HistoryNumberParseResult::NoMatch;
    }

    while (end < line.size() &&
           std::isdigit(static_cast<unsigned char>(line[end]))) {
        ++end;
    }

    try {
        num = std::stoi(line.substr(start, end - start));
    } catch (const std::invalid_argument &) {
        std::cerr << "history: invalid argument" << std::endl;
        return HistoryNumberParseResult::Error;
    } catch (const std::out_of_range &) {
        std::cerr << "history: number out of range" << std::endl;
        return HistoryNumberParseResult::Error;
    }

    return HistoryNumberParseResult::Parsed;
}

HistoryExpansionResult apply_history_reference(
    const shell::ShellState &state, int num, size_t begin, size_t end,
    shell_text::Replacement &replacement) {
    int index = -1;
    if (num > 0) {
        index = num - 1;
    } else if (num < 0) {
        index = static_cast<int>(state.history.size()) + num;
    }

    if (index < 0 || index >= static_cast<int>(state.history.size())) {
        std::cerr << "history: invalid history number " << num << std::endl;
        return HistoryExpansionResult::Error;
    }

    replacement.begin = begin;
    replacement.end = end;
    replacement.text = state.history[index];
    return HistoryExpansionResult::Applied;
}

HistoryExpansionResult parse_history_expansion(
    const shell::ShellState &state, const std::string &line, size_t begin,
    shell_text::Replacement &replacement) {
    if (begin + 1 >= line.size()) {
        return HistoryExpansionResult::NoMatch;
    }

    size_t end = begin + 1;
    if (line[end] == '!') {
        ++end;
        return apply_history_reference(state,
                                       static_cast<int>(state.history.size()),
                                       begin, end, replacement);
    }

    int num = 0;
    switch (parse_history_number(line, begin + 1, end, num)) {
    case HistoryNumberParseResult::NoMatch:
        return HistoryExpansionResult::NoMatch;
    case HistoryNumberParseResult::Error:
        return HistoryExpansionResult::Error;
    case HistoryNumberParseResult::Parsed:
        break;
    }

    return apply_history_reference(state, num, begin, end, replacement);
}

} // namespace

bool expand_history(shell::ShellState &state, std::string &line) {
    if (line.empty()) {
        return true;
    }

    std::vector<shell_text::Replacement> replacements;
    const bool ok = shell_text::for_each_unescaped_position(
        line, [&](size_t &i, const shell_text::ScanState &scan_state) {
            if (scan_state.in_single_quote || line[i] != '!') {
                return true;
            }

            shell_text::Replacement replacement{};
            switch (parse_history_expansion(state, line, i, replacement)) {
            case HistoryExpansionResult::NoMatch:
                return true;
            case HistoryExpansionResult::Error:
                return false;
            case HistoryExpansionResult::Applied:
                replacements.push_back(std::move(replacement));
                i = replacements.back().end - 1;
                return true;
            }

            return true;
        });

    if (!ok) {
        return false;
    }

    if (!replacements.empty()) {
        line = shell_text::apply_replacements(line, replacements);
        std::cout << line << std::endl;
    }

    return true;
}

void save_command_line(shell::ShellState &state, const std::string &line) {
    state.history.push_back(line);
}

void load_history_file(shell::ShellState &state, const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            state.history.push_back(line);
        }
    }
}

void save_history_file(const shell::ShellState &state, const std::string &path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return;
    }

    for (const std::string &line : state.history) {
        file << line << '\n';
    }
}

void load_shell_history(shell::ShellState &state) {
    const std::string path = resolve_history_path(state);
    if (!path.empty()) {
        load_history_file(state, path);
    }
}

void save_shell_history(const shell::ShellState &state) {
    if (!state.interactive) {
        return;
    }

    const std::string path = resolve_history_path(state);
    if (history_file_exists(path)) {
        save_history_file(state, path);
    }
}

} // namespace features
