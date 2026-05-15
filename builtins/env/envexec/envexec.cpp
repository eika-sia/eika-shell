#include "envexec.hpp"

#include "../env.hpp"

#include <cstddef>

namespace builtins::env {

AssignmentSnapshot apply_temporary_assignments(
    shell::ShellState &state,
    const std::vector<parser::ast::Assignment> &assignments, int scope) {
    AssignmentSnapshot old{};
    const std::size_t target_scope = static_cast<std::size_t>(scope);

    for (const parser::ast::Assignment &assign : assignments) {
        const int existing_scope = find_scope(state, assign.name.text);
        const shell::ShellVariable *var =
            existing_scope == -1 ? nullptr
                                 : find_variable(state, assign.name.text);
        bool existed = var != nullptr;

        if (old.count(assign.name.text) == 0) {
            old[assign.name.text] =
                existed ? SavedVariable{true, existing_scope, *var}
                        : SavedVariable{false, -1, {}};
        }

        if (existed) {
            state.scopes[target_scope].variables[assign.name.text] =
                shell::ShellVariable{assign.value.text, var->exported};
        } else {
            state.scopes[target_scope].variables[assign.name.text] =
                shell::ShellVariable{assign.value.text, false};
        }

        if (setenv(assign.name.text.c_str(), assign.value.text.c_str(), 1) ==
            -1) {
            perror("setenv");
        }
    }

    return old;
}

void restore_temporary_assignments(shell::ShellState &state,
                                   const AssignmentSnapshot &snapshot,
                                   int scope) {
    const std::size_t target_scope = static_cast<std::size_t>(scope);

    for (const auto &[name, savedVar] : snapshot) {
        state.scopes[target_scope].variables.erase(name);

        if (savedVar.existed && savedVar.scope >= 0 &&
            static_cast<std::size_t>(savedVar.scope) < state.scopes.size()) {
            state.scopes[static_cast<std::size_t>(savedVar.scope)]
                .variables[name] = savedVar.old_value;
        }

        if (!savedVar.existed || !savedVar.old_value.exported) {
            if (unsetenv(name.c_str()) == -1) {
                perror("unsetenv");
            }
        } else {
            if (setenv(name.c_str(), savedVar.old_value.value.c_str(), 1) ==
                -1) {
                perror("setenv");
            }
        }
    }
}

} // namespace builtins::env
