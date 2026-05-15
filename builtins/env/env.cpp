#include "env.hpp"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
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
                    bool global_only, const std::string &prefix) {
    std::unordered_map<std::string, shell::ShellVariable> flattened;
    if (global_only)
        flattened = state.scopes[0].variables;
    else
        flattened = flatten_variables(state.scopes);

    std::map<std::string, shell::ShellVariable> sorted(flattened.begin(),
                                                       flattened.end());
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
    const shell::ShellState &state, bool exported_only, bool global_only,
    const std::string &prefix,
    std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() != 1) {
        shell::diagnostics::add_error(diagnostics, cmd.span,
                                      command + ": unexpected arguments");
        return 1;
    }

    return print_variables(state, exported_only, global_only, prefix);
}

bool is_variable_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

const shell::ShellVariable *find_variable(const shell::ShellState &state,
                                          const std::string &name, int scope) {
    if (scope == -1)
        return nullptr;
    auto it = state.scopes[scope].variables.find(name);
    if (it != state.scopes[scope].variables.end())
        return &it->second;
    else
        return nullptr;
}

} // namespace

std::unordered_map<std::string, shell::ShellVariable>
flatten_variables(const std::vector<shell::Scope> &scopes) {
    std::unordered_map<std::string, shell::ShellVariable> flattened;

    for (size_t scope = 0; scope < scopes.size(); ++scope) {
        for (auto [name, value] : scopes[scope].variables)
            flattened[name] = value;
    }

    return flattened;
}

int find_scope(const shell::ShellState &state, const std::string &name) {
    for (int scope = state.scopes.size() - 1; scope >= 0; --scope) {
        if (state.scopes[scope].variables.count(name))
            return scope;
    }
    return -1;
}

const shell::ShellVariable *find_variable(const shell::ShellState &state,
                                          const std::string &name) {
    int scope = find_scope(state, name);
    if (scope == -1)
        return nullptr;
    auto it = state.scopes[scope].variables.find(name);
    if (it != state.scopes[scope].variables.end())
        return &it->second;
    else
        return nullptr;
}

std::string get_variable_value(const shell::ShellState &state,
                               const std::string &name) {
    const shell::ShellVariable *var = find_variable(state, name);

    if (var == nullptr)
        return "";

    return var->value;
}

void set_shell_variable(shell::ShellState &state, std::string name,
                        std::string value, int scope) {
    // NOTE: Internal hard override, do not use lightly
    const shell::ShellVariable *var = find_variable(state, name, scope);
    if (var == nullptr)
        return;

    if (!var->exported) {
        state.scopes[scope].variables[name] = {value, false};
        return;
    }

    if (setenv(name.c_str(), value.c_str(), 1) == -1) {
        perror("setenv");
        return;
    }
    state.scopes[scope].variables[name] = {value, true};
}

void import_process_environment(shell::ShellState &state) {
    for (char **env = environ; *env != nullptr; ++env) {
        std::string entry = std::string(*env);
        size_t sep = entry.find('=');
        std::string name = entry.substr(0, sep);
        std::string value = entry.substr(sep + 1, std::string::npos);

        state.scopes[0].variables[name] = shell::ShellVariable{value, true};
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

int run_update_value(shell::ShellState &state,
                     const parser::ast::SimpleCommand &cmd,
                     std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    int status = 0;

    for (const parser::ast::Assignment &assign : cmd.assignments) {
        const shell::ShellVariable *var =
            find_variable(state, assign.name.text);
        int scope = find_scope(state, assign.name.text);

        if (var == nullptr) {
            shell::diagnostics::add_error(diagnostics, assign.span,
                                          "variable " + assign.name.text +
                                              " not declared in any context");
            status = 1;
            continue;
        }

        if (!var->exported) {
            state.scopes[scope].variables[assign.name.text] = {
                assign.value.text, false};
            continue;
        }

        if (setenv(assign.name.text.c_str(), assign.value.text.c_str(), 1) ==
            -1) {
            perror("setenv");
            status = 1;
            continue;
        }
        state.scopes[scope].variables[assign.name.text] = {assign.value.text,
                                                           true};
    }

    return status;
}

int run_let(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
            std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() == 1)
        return run_variable_list(cmd, "let", state, false, false, "",
                                 diagnostics);

    int status = 0;
    int outer_scope = state.scopes.size() - 1;

    for (size_t i = 1; i < cmd.invocation->words.size(); ++i) {
        const std::string &arg = cmd.invocation->words[i].text;

        std::string name;
        std::string value;
        if (parser::split_assignment_expression(arg, name, value)) {
            if (!parser::is_valid_variable_name(name)) {
                print_invalid_identifier(cmd, "set", name, diagnostics);
                status = 1;
                continue;
            }

            const shell::ShellVariable *var = find_variable(state, name);
            int scope_idx = find_scope(state, name);

            if (var != nullptr && scope_idx == outer_scope) {
                shell::diagnostics::add_error(
                    diagnostics, cmd.invocation->words[i].span,
                    "variable already declared in this scope");
                status = 1;
                continue;
            }

            state.scopes[outer_scope].variables[name] = {value, false};
            continue;
        }

        shell::diagnostics::add_error(
            diagnostics, cmd.invocation->words[i].span,
            "variable attempted to be declared without initial value");
    }

    return status;
}

int run_set(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
            std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() == 1)
        return run_variable_list(cmd, "set", state, false, true, "global ",
                                 diagnostics);

    int status = 0;

    for (size_t i = 1; i < cmd.invocation->words.size(); ++i) {
        const std::string &arg = cmd.invocation->words[i].text;

        std::string name;
        std::string value;
        if (parser::split_assignment_expression(arg, name, value)) {
            if (!parser::is_valid_variable_name(name)) {
                print_invalid_identifier(cmd, "set", name, diagnostics);
                status = 1;
                continue;
            }

            state.scopes[0].variables[name] = {value, false};
            continue;
        }

        if (!parser::is_valid_variable_name(arg)) {
            print_invalid_identifier(cmd, "export", arg, diagnostics);
            status = 1;
            continue;
        }

        const shell::ShellVariable *var = find_variable(state, arg);
        int scope_idx = find_scope(state, arg);
        if (!var) {
            shell::diagnostics::add_error(
                diagnostics, cmd.invocation->words[i].span,
                "undeclared variable tried to set global");
            status = 1;
            continue;
        }

        state.scopes[0].variables[arg] = {var->value, false};
        if (scope_idx != 0)
            state.scopes[scope_idx].variables.erase(arg);
    }

    return status;
}

int run_export(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
               std::vector<shell::diagnostics::Diagnostic> &diagnostics) {
    if (cmd.invocation->words.size() == 1)
        return run_variable_list(cmd, "export", state, true, true, "export ",
                                 diagnostics);

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

            state.scopes[0].variables[name] = {value, true};
            if (setenv(name.c_str(), value.c_str(), 1) == -1) {
                perror("setenv");
                return 1;
            }
            continue;
        }

        if (!parser::is_valid_variable_name(arg)) {
            print_invalid_identifier(cmd, "export", arg, diagnostics);
            status = 1;
            continue;
        }

        const shell::ShellVariable *var = find_variable(state, arg);
        int scope_idx = find_scope(state, arg);
        if (!var) {
            shell::diagnostics::add_error(
                diagnostics, cmd.invocation->words[i].span,
                "undeclared variable tried to export");
            status = 1;
            continue;
        }

        if (setenv(arg.c_str(), var->value.c_str(), 1) == -1) {
            perror("setenv");
            continue;
        }

        // moves variable from local scope to global one (exported vars are by
        // necessarily considered global)
        state.scopes[0].variables[arg] = {var->value, true};
        if (scope_idx != 0)
            state.scopes[scope_idx].variables.erase(arg);
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

        const shell::ShellVariable *var =
            find_variable(state, cmd.invocation->words[i].text);
        int scope_idx = find_scope(state, cmd.invocation->words[i].text);

        if (var == nullptr) {
            status = 1;
            continue;
        }

        if (var->exported) {
            if (unsetenv(cmd.invocation->words[i].text.c_str()) == -1) {
                perror("unsetenv");
                continue;
            }
        }

        state.scopes[scope_idx].variables.erase(cmd.invocation->words[i].text);
    }

    return status;
}

} // namespace builtins::env
