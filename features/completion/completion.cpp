#include "completion.hpp"
#include "completion_format.hpp"
#include "path_completion.hpp"

#include "../../parser/assignments/assignment.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace features {
namespace {

enum class CompletionTokenRole {
    CommandWord,
    Argument,
    RedirectTarget,
};

struct CompletionContext {
    size_t raw_begin = 0;
    size_t raw_end = 0;

    std::string logical_prefix;

    CompletionQuoteMode quote_mode = CompletionQuoteMode::None;
    CompletionTokenRole role = CompletionTokenRole::Argument;

    bool preserve_dot_slash = false;
};

// Keep this in sync with parser/internals/tokenize.cpp.
bool should_consume_backslash_escape(char c, bool in_double_quote) {
    if (in_double_quote) {
        return c == '"' || c == '\\' || c == '$' || c == '!' || c == '\n';
    }

    switch (c) {
    case ' ':
    case '\t':
    case '\\':
    case '\'':
    case '"':
    case '$':
    case '!':
    case '~':
    case '|':
    case '&':
    case ';':
    case '<':
    case '>':
    case '\n':
        return true;
    default:
        return false;
    }
}

struct ScanState {
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escape = false;
    bool escape_in_double_quote = false;

    bool expecting_command_word = true;
    bool expecting_redirect_target = false;
};

struct WordBuilder {
    size_t raw_begin = std::string::npos;
    struct Segment {
        size_t raw_begin = 0;
        size_t raw_end = 0;
        std::string text;
    };

    std::string logical;
    std::vector<Segment> segments;
};

struct ParsedWord {
    size_t raw_begin = 0;
    size_t raw_end = 0;
    std::string logical;
    std::vector<WordBuilder::Segment> segments;
    CompletionTokenRole role = CompletionTokenRole::Argument;
};

void begin_word_if_needed(WordBuilder &word, size_t raw_index) {
    if (word.raw_begin == std::string::npos) {
        word.raw_begin = raw_index;
    }
}

void append_segment(WordBuilder &word, size_t raw_begin, size_t raw_end,
                    std::string text) {
    begin_word_if_needed(word, raw_begin);
    word.logical += text;
    word.segments.push_back(
        WordBuilder::Segment{raw_begin, raw_end, std::move(text)});
}

CompletionTokenRole classify_word_role(ScanState &scan,
                                       const std::string &logical_word) {
    if (scan.expecting_redirect_target) {
        scan.expecting_redirect_target = false;
        return CompletionTokenRole::RedirectTarget;
    }

    if (scan.expecting_command_word) {
        std::string name;
        std::string value;
        if (parser::is_assignment_word(logical_word, name, value)) {
            return CompletionTokenRole::Argument;
        }

        scan.expecting_command_word = false;
        return CompletionTokenRole::CommandWord;
    }

    return CompletionTokenRole::Argument;
}

void apply_operator(ScanState &scan, std::string_view op) {
    if (op == "<" || op == ">" || op == ">>") {
        scan.expecting_redirect_target = true;
        return;
    }

    if (op == "|" || op == "||" || op == ";" || op == "&" || op == "&&") {
        scan.expecting_redirect_target = false;
        scan.expecting_command_word = true;
    }
}

CompletionQuoteMode quote_mode_for_scan_state(const ScanState &scan) {
    if (scan.in_single_quote) {
        return CompletionQuoteMode::Single;
    }

    if (scan.in_double_quote) {
        return CompletionQuoteMode::Double;
    }

    return CompletionQuoteMode::None;
}

bool is_blank_separator(char c) { return c == ' ' || c == '\t'; }

size_t get_operator_length(const std::string &buf, size_t index) {
    if (index >= buf.size()) {
        return 0;
    }

    const char c = buf[index];
    if (c == '|') {
        return (index + 1 < buf.size() && buf[index + 1] == '|') ? 2 : 1;
    }

    if (c == '&') {
        return (index + 1 < buf.size() && buf[index + 1] == '&') ? 2 : 1;
    }

    if (c == '>') {
        return (index + 1 < buf.size() && buf[index + 1] == '>') ? 2 : 1;
    }

    if (c == '<' || c == ';') {
        return 1;
    }

    return 0;
}

std::string build_logical_prefix(const ParsedWord &word, size_t cursor) {
    std::string prefix;

    for (const auto &segment : word.segments) {
        if (segment.raw_end <= cursor) {
            prefix += segment.text;
        }
    }

    return prefix;
}

CompletionContext parse_completion_context(const std::string &buf,
                                           size_t cursor) {
    if (cursor > buf.size()) {
        cursor = buf.size();
    }

    CompletionContext result{};
    ScanState scan{};
    WordBuilder word{};
    std::vector<ParsedWord> words;

    bool cursor_state_captured = false;
    CompletionQuoteMode cursor_quote_mode = CompletionQuoteMode::None;
    bool cursor_expecting_command_word = true;
    bool cursor_expecting_redirect_target = false;

    auto capture_cursor_state = [&](size_t position) {
        if (cursor_state_captured || position != cursor) {
            return;
        }

        cursor_state_captured = true;
        cursor_quote_mode = quote_mode_for_scan_state(scan);
        cursor_expecting_command_word = scan.expecting_command_word;
        cursor_expecting_redirect_target = scan.expecting_redirect_target;
    };

    auto flush_word = [&](size_t flush_raw_end) {
        if (word.raw_begin == std::string::npos) {
            return;
        }

        words.push_back(ParsedWord{word.raw_begin, flush_raw_end, word.logical,
                                   word.segments,
                                   classify_word_role(scan, word.logical)});

        word = WordBuilder{};
    };

    for (size_t i = 0; i < buf.size(); i++) {
        capture_cursor_state(i);

        const char c = buf[i];

        if (scan.escape) {
            if (should_consume_backslash_escape(c,
                                                scan.escape_in_double_quote)) {
                append_segment(word, i - 1, i + 1, std::string(1, c));
            } else {
                append_segment(word, i - 1, i, "\\");
                append_segment(word, i, i + 1, std::string(1, c));
            }

            scan.escape = false;
            continue;
        }

        if (c == '\\' && !scan.in_single_quote) {
            begin_word_if_needed(word, i);
            scan.escape = true;
            scan.escape_in_double_quote = scan.in_double_quote;
            continue;
        }

        if (c == '\'' && !scan.in_double_quote) {
            begin_word_if_needed(word, i);
            scan.in_single_quote = !scan.in_single_quote;
            continue;
        }

        if (c == '"' && !scan.in_single_quote) {
            begin_word_if_needed(word, i);
            scan.in_double_quote = !scan.in_double_quote;
            continue;
        }

        if (!scan.in_single_quote && !scan.in_double_quote &&
            is_blank_separator(c)) {
            flush_word(i);
            continue;
        }

        if (!scan.in_single_quote && !scan.in_double_quote) {
            const size_t operator_length = get_operator_length(buf, i);
            if (operator_length != 0) {
                flush_word(i);

                const std::string_view op(buf.data() + i, operator_length);
                apply_operator(scan, op);
                capture_cursor_state(i + operator_length);

                i += operator_length - 1;
                continue;
            }
        }

        append_segment(word, i, i + 1, std::string(1, c));
    }

    if (scan.escape) {
        append_segment(word, buf.size() - 1, buf.size(), "\\");
        scan.escape = false;
    }

    capture_cursor_state(buf.size());
    flush_word(buf.size());

    const ParsedWord *active_word = nullptr;
    for (const ParsedWord &candidate : words) {
        if (candidate.raw_begin <= cursor && cursor <= candidate.raw_end) {
            active_word = &candidate;
        }
    }

    if (active_word != nullptr) {
        result.raw_begin = active_word->raw_begin;
        result.raw_end = active_word->raw_end;
        result.logical_prefix = build_logical_prefix(*active_word, cursor);
        result.quote_mode = cursor_quote_mode;
        result.role = active_word->role;
        result.preserve_dot_slash = active_word->logical.rfind("./", 0) == 0;
        return result;
    }

    result.raw_begin = cursor;
    result.raw_end = cursor;
    result.quote_mode = cursor_quote_mode;
    result.role =
        cursor_expecting_redirect_target
            ? CompletionTokenRole::RedirectTarget
            : (cursor_expecting_command_word ? CompletionTokenRole::CommandWord
                                             : CompletionTokenRole::Argument);

    return result;
}

std::string
longest_common_prefix(const std::vector<CompletionCandidate> &matches) {
    if (matches.empty()) {
        return "";
    }

    std::string prefix = matches[0].text;
    for (size_t i = 1; i < matches.size(); ++i) {
        size_t j = 0;
        while (j < prefix.size() && j < matches[i].text.size() &&
               prefix[j] == matches[i].text[j]) {
            ++j;
        }
        prefix.resize(j);
    }
    return prefix;
}

bool is_directory_candidate(const CompletionCandidate &candidate) {
    return candidate.kind == CompletionDisplayKind::Directory;
}

std::string format_selection_candidate(
    const CompletionCandidate &candidate, CompletionQuoteMode quote_mode) {
    CompletionFormatOptions format{};
    format.quote_mode = quote_mode;

    std::string replacement =
        format_completion_replacement(candidate.text, format);
    if (is_directory_candidate(candidate)) {
        replacement += "/";
    }

    return replacement;
}

std::vector<std::string> selection_candidates(
    const std::vector<CompletionCandidate> &matches,
    CompletionQuoteMode quote_mode) {
    std::vector<std::string> out;
    out.reserve(matches.size());

    for (const auto &m : matches) {
        out.push_back(format_selection_candidate(m, quote_mode));
    }
    return out;
}

std::vector<CompletionDisplayCandidate>
display_candidates(const std::vector<CompletionCandidate> &matches) {
    std::vector<CompletionDisplayCandidate> out;
    out.reserve(matches.size());

    for (const auto &m : matches) {
        out.push_back(CompletionDisplayCandidate{
            is_directory_candidate(m) ? (m.text + "/") : m.text, m.kind});
    }

    return out;
}

} // namespace

CompletionResult complete_at_cursor(const shell::ShellState &state,
                                    const std::string &buf, size_t cursor) {
    CompletionContext ctx = parse_completion_context(buf, cursor);

    std::vector<CompletionCandidate> matches;
    const bool path_like = looks_like_path_token(ctx.logical_prefix) ||
                           ctx.role == CompletionTokenRole::RedirectTarget;

    if (ctx.role == CompletionTokenRole::CommandWord && !path_like) {
        matches = complete_command_token(state, ctx.logical_prefix);
    } else {
        PathCompletionOptions options{};
        options.keep_current_dir_prefix = ctx.preserve_dot_slash;
        options.executable_only =
            ctx.role == CompletionTokenRole::CommandWord && path_like;
        matches = complete_path_token(state, ctx.logical_prefix, options);
    }

    if (matches.empty()) {
        return {};
    }

    std::sort(matches.begin(), matches.end(),
              [](const auto &a, const auto &b) { return a.text < b.text; });

    CompletionFormatOptions format{};
    format.quote_mode = ctx.quote_mode;
    format.finalize = false;
    format.is_directory = false;
    if (matches.size() == 1) {
        format.finalize = true;
        format.is_directory = is_directory_candidate(matches[0]);
        return CompletionResult{
            CompletionAction::ReplaceToken,
            ctx.raw_begin,
            ctx.raw_end,
            format_completion_replacement(matches[0].text, format),
            {},
            {}};
    }

    const std::string lcp = longest_common_prefix(matches);
    if (lcp.size() > ctx.logical_prefix.size()) {
        return CompletionResult{CompletionAction::ReplaceToken,
                                ctx.raw_begin,
                                ctx.raw_end,
                                format_completion_replacement(lcp, format),
                                {},
                                {}};
    }

    return CompletionResult{CompletionAction::ShowCandidates, ctx.raw_begin,
                            ctx.raw_end, "",
                            selection_candidates(matches, ctx.quote_mode),
                            display_candidates(matches)};
}

} // namespace features
