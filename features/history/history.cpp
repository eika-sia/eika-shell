#include "history.hpp"

#include <cctype>
#include <iostream>
#include <string>
#include <vector>

#include "../shell_text/shell_text.hpp"

namespace features {
namespace {

enum class HistoryExpansionResult {
    NoMatch,
    Applied,
    Error,
};

HistoryExpansionResult parse_history_expansion(
    const shell::ShellState &state, const std::string &line, size_t begin,
    shell_text::Replacement &replacement) {
    if (begin + 1 >= line.size()) {
        return HistoryExpansionResult::NoMatch;
    }

    int num = 0;
    size_t end = begin + 1;

    if (line[end] == '!') {
        num = static_cast<int>(state.history.size());
        ++end;
    } else if (std::isdigit(static_cast<unsigned char>(line[end]))) {
        while (end < line.size() &&
               std::isdigit(static_cast<unsigned char>(line[end]))) {
            ++end;
        }

        try {
            num = std::stoi(line.substr(begin + 1, end - (begin + 1)));
        } catch (const std::invalid_argument &) {
            std::cerr << "history: invalid argument" << std::endl;
            return HistoryExpansionResult::Error;
        } catch (const std::out_of_range &) {
            std::cerr << "history: number out of range" << std::endl;
            return HistoryExpansionResult::Error;
        }
    } else {
        return HistoryExpansionResult::NoMatch;
    }

    if (num < 1 || num > static_cast<int>(state.history.size())) {
        std::cerr << "history: invalid history number " << num << std::endl;
        return HistoryExpansionResult::Error;
    }

    replacement.begin = begin;
    replacement.end = end;
    replacement.text = state.history[num - 1];
    return HistoryExpansionResult::Applied;
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

} // namespace features
