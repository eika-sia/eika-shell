#pragma once

#include "../diagnostics.hpp"
#include "../parser.hpp"
#include "./tokenize.hpp"

#include <string>

namespace parser {

bool parse_simple_command(const std::vector<Token> &tokens,
                          const std::string &source, Command &cmd,
                          std::vector<diagnostics::Diagnostic> &diagnostics);
bool parse_pipeline_tokens(const std::vector<Token> &tokens,
                           const std::string &source, Pipeline &pipe,
                           std::vector<diagnostics::Diagnostic> &diagnostics);
bool parse_and_or_tokens(const std::vector<Token> &tokens,
                         const std::string &source, ConditionalChain &chain,
                         std::vector<diagnostics::Diagnostic> &diagnostics);

} // namespace parser
