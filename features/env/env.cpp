#include "env.hpp"

#include <cctype>
#include <cstdlib>
#include <string>

std::string expand_environment_variables(const std::string &line) {
    std::string out;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        // backslash
        if (c == '\\') {
            if (i + 1 < line.size()) {
                out.push_back(line[i + 1]);
                ++i;
            } else {
                out.push_back('\\');
            }
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
