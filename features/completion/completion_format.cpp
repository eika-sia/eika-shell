#include "completion_format.hpp"

namespace features {
namespace {
bool needs_unquoted_escape(char c) {
    switch (c) {
    case ' ':
    case '\t':
    case '\\':
    case '\'':
    case '"':
    case '$':
    case '!':
    case '|':
    case '&':
    case ';':
    case '<':
    case '>':
        return true;
    default:
        return false;
    }
}

std::string render_unquoted(const std::string &text) {
    std::string out;
    for (char c : text) {
        if (needs_unquoted_escape(c)) {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

std::string render_double_quoted(const std::string &text, bool finalize) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\' || c == '$' || c == '!') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    if (finalize) {
        out.push_back('"');
    }
    return out;
}

std::string render_single_quoted(const std::string &text, bool finalize) {
    std::string out = "'";
    for (char c : text) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    if (finalize) {
        out.push_back('\'');
    }
    return out;
}

} // namespace

std::string
format_completion_replacement(const std::string &logical_text,
                              const CompletionFormatOptions &options) {
    std::string out;

    switch (options.quote_mode) {
    case CompletionQuoteMode::None:
        out = render_unquoted(logical_text);
        break;
    case CompletionQuoteMode::Single:
        out = render_single_quoted(logical_text, options.finalize);
        break;
    case CompletionQuoteMode::Double:
        out = render_double_quoted(logical_text, options.finalize);
        break;
    }

    if (options.finalize) {
        if (options.is_directory) {
            out += "/";
        } else {
            out += " ";
        }
    }

    return out;
}

std::string get_basename_part(const std::string &token) {
    size_t pos = token.find_last_of('/');

    if (pos == std::string::npos) {
        return token;
    }

    return token.substr(pos + 1);
}

} // namespace features
