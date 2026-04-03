#pragma once

#include <string>

namespace parser {

bool split_assignment_expression(const std::string &expr, std::string &name,
                                 std::string &value);
bool is_valid_variable_name(const std::string &name);
bool is_assignment_word(const std::string &word, std::string &name,
                        std::string &value);

} // namespace parser
