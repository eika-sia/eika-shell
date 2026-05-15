#pragma once

#include "../../../parser/ast.hpp"
#include "../../../shell/shell.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace builtins::env {

struct SavedVariable {
    bool existed = false;
    int scope = -1;
    shell::ShellVariable old_value;
};

using AssignmentSnapshot = std::unordered_map<std::string, SavedVariable>;

AssignmentSnapshot apply_temporary_assignments(
    shell::ShellState &state,
    const std::vector<parser::ast::Assignment> &assignments, int scope);

void restore_temporary_assignments(shell::ShellState &state,
                                   const AssignmentSnapshot &snapshot,
                                   int scope);

} // namespace builtins::env
