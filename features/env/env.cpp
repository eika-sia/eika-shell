#include "env.hpp"

#include <cctype>
#include <cstdlib>
#include <string>

#include "../../shell/shell.hpp"

namespace features {

std::string expand_environment_variables(const shell::ShellState &state,
                                         const std::string &line) {
    std::string out;
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            out.push_back(c);
            continue;
        }

        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            out.push_back(c);
            continue;
        }

        // Preserve escapes so reparsing keeps the same token boundaries.
        if (c == '\\') {
            if (i + 1 < line.size()) {
                out.push_back('\\');
                out.push_back(line[i + 1]);
                ++i;
            } else {
                out.push_back('\\');
            }
            continue;
        }

        if (in_single_quote) {
            out.push_back(c);
            continue;
        }

        if (c != '$') {
            out.push_back(c);
            continue;
        }

        // safety
        if (i + 1 >= line.size()) {
            out.push_back('$');
            continue;
        }

        size_t j = i + 1;

        if (line[j] == '?') {
            out += std::to_string(state.last_status);
            i = j;
            continue;
        }

        if (!(std::isalpha(static_cast<unsigned char>(line[j])) ||
              line[j] == '_')) {
            out.push_back('$');
            continue;
        }

        while (j < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[j])) ||
                line[j] == '_')) {
            ++j;
        }

        std::string name = line.substr(i + 1, j - (i + 1));

        if (const char *value = std::getenv(name.c_str())) {
            out += value;
        }

        i = j - 1;
    }

    return out;
}

} // namespace features
