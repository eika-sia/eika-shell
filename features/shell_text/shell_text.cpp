#include "shell_text.hpp"

namespace features::shell_text {

bool for_each_unescaped_position(const std::string &line,
                                 const ScanVisitor &visitor) {
    ScanState state{};

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        if (c == '\\' && !state.in_single_quote) {
            if (i + 1 < line.size()) {
                ++i;
            }
            continue;
        }

        if (c == '\'' && !state.in_double_quote) {
            state.in_single_quote = !state.in_single_quote;
            continue;
        }

        if (c == '"' && !state.in_single_quote) {
            state.in_double_quote = !state.in_double_quote;
            continue;
        }

        if (!visitor(i, state)) {
            return false;
        }
    }

    return true;
}

std::string apply_replacements(const std::string &line,
                               const std::vector<Replacement> &replacements) {
    if (replacements.empty()) {
        return line;
    }

    std::string expanded;
    expanded.reserve(line.size());

    size_t cursor = 0;
    for (const Replacement &replacement : replacements) {
        expanded.append(line, cursor, replacement.begin - cursor);
        expanded += replacement.text;
        cursor = replacement.end;
    }

    expanded.append(line, cursor, std::string::npos);
    return expanded;
}

} // namespace features::shell_text
