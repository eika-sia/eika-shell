#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace parser {

struct SourceSpan {
    size_t start = std::string::npos;
    size_t end = std::string::npos;
};

struct SourceLocation {
    size_t line = 1;
    size_t column = 1;
};

namespace diagnostics {

enum class DiagnosticSeverity {
    Error,
    Warning,
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    SourceSpan span;
    std::string message;
};

SourceLocation source_location_for_offset(const std::string &source,
                                          size_t offset);

void add_error(std::vector<Diagnostic> &diagnostics, SourceSpan span,
               std::string message);
void add_warning(std::vector<Diagnostic> &diagnostics, SourceSpan span,
                 std::string message);

void print_diagnostics(const std::string &source,
                       const std::vector<Diagnostic> &diagnostics);

} // namespace diagnostics
} // namespace parser
