#include "envexec.hpp"

#include "../env.hpp"

namespace builtins::env {

void apply_persistent_assignments(
    shell::ShellState &state,
    const std::vector<parser::Assignment> &assignments) {
    for (parser::Assignment assign : assignments) {
        set_shell_variable(state, assign.name, assign.value);
    }
}

AssignmentSnapshot apply_temporary_assignments(
    shell::ShellState &state,
    const std::vector<parser::Assignment> &assignments) {
    AssignmentSnapshot old{};

    for (const parser::Assignment &assign : assignments) {
        const shell::ShellVariable *var = find_variable(state, assign.name);
        bool existed = var != nullptr;

        if (existed) {
            old[assign.name] = SavedVariable{true, *var};
            state.variables[assign.name] =
                shell::ShellVariable{assign.value, var->exported};
        } else {
            state.variables[assign.name] =
                shell::ShellVariable{assign.value, false};
        }

        if (setenv(assign.name.c_str(), assign.value.c_str(), 1) == -1) {
            perror("setenv");
        }
    }

    return old;
}

void restore_temporary_assignments(shell::ShellState &state,
                                   const AssignmentSnapshot &snapshot) {
    for (auto &[name, savedVar] : snapshot) {
        if (savedVar.existed) {
            state.variables[name] = savedVar.old_value;
        } else {
            state.variables.erase(name);
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
