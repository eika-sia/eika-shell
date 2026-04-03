#pragma once

#include "../../../parser/parser.hpp"
#include "../../../shell/shell.hpp"

namespace builtins::env {

struct SavedVariable {
    bool existed = false;
    shell::ShellVariable old_value;
};

using AssignmentSnapshot = std::unordered_map<std::string, SavedVariable>;

void apply_persistent_assignments(
    shell::ShellState &state,
    const std::vector<parser::Assignment> &assignments);

AssignmentSnapshot
apply_temporary_assignments(shell::ShellState &state,
                            const std::vector<parser::Assignment> &assignments);

void restore_temporary_assignments(shell::ShellState &state,
                                   const AssignmentSnapshot &snapshot);

} // namespace builtins::env
