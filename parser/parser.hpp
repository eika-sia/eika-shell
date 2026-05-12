#pragma once

#include "./ast.hpp"

#include <string>

namespace parser {

ast::ParseResult parse_program(const std::string &source);

} // namespace parser
