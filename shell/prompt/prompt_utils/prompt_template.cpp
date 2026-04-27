#include "prompt_template.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "../../../builtins/env/env.hpp"
#include "../render_utils.hpp"
#include "./prompt_segments.hpp"

namespace shell::prompt::prompt_template {
namespace {

enum class RenderedPartKind {
    Text,
    Whitespace,
    Style,
    AutoPowerlineLeft,
    AutoPowerlineRight,
    Newline,
    EmptyData,
};

struct RenderedPart {
    std::string rendered;
    RenderedPartKind kind = RenderedPartKind::Text;
    bool changes_background = false;
    prompt_segments::PromptColor background =
        prompt_segments::PromptColor::Default;
};

enum class AutoPowerlineMode {
    None,
    Left,
    Right,
};

using RenderedPartList = std::vector<RenderedPart>;
using RenderedPartGroups = std::vector<RenderedPartList>;

struct VisibleSegment {
    std::string rendered;
    prompt_segments::PromptColor background =
        prompt_segments::PromptColor::Default;
};

struct RenderedPromptLines {
    std::string header;
    std::string last_line;
};

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

std::string resolve_right_prompt_template(const shell::ShellState &state) {
    const shell::ShellVariable *rprompt =
        builtins::env::find_variable(state, "RPROMPT");
    if (rprompt != nullptr && !rprompt->value.empty()) {
        return rprompt->value;
    }

    return "";
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

        size_t trimmed_end = line_end;
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

void append_literal_parts(std::vector<RenderedPart> &parts,
                          const std::string &text) {
    size_t start = 0;
    while (start < text.size()) {
        const char ch = text[start];
        if (ch == '\n') {
            parts.push_back(
                RenderedPart{std::string("\n"), RenderedPartKind::Newline});
            ++start;
            continue;
        }

        const bool whitespace = ch == ' ' || ch == '\t';
        size_t end = start + 1;
        while (end < text.size()) {
            const char next = text[end];
            if (next == '\n') {
                break;
            }

            const bool next_whitespace = next == ' ' || next == '\t';
            if (next_whitespace != whitespace) {
                break;
            }
            ++end;
        }

        parts.push_back(RenderedPart{text.substr(start, end - start),
                                     whitespace ? RenderedPartKind::Whitespace
                                                : RenderedPartKind::Text});
        start = end;
    }
}

void append_rendered_token_parts(std::vector<RenderedPart> &parts,
                                 const prompt_segments::RenderedToken &token) {
    using prompt_segments::PromptTokenKind;

    switch (token.kind) {
    case PromptTokenKind::Style:
        parts.push_back(RenderedPart{token.rendered, RenderedPartKind::Style,
                                     token.style.changes_background,
                                     token.style.background});
        return;
    case PromptTokenKind::AutoPowerlineLeft:
        parts.push_back(RenderedPart{"", RenderedPartKind::AutoPowerlineLeft});
        return;
    case PromptTokenKind::AutoPowerlineRight:
        parts.push_back(RenderedPart{"", RenderedPartKind::AutoPowerlineRight});
        return;
    case PromptTokenKind::Newline:
        parts.push_back(
            RenderedPart{token.rendered, RenderedPartKind::Newline});
        return;
    case PromptTokenKind::Data:
        if (token.rendered.empty()) {
            parts.push_back(RenderedPart{"", RenderedPartKind::EmptyData});
        } else {
            parts.push_back(
                RenderedPart{token.rendered, RenderedPartKind::Text});
        }
        return;
    }
}

std::string render_plain_parts(const std::vector<RenderedPart> &parts) {
    std::string rendered;
    bool previous_token_was_empty = false;

    for (const RenderedPart &part : parts) {
        switch (part.kind) {
        case RenderedPartKind::Style:
            rendered += part.rendered;
            break;
        case RenderedPartKind::EmptyData:
            previous_token_was_empty = true;
            break;
        case RenderedPartKind::Whitespace:
            if (previous_token_was_empty &&
                rendered_ends_with_visible_blank(rendered)) {
                break;
            }

            rendered += part.rendered;
            break;
        case RenderedPartKind::Text:
            rendered += part.rendered;
            previous_token_was_empty = false;
            break;
        case RenderedPartKind::AutoPowerlineLeft:
        case RenderedPartKind::AutoPowerlineRight:
        case RenderedPartKind::Newline:
            break;
        }
    }

    return rendered;
}

bool part_has_visible_non_whitespace(const RenderedPart &part) {
    if (part.kind != RenderedPartKind::Text) {
        return false;
    }

    for (unsigned char ch : part.rendered) {
        if (ch != ' ' && ch != '\t') {
            return true;
        }
    }

    return false;
}

bool segment_has_visible_content(const std::vector<RenderedPart> &parts) {
    for (const RenderedPart &part : parts) {
        if (part_has_visible_non_whitespace(part)) {
            return true;
        }
    }

    return false;
}

prompt_segments::PromptColor
segment_background(const std::vector<RenderedPart> &parts) {
    prompt_segments::PromptColor background =
        prompt_segments::PromptColor::Default;

    for (const RenderedPart &part : parts) {
        if (part.kind == RenderedPartKind::Style && part.changes_background) {
            background = part.background;
        }
    }

    return background;
}

std::optional<VisibleSegment>
render_visible_segment(const RenderedPartList &parts) {
    if (!segment_has_visible_content(parts)) {
        return std::nullopt;
    }

    return VisibleSegment{render_plain_parts(parts), segment_background(parts)};
}

AutoPowerlineMode detect_auto_powerline_mode(const RenderedPartList &parts) {
    bool has_left = false;
    bool has_right = false;

    for (const RenderedPart &part : parts) {
        if (part.kind == RenderedPartKind::AutoPowerlineLeft) {
            has_left = true;
        } else if (part.kind == RenderedPartKind::AutoPowerlineRight) {
            has_right = true;
        }
    }

    if (has_left) {
        return AutoPowerlineMode::Left;
    }
    if (has_right) {
        return AutoPowerlineMode::Right;
    }

    return AutoPowerlineMode::None;
}

RenderedPartGroups
split_auto_powerline_segments(const RenderedPartList &parts,
                              RenderedPartKind separator_kind) {
    RenderedPartGroups raw_segments;
    RenderedPartList current_raw_segment;

    for (const RenderedPart &part : parts) {
        if (part.kind == separator_kind) {
            raw_segments.push_back(std::move(current_raw_segment));
            current_raw_segment.clear();
            continue;
        }

        current_raw_segment.push_back(part);
    }
    raw_segments.push_back(std::move(current_raw_segment));

    return raw_segments;
}

std::vector<VisibleSegment>
collect_visible_segments(const RenderedPartGroups &raw_segments,
                         size_t start_index = 0) {
    std::vector<VisibleSegment> visible_segments;
    visible_segments.reserve(raw_segments.size());

    for (size_t i = start_index; i < raw_segments.size(); ++i) {
        const std::optional<VisibleSegment> segment =
            render_visible_segment(raw_segments[i]);
        if (segment.has_value()) {
            visible_segments.push_back(*segment);
        }
    }

    return visible_segments;
}

std::string finalize_rendered_line(std::string rendered) {
    if (rendered.empty()) {
        return rendered;
    }

    const std::string reset = prompt_segments::final_reset_sequence();
    const size_t trailing_blank_start =
        rendered.find_last_not_of(" \t") == std::string::npos
            ? 0
            : rendered.find_last_not_of(" \t") + 1;

    return rendered.substr(0, trailing_blank_start) + reset +
           rendered.substr(trailing_blank_start);
}

std::string
render_auto_powerline_left_line(const RenderedPartGroups &raw_segments) {
    std::string rendered;
    if (!raw_segments.empty()) {
        rendered = render_plain_parts(raw_segments.front());
    }

    prompt_segments::PromptColor previous_background =
        prompt_segments::PromptColor::Default;
    if (!raw_segments.empty()) {
        const std::optional<VisibleSegment> first_segment =
            render_visible_segment(raw_segments.front());
        if (first_segment.has_value()) {
            previous_background = first_segment->background;
        }
    }

    for (const VisibleSegment &segment :
         collect_visible_segments(raw_segments, 1)) {
        rendered += prompt_segments::render_powerline_left_transition(
            previous_background, segment.background);
        rendered += segment.rendered;
        previous_background = segment.background;
    }

    return finalize_rendered_line(std::move(rendered));
}

std::string
render_auto_powerline_right_line(const RenderedPartGroups &raw_segments) {
    const std::vector<VisibleSegment> visible_segments =
        collect_visible_segments(raw_segments);
    const bool has_trailing_auto_close =
        raw_segments.size() > 1 &&
        !segment_has_visible_content(raw_segments.back());
    const std::string trailing_tail =
        has_trailing_auto_close ? render_plain_parts(raw_segments.back()) : "";

    std::string rendered;
    for (size_t i = 0; i < visible_segments.size(); ++i) {
        if (i > 0) {
            rendered += prompt_segments::render_powerline_right_transition(
                visible_segments[i - 1].background,
                visible_segments[i].background);
        }

        rendered += visible_segments[i].rendered;
    }

    if (has_trailing_auto_close && !visible_segments.empty()) {
        rendered += prompt_segments::render_powerline_right_transition(
            visible_segments.back().background,
            prompt_segments::PromptColor::Default);
    }

    rendered += trailing_tail;
    return finalize_rendered_line(std::move(rendered));
}

std::string render_line_parts(const std::vector<RenderedPart> &parts) {
    switch (detect_auto_powerline_mode(parts)) {
    case AutoPowerlineMode::Left:
        return render_auto_powerline_left_line(split_auto_powerline_segments(
            parts, RenderedPartKind::AutoPowerlineLeft));
    case AutoPowerlineMode::Right:
        return render_auto_powerline_right_line(split_auto_powerline_segments(
            parts, RenderedPartKind::AutoPowerlineRight));
    case AutoPowerlineMode::None:
        break;
    }

    return finalize_rendered_line(render_plain_parts(parts));
}

std::string render_parts(const std::vector<RenderedPart> &parts) {
    std::string rendered;
    std::vector<RenderedPart> line_parts;

    for (const RenderedPart &part : parts) {
        if (part.kind != RenderedPartKind::Newline) {
            line_parts.push_back(part);
            continue;
        }

        rendered += render_line_parts(line_parts);
        rendered.push_back('\n');
        line_parts.clear();
    }

    rendered += render_line_parts(line_parts);
    return rendered;
}

std::string render_prompt_template(const shell::ShellState &state,
                                   const std::string &prompt_template) {
    std::vector<RenderedPart> parts;
    parts.reserve(prompt_template.size());

    for (size_t i = 0; i < prompt_template.size(); ++i) {
        const char ch = prompt_template[i];
        if (ch != '%') {
            append_literal_parts(parts, std::string(1, ch));
            continue;
        }

        if (i + 1 >= prompt_template.size()) {
            append_literal_parts(parts, "%");
            continue;
        }

        if (prompt_template[i + 1] == '%') {
            append_literal_parts(parts, "%");
            ++i;
            continue;
        }

        std::string token;
        size_t next_index = i + 1;
        if (!parse_braced_token(prompt_template, i, token, next_index) &&
            !parse_bare_token(prompt_template, i, token, next_index)) {
            append_literal_parts(parts, "%");
            continue;
        }

        const std::optional<prompt_segments::RenderedToken> expanded =
            prompt_segments::render_token(state, token);
        if (!expanded.has_value()) {
            append_literal_parts(parts,
                                 prompt_template.substr(i, next_index - i));
            i = next_index - 1;
            continue;
        }

        append_rendered_token_parts(parts, *expanded);
        i = next_index - 1;
    }

    std::string rendered = render_parts(parts);
    trim_trailing_blank_runs(rendered);
    return rendered;
}

RenderedPromptLines split_rendered_prompt_lines(const std::string &rendered) {
    RenderedPromptLines lines;
    const size_t split = rendered.rfind('\n');
    if (split == std::string::npos) {
        lines.last_line = rendered;
        return lines;
    }

    lines.header = rendered.substr(0, split);
    lines.last_line = rendered.substr(split + 1);
    return lines;
}

PromptLayout split_prompt_layout(const std::string &rendered) {
    PromptLayout layout{};
    const RenderedPromptLines lines = split_rendered_prompt_lines(rendered);
    layout.header_rendered = lines.header;
    layout.input_prefix_rendered = lines.last_line;

    layout.prompt_prefix_display_width =
        render_utils::measure_display_width(layout.input_prefix_rendered);
    return layout;
}

} // namespace

PromptLayout build_layout(const shell::ShellState &state) {
    PromptLayout layout = split_prompt_layout(
        render_prompt_template(state, resolve_prompt_template(state)));

    const std::string right_prompt_template =
        resolve_right_prompt_template(state);
    if (!right_prompt_template.empty()) {
        layout.input_right_rendered =
            split_rendered_prompt_lines(
                render_prompt_template(state, right_prompt_template))
                .last_line;
        layout.input_right_display_width =
            render_utils::measure_display_width(layout.input_right_rendered);
    }

    return layout;
}

} // namespace shell::prompt::prompt_template
