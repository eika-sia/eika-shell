#pragma once

#include "../../../parser/ast.hpp"
#include "../../../shell/shell.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace builtins::env {

struct SavedVariable {
    bool existed = false;
    shell::ShellVariable old_value;
};

using AssignmentSnapshot = std::unordered_map<std::string, SavedVariable>;

void apply_persistent_assignments(
    shell::ShellState &state,
    const std::vector<parser::ast::Assignment> &assignments);

AssignmentSnapshot apply_temporary_assignments(
    shell::ShellState &state,
    const std::vector<parser::ast::Assignment> &assignments);

void restore_temporary_assignments(shell::ShellState &state,
                                   const AssignmentSnapshot &snapshot);

} // namespace builtins::env
