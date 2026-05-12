#include "env.hpp"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

#include "../../features/shell_text/shell_text.hpp"
#include "../../parser/assignments/assignment.hpp"

extern char **environ;

namespace builtins::env {

namespace {

void print_invalid_identifier(
    const parser::ast::SimpleCommand &cmd, const std::string &command,
    const std::string &name,
    std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    shell::diagnostics::add_error(diagnostics, cmd.span,
                                  command + ": '" + name +
                                      "' is not a valid identifier");
}

std::string quote_shell_value(const std::string &value) {
    std::string quoted = "'";

    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
            continue;
        }

        quoted.push_back(c);
    }

    quoted.push_back('\'');
    return quoted;
}

int print_variables(const shell::ShellState &state, bool exported_only,
                    const std::string &prefix) {
    std::map<std::string, shell::ShellVariable> sorted(state.variables.begin(),
                                                       state.variables.end());
    for (const auto &[name, var] : sorted) {
        if (exported_only && !var.exported) {
            continue;
        }

        if (!prefix.empty()) {
            std::cout << prefix;
        }
        std::cout << name << "=" << quote_shell_value(var.value) << "\n";
    }

    return 0;
}

int run_variable_list(
    const parser::ast::SimpleCommand &cmd, const std::string &command,
    const shell::ShellState &state, bool exported_only,
    const std::string &prefix,
    std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() != 1) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      command + ": unexpected arguments");
        return 1;
    }

    return print_variables(state, exported_only, prefix);
}

bool is_variable_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

} // namespace

const shell::ShellVariable *find_variable(const shell::ShellState &state,
                                          const std::string &name) {
    auto it = state.variables.find(name);
    if (it == state.variables.end()) {
        return nullptr;
    }

    return &it->second;
}

std::string get_variable_value(const shell::ShellState &state,
                               const std::string &name) {
    const shell::ShellVariable *var = find_variable(state, name);

    if (var == nullptr)
        return "";

    return var->value;
}

void set_shell_variable(shell::ShellState &state, std::string name,
                        std::string value) {
    auto it = state.variables.find(name);

    if (it == state.variables.end()) {
        state.variables[name] = shell::ShellVariable{value, false};
        return;
    }

    shell::ShellVariable old = it->second;
    old.value = value;
    it->second = old;

    if (it->second.exported && setenv(name.c_str(), value.c_str(), 1) == -1) {
        perror("setenv");
    }
}

void export_variable(shell::ShellState &state, const std::string &name) {
    auto [it, inserted] =
        state.variables.emplace(name, shell::ShellVariable{"", false});

    it->second.exported = true;

    if (setenv(name.c_str(), it->second.value.c_str(), 1) == -1) {
        perror("setenv");
    }
}

void unset_variable(shell::ShellState &state, const std::string &name) {
    const shell::ShellVariable *var = find_variable(state, name);

    if (var == nullptr) {
        return;
    }

    if (var->exported) {
        if (unsetenv(name.c_str()) == -1) {
            perror("unsetenv");
        }
    }

    state.variables.erase(name);
}

void import_process_environment(shell::ShellState &state) {
    for (char **env = environ; *env != nullptr; ++env) {
        std::string entry = std::string(*env);
        size_t sep = entry.find('=');
        std::string name = entry.substr(0, sep);
        std::string value = entry.substr(sep + 1, std::string::npos);

        state.variables[name] = shell::ShellVariable{value, true};
    }
}

std::string expand_variable_reference(const shell::ShellState &state,
                                      const std::string &source,
                                      size_t dollar_offset,
                                      size_t &end_offset) {
    end_offset = dollar_offset + 1;
    if (dollar_offset >= source.size() || source[dollar_offset] != '$' ||
        end_offset >= source.size()) {
        return "$";
    }

    if (source[end_offset] == '?') {
        ++end_offset;
        return std::to_string(state.last_status);
    }

    if (!is_variable_name_char(source[end_offset])) {
        return "$";
    }

    while (end_offset < source.size() &&
           is_variable_name_char(source[end_offset])) {
        ++end_offset;
    }

    return get_variable_value(
        state,
        source.substr(dollar_offset + 1, end_offset - dollar_offset - 1));
}

std::string expand_variables(const shell::ShellState &state,
                             const std::string &line) {
    std::vector<features::shell_text::Replacement> replacements;
    features::shell_text::for_each_unescaped_position(
        line,
        [&](size_t &i, const features::shell_text::ScanState &scan_state) {
            if (scan_state.in_single_quote || line[i] != '$' ||
                i + 1 >= line.size()) {
                return true;
            }

            size_t end = i;
            std::string text = expand_variable_reference(state, line, i, end);
            if (end == i + 1 && text == "$") {
                return true;
            }

            replacements.push_back(
                features::shell_text::Replacement{i, end, text});
            i = end - 1;
            return true;
        });

    return features::shell_text::apply_replacements(line, replacements);
}

int run_set(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
            std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    return run_variable_list(cmd, "set", state, false, "", diagnostics);
}

int run_export(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
               std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() == 1) {
        return run_variable_list(cmd, "export", state, true, "export ",
                                 diagnostics);
    }

    int status = 0;
    for (size_t i = 1; i < cmd.invocation->words.size(); ++i) {
        const std::string &arg = cmd.invocation->words[i].text;
        if (arg == "-p") {
            shell::diagnostics::add_error(diagnostics, cmd.span,
                                          "export: unexpected arguments");
            return 1;
        }

        std::string name;
        std::string value;
        if (parser::split_assignment_expression(arg, name, value)) {
            if (!parser::is_valid_variable_name(name)) {
                print_invalid_identifier(cmd, "export", name, diagnostics);
                status = 1;
                continue;
            }

            set_shell_variable(state, name, value);
            export_variable(state, name);
            continue;
        }

        if (!parser::is_valid_variable_name(arg)) {
            print_invalid_identifier(cmd, "export", arg, diagnostics);
            status = 1;
            continue;
        }

        export_variable(state, arg);
    }

    return status;
}

int run_unset(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
              std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() < 2) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      "unset: expected assignments");
        return 1;
    }

    int status = 0;
    for (size_t i = 1; i < cmd.invocation->words.size(); ++i) {
        if (!parser::is_valid_variable_name(cmd.invocation->words[i].text)) {
            print_invalid_identifier(
                cmd, "unset", cmd.invocation->words[i].text, diagnostics);
            status = 1;
            continue;
        }

        unset_variable(state, cmd.invocation->words[i].text);
    }

    return status;
}

} // namespace builtins::env
