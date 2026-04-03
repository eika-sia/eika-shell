#include "highlighting.hpp"

#include "../../builtins/builtins.hpp"
#include "../../builtins/env/env.hpp"
#include "../../parser/assignments/assignment.hpp"
#include "../../parser/internals/tokenize.hpp"
#include "../completion/path_completion.hpp"

namespace features::highlighting {
namespace {

enum class HighlightColor {
    None,
    Red,
    Green,
    Yellow,
    Blue,
    Cyan,
};

struct TextStyle {
    HighlightColor fg = HighlightColor::None;
    bool bold = false;
    bool underline = false;
};

struct StyledRange {
    size_t begin = 0;
    size_t end = 0;
    TextStyle style;
};

bool token_contains_quotes(const std::string &line,
                           const parser::Token &token) {
    if (token.raw_start == std::string::npos || token.raw_end > line.size() ||
        token.raw_start >= token.raw_end) {
        return false;
    }

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escape = false;

    for (size_t i = token.raw_start; i < token.raw_end; ++i) {
        const char c = line[i];

        if (escape) {
            escape = false;
            continue;
        }

        if (c == '\\' && !in_single_quote) {
            escape = true;
            continue;
        }

        if (c == '\'' && !in_double_quote) {
            return true;
        }

        if (c == '"' && !in_single_quote) {
            return true;
        }
    }

    return false;
}

std::string expand_for_lookup(const shell::ShellState &state,
                              const std::string &token) {
    return features::expand_tilde_prefix(
        state, builtins::env::expand_variables(state, token));
}

bool is_command_valid(const shell::ShellState &state,
                      const std::string &token) {
    if (token.empty())
        return false;

    std::string cmd = expand_for_lookup(state, token);

    if (builtins::is_builtin_name(cmd))
        return true;
    if (state.alias.find(token) != state.alias.end())
        return true;

    if (features::looks_like_path_token(cmd))
        return features::path_is_executable_file(state, cmd);

    return features::command_exists_in_path(state, cmd);
}

bool is_existing_path_token(const shell::ShellState &state,
                            const std::string &token) {
    return features::path_exists(state, expand_for_lookup(state, token));
}

TextStyle classify_word_token(const shell::ShellState &state,
                              const std::string &line,
                              const parser::Token &token,
                              bool expecting_command_word,
                              bool expecting_redirect_target) {
    bool quoted = token_contains_quotes(line, token);
    bool path_like = features::looks_like_path_token(token.text);

    TextStyle style{};

    if (expecting_redirect_target) {
        if (quoted) {
            style.fg = HighlightColor::Yellow;
        }
        style.underline = is_existing_path_token(state, token.text);

        return style;
    }

    if (expecting_command_word) {
        std::string name, value;
        if (parser::is_assignment_word(token.text, name, value)) {
            style.fg = HighlightColor::Blue;
            return style;
        }

        if (is_command_valid(state, token.text)) {
            style.fg = HighlightColor::Green;
            style.bold = true;
        } else {
            style.fg = HighlightColor::Red;
            style.bold = true;
        }

        if (path_like && is_existing_path_token(state, token.text)) {
            style.underline = true;
        }

        return style;
    }

    if (quoted) {
        style.fg = HighlightColor::Yellow;
    }

    if (path_like && is_existing_path_token(state, token.text)) {
        style.underline = true;
    }

    return style;
}

std::string ansi_prefix_for_style(const TextStyle &style) {
    std::vector<std::string> codes;

    if (style.bold) {
        codes.push_back("1");
    }
    if (style.underline) {
        codes.push_back("4");
    }

    switch (style.fg) {
    case HighlightColor::Red:
        codes.push_back("31");
        break;
    case HighlightColor::Green:
        codes.push_back("32");
        break;
    case HighlightColor::Yellow:
        codes.push_back("33");
        break;
    case HighlightColor::Blue:
        codes.push_back("34");
        break;
    case HighlightColor::Cyan:
        codes.push_back("36");
        break;
    case HighlightColor::None:
        break;
    }

    if (codes.empty()) {
        return "";
    }

    std::string prefix = "\033[";
    for (size_t i = 0; i < codes.size(); ++i) {
        if (i > 0) {
            prefix += ";";
        }
        prefix += codes[i];
    }
    prefix += "m";

    return prefix;
}
std::string render_with_styles(const std::string &line,
                               const std::vector<StyledRange> &ranges) {
    std::string out;
    size_t cursor = 0;

    for (const StyledRange &range : ranges) {
        out.append(line, cursor, range.begin - cursor);
        out += ansi_prefix_for_style(range.style);
        out.append(line, range.begin, range.end - range.begin);
        out += "\033[0m";
        cursor = range.end;
    }

    out.append(line, cursor, std::string::npos);
    return out;
}

} // namespace

std::string render_highlighted_line(const shell::ShellState &state,
                                    const std::string &line) {
    std::vector<parser::Token> tokens;
    parser::tokenize_line(line, tokens, parser::TokenizeMode::Relaxed);

    std::vector<StyledRange> ranges;

    bool expecting_command_word = true;
    bool expecting_redirect_target = false;

    for (const parser::Token &token : tokens) {
        if (token.kind == parser::TokenKind::Word) {
            TextStyle style =
                classify_word_token(state, line, token, expecting_command_word,
                                    expecting_redirect_target);

            if (style.fg != HighlightColor::None || style.bold ||
                style.underline) {
                ranges.push_back({token.raw_start, token.raw_end, style});
            }

            if (expecting_redirect_target) {
                expecting_redirect_target = false;
                continue;
            }

            if (expecting_command_word) {
                std::string name, value;
                if (!parser::is_assignment_word(token.text, name, value)) {
                    expecting_command_word = false;
                }
            }

            continue;
        }

        // operator token
        ranges.push_back(
            {token.raw_start, token.raw_end, TextStyle{HighlightColor::Cyan}});

        if (parser::is_redirect(token.kind)) {
            expecting_redirect_target = true;
            continue;
        }

        expecting_command_word = true;
        expecting_redirect_target = false;
    }

    return render_with_styles(line, ranges);
}

} // namespace features::highlighting
