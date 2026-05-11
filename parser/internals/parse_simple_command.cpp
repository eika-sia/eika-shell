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

    const size_t command_start = tokens.front().span.start;
    const size_t command_end = tokens.back().span.end;
    cmd.raw =
        shell::trim(source.substr(command_start, command_end - command_start));

    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token &token = tokens[i];

        if (token.kind == TokenKind::Assignment) {
            std::string name;
            std::string value;
            if (!is_assignment_word(token.text, name, value)) {
                diagnostics::add_error(
                    diagnostics, token.span,
                    "syntax error: invalid assignment " + token.text);
                return false;
            }

            cmd.assignments.push_back(Assignment{name, value});
            continue;
        }

        if (token.kind == TokenKind::Word) {
            cmd.args.push_back(token.text);
            if (cmd.command_name_offset == std::string::npos) {
                cmd.command_name_offset = token.span.start - command_start;
                cmd.command_name_length = token.span.end - token.span.start;
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
        diagnostics::add_error(diagnostics,
                               SourceSpan{command_start, command_end},
                               "syntax error: missing command");
        return false;
    }

    cmd.valid = true;
    return true;
}

} // namespace parser
