#include "diagnostics.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace parser::diagnostics {
namespace {

constexpr const char *reset = "\033[0m";
constexpr const char *bold = "\033[1m";
constexpr const char *red = "\033[31m";
constexpr const char *yellow = "\033[33m";
constexpr const char *dim = "\033[2m";

size_t clamp_offset(const std::string &source, size_t offset) {
    if (offset == std::string::npos || offset > source.size()) {
        return source.size();
    }

    return offset;
}

size_t line_start_for_offset(const std::string &source, size_t offset) {
    offset = clamp_offset(source, offset);
    if (offset == 0) {
        return 0;
    }

    const size_t newline = source.rfind('\n', offset - 1);
    if (newline == std::string::npos) {
        return 0;
    }

    return newline + 1;
}

size_t line_end_for_offset(const std::string &source, size_t offset) {
    offset = clamp_offset(source, offset);
    const size_t newline = source.find('\n', offset);
    if (newline == std::string::npos) {
        return source.size();
    }

    return newline;
}

size_t decimal_width(size_t value) {
    size_t width = 1;
    while (value >= 10) {
        value /= 10;
        ++width;
    }

    return width;
}

const char *severity_label(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Error:
        return "error";
    case DiagnosticSeverity::Warning:
        return "warning";
    }

    return "diagnostic";
}

const char *severity_color(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Error:
        return red;
    case DiagnosticSeverity::Warning:
        return yellow;
    }

    return red;
}

SourceSpan normalized_span(const std::string &source, SourceSpan span) {
    span.start = clamp_offset(source, span.start);
    span.end = clamp_offset(source, span.end);
    if (span.end < span.start) {
        span.end = span.start;
    }

    return span;
}

std::string spaces(size_t count) { return std::string(count, ' '); }

} // namespace

SourceLocation source_location_for_offset(const std::string &source,
                                          size_t offset) {
    offset = clamp_offset(source, offset);
    SourceLocation location{};

    for (size_t i = 0; i < offset; ++i) {
        if (source[i] == '\n') {
            ++location.line;
            location.column = 1;
        } else {
            ++location.column;
        }
    }

    return location;
}

void add_error(std::vector<Diagnostic> &diagnostics, SourceSpan span,
               std::string message) {
    diagnostics.push_back(Diagnostic{DiagnosticSeverity::Error, span, message});
}

void print_diagnostics(const std::string &source,
                       const std::vector<Diagnostic> &diagnostics) {
    for (const Diagnostic &diagnostic : diagnostics) {
        const SourceSpan span = normalized_span(source, diagnostic.span);
        const SourceLocation location =
            source_location_for_offset(source, span.start);
        const size_t line_start = line_start_for_offset(source, span.start);
        const size_t line_end = line_end_for_offset(source, span.start);
        const std::string line =
            source.substr(line_start, line_end - line_start);
        const size_t line_number_width = decimal_width(location.line);
        const size_t caret_start = span.start - line_start;
        const size_t span_end_on_line = std::min(span.end, line_end);
        const size_t caret_count =
            std::max<size_t>(1, span_end_on_line - span.start);
        const char *color = severity_color(diagnostic.severity);

        std::cerr << color << bold << severity_label(diagnostic.severity)
                  << reset << ": " << diagnostic.message << "\n";
        std::cerr << dim << spaces(line_number_width) << " --> line "
                  << location.line << ", column " << location.column << reset
                  << "\n";
        std::cerr << dim << spaces(line_number_width) << " |" << reset << "\n";
        std::cerr << dim << location.line << " | " << reset << line << "\n";
        std::cerr << dim << spaces(line_number_width) << " | " << reset
                  << spaces(caret_start) << color
                  << std::string(caret_count, '^') << reset << "\n";
    }
}

} // namespace parser::diagnostics
