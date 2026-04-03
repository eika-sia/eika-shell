#pragma once

#include <string>

#include "../../shell/shell.hpp"

namespace builtins::env {
const shell::ShellVariable *find_variable(const shell::ShellState &state,
                                          std::string name);
std::string get_variable_value(const shell::ShellState &state,
                               std::string name);

void set_shell_variable(shell::ShellState &state, std::string name,
                        std::string value);
void export_variable(shell::ShellState &state, std::string name);
void unset_variable(shell::ShellState &state, std::string name);

void import_process_environment(shell::ShellState &state);
std::string expand_variables(const shell::ShellState &state,
                             const std::string &line);

int run_set_list(shell::ShellState &state, const parser::Command &cmd);
int run_export_manage(shell::ShellState &state, const parser::Command &cmd);
int run_export_list(shell::ShellState &state, const parser::Command &cmd);
} // namespace builtins::env
