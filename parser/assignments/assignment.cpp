#include "assignment.hpp"

#include <cctype>

namespace parser {

bool split_assignment_expression(const std::string &expr, std::string &name,
                                 std::string &value) {
    const size_t sep = expr.find('=');
    if (sep == std::string::npos) {
        return false;
    }

    name = expr.substr(0, sep);
    value = expr.substr(sep + 1);
    return true;
}

bool is_valid_variable_name(const std::string &name) {
    if (name.empty()) {
        return false;
    }

    if (!(std::isalpha(static_cast<unsigned char>(name[0])) ||
          name[0] == '_')) {
        return false;
    }

    for (size_t i = 1; i < name.size(); ++i) {
        if (!(std::isalnum(static_cast<unsigned char>(name[i])) ||
              name[i] == '_')) {
            return false;
        }
    }

    return true;
}

bool is_assignment_word(const std::string &word, std::string &name,
                        std::string &value) {
    return split_assignment_expression(word, name, value) &&
           is_valid_variable_name(name);
}

} // namespace parser
