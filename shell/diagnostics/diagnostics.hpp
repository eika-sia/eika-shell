#pragma once

#include <string>
#include <vector>

#include "../../parser/source.hpp"

namespace shell::diagnostics {

enum class DiagnosticSeverity {
    Error,
    Warning,
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    parser::SourceSpan span;
    std::string message;
};

void add_error(std::vector<Diagnostic> &diagnostics, parser::SourceSpan span,
               std::string message);

void print_diagnostics(const std::string &source,
                       const std::vector<Diagnostic> &diagnostics);

} // namespace shell::diagnostics
