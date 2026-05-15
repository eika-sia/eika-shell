#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../../parser/ast.hpp"
#include "../../shell/shell.hpp"

namespace builtins::env {

std::unordered_map<std::string, shell::ShellVariable>
flatten_variables(const std::vector<shell::Scope> &scopes);
int find_scope(const shell::ShellState &state, const std::string &name);
const shell::ShellVariable *find_variable(const shell::ShellState &state,
                                          const std::string &name);

std::string get_variable_value(const shell::ShellState &state,
                               const std::string &name);

void set_shell_variable(shell::ShellState &state, std::string name,
                        std::string value, int scope);
void import_process_environment(shell::ShellState &state);

std::string expand_variable_reference(const shell::ShellState &state,
                                      const std::string &source,
                                      size_t dollar_offset, size_t &end_offset);
std::string expand_variables(const shell::ShellState &state,
                             const std::string &line);

// builtin layer of helpers
int run_update_value(shell::ShellState &state,
                     const parser::ast::SimpleCommand &cmd,
                     std::vector<shell::diagnostics::Diagnostic> &diagnostics);
int run_let(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
            std::vector<shell::diagnostics::Diagnostic> &diagnostics);
int run_set(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
            std::vector<shell::diagnostics::Diagnostic> &diagnostics);
int run_export(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
               std::vector<shell::diagnostics::Diagnostic> &diagnostics);
int run_unset(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
              std::vector<shell::diagnostics::Diagnostic> &diagnostics);
} // namespace builtins::env
