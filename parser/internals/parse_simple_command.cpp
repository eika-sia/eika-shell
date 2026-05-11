#include "internal.hpp"

#include <string>
#include <vector>

#include "../../shell/shell.hpp"
#include "../assignments/assignment.hpp"
#include "../diagnostics.hpp"

namespace parser {

bool parse_simple_command(const std::vector<Token> &tokens,
                          const std::string &source, Command &cmd,
                          std::vector<diagnostics::Diagnostic> &diagnostics) {
    cmd = Command{};
    cmd.valid = false;

    if (tokens.empty()) {
        diagnostics::add_error(diagnostics, SourceSpan{0, 0},
                               "syntax error: missing command");
        return false;
    }

    const size_t raw_start = tokens.front().raw_start;
    const size_t raw_end = tokens.back().raw_end;
    cmd.raw = shell::trim(source.substr(raw_start, raw_end - raw_start));

    bool seen_command_word = false;

    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token &token = tokens[i];

        if (token.kind == TokenKind::Word) {
            std::string name;
            std::string value;
            if (!seen_command_word &&
                is_assignment_word(token.text, name, value)) {
                cmd.assignments.push_back(Assignment{name, value});
            } else {
                cmd.args.push_back(token.text);
                if (cmd.command_name_offset == std::string::npos) {
                    cmd.command_name_offset = token.raw_start - raw_start;
                    cmd.command_name_length = token.raw_end - token.raw_start;
                }
                seen_command_word = true;
            }
            continue;
        }

        if (is_redirect(token.kind)) {
            if (i + 1 >= tokens.size() ||
                tokens[i + 1].kind != TokenKind::Word) {
                diagnostics::add_error(
                    diagnostics, token.span,
                    "syntax error: expected filename after " + token.text);
                return false;
            }

            const std::string &filename = tokens[i + 1].text;

            if (token.kind == TokenKind::InputRedirect) {
                if (!cmd.input_file.empty()) {
                    diagnostics::add_error(
                        diagnostics, token.span,
                        "syntax error: multiple input redirections");
                    return false;
                }
                cmd.input_file = filename;
            } else {
                if (!cmd.output_file.empty()) {
                    diagnostics::add_error(
                        diagnostics, token.span,
                        "syntax error: multiple output redirections");
                    return false;
                }
                cmd.output_file = filename;
                cmd.append_output = (token.kind == TokenKind::AppendRedirect);
            }

            ++i;
            continue;
        }

        diagnostics::add_error(diagnostics, token.span,
                               "syntax error: unexpected token " + token.text);
        return false;
    }

    if ((cmd.args.empty() && cmd.assignments.empty()) ||
        (!cmd.args.empty() && cmd.args[0].empty())) {
        diagnostics::add_error(diagnostics, SourceSpan{raw_start, raw_end},
                               "syntax error: missing command");
        return false;
    }

    cmd.valid = true;
    return true;
}

} // namespace parser
