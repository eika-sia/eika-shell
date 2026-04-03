#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace features::shell_text {

struct ScanState {
    bool in_single_quote = false;
    bool in_double_quote = false;
};

struct Replacement {
    size_t begin = 0;
    size_t end = 0;
    std::string text;
};

using ScanVisitor = std::function<bool(size_t &, const ScanState &)>;

bool is_shell_separator(char c);
bool for_each_unescaped_position(const std::string &line,
                                 const ScanVisitor &visitor);

std::string apply_replacements(const std::string &line,
                               const std::vector<Replacement> &replacements);

} // namespace features::shell_text
