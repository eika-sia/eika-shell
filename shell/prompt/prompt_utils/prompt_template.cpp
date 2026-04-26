#include "prompt_template.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include "../../../builtins/env/env.hpp"
#include "../render_utils.hpp"
#include "./prompt_segments.hpp"

namespace shell::prompt::prompt_template {
namespace {

const char *default_prompt_template =
    "%{bold_magenta}╭─%{reset} "
    "%{bold_magenta}%user%{bold_cyan} → %dir%{reset} "
    "%{bold_red}%status%{reset} "
    "%{green}%git%{reset} "
    "%{cyan}%bg%{reset}"
    "%{n}"
    "%{bold_magenta}%arrow%{reset}";

std::string resolve_prompt_template(const shell::ShellState &state) {
    const shell::ShellVariable *prompt =
        builtins::env::find_variable(state, "PROMPT");
    if (prompt == nullptr || prompt->value.empty()) {
        return default_prompt_template;
    }

    return prompt->value;
}

void trim_trailing_blank_runs(std::string &text) {
    std::string out;
    out.reserve(text.size());

    size_t line_start = 0;
    while (line_start <= text.size()) {
        const size_t line_end = text.find('\n', line_start);
        if (line_end == std::string::npos) {
            out.append(text, line_start, std::string::npos);
            break;
        }

        const size_t segment_end = line_end;

        size_t trimmed_end = segment_end;
        while (trimmed_end > line_start && (text[trimmed_end - 1] == ' ' ||
                                            text[trimmed_end - 1] == '\t')) {
            --trimmed_end;
        }

        out.append(text, line_start, trimmed_end - line_start);
        out.push_back('\n');
        line_start = line_end + 1;
    }

    text = std::move(out);
}

bool rendered_ends_with_visible_blank(const std::string &text) {
    char last_visible = '\0';
    bool have_visible = false;

    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);

        if (c == '\033') {
            ++i;
            if (i < text.size() && text[i] == '[') {
                ++i;
                while (i < text.size() && !(text[i] >= '@' && text[i] <= '~')) {
                    ++i;
                }
                if (i < text.size()) {
                    ++i;
                }
            }
            continue;
        }

        if ((c & 0xC0) != 0x80) {
            last_visible = static_cast<char>(c);
            have_visible = true;
        }
        ++i;
    }

    return have_visible && (last_visible == ' ' || last_visible == '\t');
}

bool parse_braced_token(const std::string &prompt_template, size_t start,
                        std::string &token, size_t &next_index) {
    if (start + 1 >= prompt_template.size() ||
        prompt_template[start + 1] != '{') {
        return false;
    }

    const size_t close = prompt_template.find('}', start + 2);
    if (close == std::string::npos) {
        return false;
    }

    token = prompt_template.substr(start + 2, close - (start + 2));
    next_index = close + 1;
    return true;
}

bool parse_bare_token(const std::string &prompt_template, size_t start,
                      std::string &token, size_t &next_index) {
    if (start + 1 >= prompt_template.size()) {
        return false;
    }

    const char next = prompt_template[start + 1];
    if (!(std::isalpha(static_cast<unsigned char>(next)) || next == '_')) {
        return false;
    }

    size_t token_end = start + 1;
    while (
        token_end < prompt_template.size() &&
        (std::isalnum(static_cast<unsigned char>(prompt_template[token_end])) ||
         prompt_template[token_end] == '_')) {
        ++token_end;
    }

    token = prompt_template.substr(start + 1, token_end - (start + 1));
    next_index = token_end;
    return true;
}

size_t measure_rendered_block_max_width(const std::string &text) {
    size_t max_width = 0;
    size_t line_start = 0;
    while (line_start <= text.size()) {
        const size_t line_end = text.find('\n', line_start);
        const size_t segment_end =
            line_end == std::string::npos ? text.size() : line_end;
        max_width =
            std::max(max_width, render_utils::measure_display_width(text.substr(
                                    line_start, segment_end - line_start)));

        if (line_end == std::string::npos) {
            break;
        }

        line_start = line_end + 1;
    }

    return max_width;
}

std::string render_prompt_template(const shell::ShellState &state,
                                   const std::string &prompt_template) {
    std::string rendered;
    rendered.reserve(prompt_template.size());

    bool previous_token_was_empty = false;

    for (size_t i = 0; i < prompt_template.size(); ++i) {
        const char ch = prompt_template[i];
        if (ch != '%') {
            if (previous_token_was_empty && (ch == ' ' || ch == '\t') &&
                rendered_ends_with_visible_blank(rendered)) {
                continue;
            }

            rendered.push_back(ch);
            if (ch == '\n' || (ch != ' ' && ch != '\t')) {
                previous_token_was_empty = false;
            }
            continue;
        }

        if (i + 1 >= prompt_template.size()) {
            rendered.push_back('%');
            previous_token_was_empty = false;
            continue;
        }

        if (prompt_template[i + 1] == '%') {
            rendered.push_back('%');
            previous_token_was_empty = false;
            ++i;
            continue;
        }

        std::string token;
        size_t next_index = i + 1;
        if (!parse_braced_token(prompt_template, i, token, next_index) &&
            !parse_bare_token(prompt_template, i, token, next_index)) {
            rendered.push_back('%');
            previous_token_was_empty = false;
            continue;
        }

        const std::optional<std::string> expanded =
            prompt_segments::render_token(state, token);
        if (!expanded.has_value()) {
            rendered.append(prompt_template, i, next_index - i);
            previous_token_was_empty = false;
            i = next_index - 1;
            continue;
        }

        rendered += *expanded;
        if (expanded->empty()) {
            previous_token_was_empty = true;
        } else if (expanded->find('\n') != std::string::npos ||
                   render_utils::measure_display_width(*expanded) > 0) {
            previous_token_was_empty = false;
        }

        i = next_index - 1;
    }

    trim_trailing_blank_runs(rendered);
    return rendered;
}

PromptLayout split_prompt_layout(const std::string &rendered) {
    PromptLayout layout{};

    const size_t split = rendered.rfind('\n');
    if (split == std::string::npos) {
        layout.input_prefix_rendered = rendered;
    } else {
        layout.header_rendered = rendered.substr(0, split);
        layout.input_prefix_rendered = rendered.substr(split + 1);
    }

    layout.header_display_width =
        measure_rendered_block_max_width(layout.header_rendered);
    layout.prompt_prefix_display_width =
        render_utils::measure_display_width(layout.input_prefix_rendered);
    return layout;
}

} // namespace

PromptLayout build_layout(const shell::ShellState &state) {
    return split_prompt_layout(
        render_prompt_template(state, resolve_prompt_template(state)));
}

} // namespace shell::prompt::prompt_template
