#pragma once

#include <cstddef>
#include <string>

namespace parser {

struct SourceSpan {
    size_t start = std::string::npos;
    size_t end = std::string::npos;
};

struct SourceLocation {
    size_t line = 1;
    size_t column = 1;
};

} // namespace parser
