#pragma once

#include <string>

namespace features {

enum class CompletionQuoteMode {
    None,
    Single,
    Double,
};

struct CompletionFormatOptions {
    CompletionQuoteMode quote_mode = CompletionQuoteMode::None;
    bool finalize = false;     // true for unique final match
    bool is_directory = false; // append /
};

std::string
format_completion_replacement(const std::string &logical_text,
                              const CompletionFormatOptions &options);

std::string get_basename_part(const std::string &token);
std::string format_completion_display_label(const std::string &candidate);

} // namespace features
