#include "env.hpp"

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#include "../../shell/shell.hpp"
#include "../shell_text/shell_text.hpp"

namespace features {

std::string expand_environment_variables(const shell::ShellState &state,
                                         const std::string &line) {
    std::vector<shell_text::Replacement> replacements;
    shell_text::for_each_unescaped_position(
        line, [&](size_t &i, const shell_text::ScanState &scan_state) {
            if (scan_state.in_single_quote || line[i] != '$' ||
                i + 1 >= line.size()) {
                return true;
            }

            size_t end = i + 1;
            std::string text;

            if (line[end] == '?') {
                text = std::to_string(state.last_status);
                ++end;
            } else {
                if (!(std::isalpha(static_cast<unsigned char>(line[end])) ||
                      line[end] == '_')) {
                    return true;
                }

                while (end < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[end])) ||
                        line[end] == '_')) {
                    ++end;
                }

                const std::string name = line.substr(i + 1, end - (i + 1));
                if (const char *value = std::getenv(name.c_str())) {
                    text = value;
                }
            }

            replacements.push_back(shell_text::Replacement{i, end, text});
            i = end - 1;
            return true;
        });

    return shell_text::apply_replacements(line, replacements);
}

} // namespace features
