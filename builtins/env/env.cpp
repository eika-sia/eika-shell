#include "env.hpp"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

#include "../../parser/assignments/assignment.hpp"
#include "../../features/shell_text/shell_text.hpp"

extern char **environ;

namespace builtins::env {

namespace {

void print_invalid_identifier(const std::string &command,
                              const std::string &name) {
    std::cerr << command << ": `" << name << "': not a valid identifier\n";
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

int run_variable_list(const parser::Command &cmd, const std::string &command,
                      const shell::ShellState &state, bool exported_only,
                      const std::string &prefix) {
    if (cmd.args.size() != 1) {
        std::cerr << command << ": unexpected arguments\n";
        return 1;
    }

    return print_variables(state, exported_only, prefix);
}

} // namespace

const shell::ShellVariable *find_variable(const shell::ShellState &state,
                                          std::string name) {
    auto it = state.variables.find(name);
    if (it == state.variables.end()) {
        return nullptr;
    }

    return &it->second;
}

std::string get_variable_value(const shell::ShellState &state,
                               std::string name) {
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

void export_variable(shell::ShellState &state, std::string name) {
    auto [it, inserted] =
        state.variables.emplace(name, shell::ShellVariable{"", false});

    it->second.exported = true;

    if (setenv(name.c_str(), it->second.value.c_str(), 1) == -1) {
        perror("setenv");
    }
}

void unset_variable(shell::ShellState &state, std::string name) {
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

            size_t end = i + 1;
            std::string text;

            if (line[end] == '?') {
                text = std::to_string(state.last_status);
                ++end;
            } else {
                if (!(std::isalpha(static_cast<unsigned char>(line[end])) ||
                      line[end] == '_')) {
                    return true;
                }

                while (end < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[end])) ||
                        line[end] == '_')) {
                    ++end;
                }

                const std::string name = line.substr(i + 1, end - (i + 1));
                text = get_variable_value(state, name);
            }

            replacements.push_back(
                features::shell_text::Replacement{i, end, text});
            i = end - 1;
            return true;
        });

    return features::shell_text::apply_replacements(line, replacements);
}

int run_export_list(shell::ShellState &state, const parser::Command &cmd) {
    return run_variable_list(cmd, "export", state, true, "export ");
}

int run_set_list(shell::ShellState &state, const parser::Command &cmd) {
    return run_variable_list(cmd, "set", state, false, "");
}

int run_export_manage(shell::ShellState &state, const parser::Command &cmd) {
    if (cmd.args[0] == "export") {
        if (cmd.args.size() < 2) {
            std::cerr << "export: unexpected arguments\n";
            return 1;
        }

        int status = 0;
        for (size_t i = 1; i < cmd.args.size(); ++i) {
            const std::string &raw = cmd.args[i];
            if (raw == "-p") {
                std::cerr << "export: unexpected arguments\n";
                return 1;
            }

            std::string name;
            std::string value;
            if (parser::split_assignment_expression(raw, name, value)) {
                if (!parser::is_valid_variable_name(name)) {
                    print_invalid_identifier("export", name);
                    status = 1;
                    continue;
                }

                set_shell_variable(state, name, value);
                export_variable(state, name);
                continue;
            }

            if (!parser::is_valid_variable_name(raw)) {
                print_invalid_identifier("export", raw);
                status = 1;
                continue;
            }

            export_variable(state, raw);
        }

        return status;
    }

    if (cmd.args[0] == "unset") {
        if (cmd.args.size() < 2) {
            std::cerr << "unset: unexpected arguments\n";
            return 1;
        }

        int status = 0;
        for (size_t i = 1; i < cmd.args.size(); ++i) {
            if (!parser::is_valid_variable_name(cmd.args[i])) {
                print_invalid_identifier("unset", cmd.args[i]);
                status = 1;
                continue;
            }

            unset_variable(state, cmd.args[i]);
        }

        return status;
    }

    std::cerr << "export: what??\n";
    return 1;
}

} // namespace builtins::env
