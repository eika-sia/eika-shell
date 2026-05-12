#include "envexec.hpp"

#include "../env.hpp"

namespace builtins::env {

void apply_persistent_assignments(
    shell::ShellState &state,
    const std::vector<parser::ast::Assignment> &assignments) {
    for (const parser::ast::Assignment &assign : assignments) {
        set_shell_variable(state, assign.name.text, assign.value.text);
    }
}

AssignmentSnapshot apply_temporary_assignments(
    shell::ShellState &state,
    const std::vector<parser::ast::Assignment> &assignments) {
    AssignmentSnapshot old{};

    for (const parser::ast::Assignment &assign : assignments) {
        const shell::ShellVariable *var =
            find_variable(state, assign.name.text);
        bool existed = var != nullptr;

        if (old.count(assign.name.text) == 0) {
            old[assign.name.text] =
                existed ? SavedVariable{true, *var} : SavedVariable{false, {}};
        }

        if (existed) {
            state.variables[assign.name.text] =
                shell::ShellVariable{assign.value.text, var->exported};
        } else {
            state.variables[assign.name.text] =
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
