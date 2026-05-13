#include "exec.hpp"

#include <array>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "../../builtins/builtins.hpp"
#include "../../builtins/env/envexec/envexec.hpp"

namespace shell::exec {
namespace {

struct SavedStdio {
    int stdin_fd = -1;
    int stdout_fd = -1;
};

struct EnvironmentBlock {
    std::vector<std::string> storage;
    std::vector<char *> envp;
    std::string path_override;
    bool has_path_override = false;
};

std::vector<char *> build_argv(const std::vector<parser::ast::Word> &args) {
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);

    for (const parser::ast::Word &s : args) {
        argv.push_back(const_cast<char *>(s.text.c_str()));
    }

    argv.push_back(nullptr);
    return argv;
}

EnvironmentBlock
build_envp(const ShellState &state,
           const std::vector<parser::ast::Assignment> &assignments) {
    std::unordered_map<std::string, std::string> env_map;
    env_map.reserve(state.variables.size() + assignments.size());

    for (const auto &[name, variable] : state.variables) {
        if (variable.exported) {
            env_map[name] = variable.value;
        }
    }

    EnvironmentBlock block{};
    for (const parser::ast::Assignment &assignment : assignments) {
        env_map[assignment.name.text] = assignment.value.text;
        if (assignment.name.text == "PATH") {
            block.path_override = assignment.value.text;
            block.has_path_override = true;
        }
    }

    block.storage.reserve(env_map.size());
    block.envp.reserve(env_map.size() + 1);

    for (const auto &[name, value] : env_map) {
        block.storage.push_back(name + "=" + value);
    }

    for (std::string &entry : block.storage) {
        block.envp.push_back(const_cast<char *>(entry.c_str()));
    }
    block.envp.push_back(nullptr);

    return block;
}

struct redirects {
    std::string input_file{};
    std::string output_file{};
    bool append_output = false;
};

} // namespace

bool apply_redirections(const parser::ast::SimpleCommand &cmd) {
    redirects redirects{};

    for (auto &red : cmd.redirects) {
        switch (red.kind) {
        case parser::ast::RedirectKind::Input:
            redirects.input_file = red.target.text;
            break;
        case parser::ast::RedirectKind::Output:
            redirects.output_file = red.target.text;
            break;
        case parser::ast::RedirectKind::AppendOutput:
            redirects.output_file = red.target.text;
            redirects.append_output = true;
            break;
        }
    }

    if (!redirects.input_file.empty()) {
        int fd = open(redirects.input_file.c_str(), O_RDONLY);
        if (fd == -1) {
            perror("open input");
            return false;
        }

        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2 input");
            close(fd);
            return false;
        }

        close(fd);
    }

    if (!redirects.output_file.empty()) {
        int fd;

        if (redirects.append_output) {
            fd = open(redirects.output_file.c_str(),
                      O_WRONLY | O_CREAT | O_APPEND, 0644);
        } else {
            fd = open(redirects.output_file.c_str(),
                      O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }

        if (fd == -1) {
            perror("open output");
            return false;
        }

        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("dup2 output");
            close(fd);
            return false;
        }

        close(fd);
    }

    return true;
}

void apply_child_pipes(size_t index, size_t count,
                       const std::vector<std::array<int, 2>> &fds) {
    if (index > 0) {
        if (dup2(fds[index - 1][0], STDIN_FILENO) == -1) {
            perror("dup2 stdin");
            _exit(1);
        }
    }

    if (index + 1 < count) {
        if (dup2(fds[index][1], STDOUT_FILENO) == -1) {
            perror("dup2 stdout");
            _exit(1);
        }
    }

    for (auto &fdpair : fds) {
        close(fdpair[0]);
        close(fdpair[1]);
    }
}

void close_pipe_fds(std::vector<std::array<int, 2>> &fds) {
    for (std::array<int, 2> &fdpair : fds) {
        if (fdpair[0] != -1) {
            close(fdpair[0]);
            fdpair[0] = -1;
        }
        if (fdpair[1] != -1) {
            close(fdpair[1]);
            fdpair[1] = -1;
        }
    }
}

[[noreturn]] void exec_external(ShellState &state,
                                const parser::ast::SimpleCommand &cmd) {
    if (!cmd.invocation) {
        builtins::env::apply_temporary_assignments(state, cmd.assignments);
        std::cout.flush();
        std::cerr.flush();
        _exit(0);
    }

    EnvironmentBlock env = build_envp(state, cmd.assignments);
    if (env.has_path_override &&
        setenv("PATH", env.path_override.c_str(), 1) == -1) {
        perror("setenv PATH");
        _exit(1);
    }

    std::vector<char *> argv = build_argv(cmd.invocation->words);
    execvpe(argv[0], argv.data(), env.envp.data());
    perror(argv[0]);
    _exit(errno == ENOENT ? 127 : 126);
}

namespace {

bool save_stdio(SavedStdio &saved) {
    saved = SavedStdio{};

    saved.stdin_fd = dup(STDIN_FILENO);
    if (saved.stdin_fd == -1) {
        perror("dup stdin");
        return false;
    }

    saved.stdout_fd = dup(STDOUT_FILENO);
    if (saved.stdout_fd == -1) {
        perror("dup stdout");
        close(saved.stdin_fd);
        saved.stdin_fd = -1;
        return false;
    }

    return true;
}

void restore_stdio(const SavedStdio &saved) {
    if (saved.stdin_fd != -1) {
        if (dup2(saved.stdin_fd, STDIN_FILENO) == -1) {
            perror("dup2 restore stdin");
        }
        close(saved.stdin_fd);
    }

    if (saved.stdout_fd != -1) {
        if (dup2(saved.stdout_fd, STDOUT_FILENO) == -1) {
            perror("dup2 restore stdout");
        }
        close(saved.stdout_fd);
    }
}

} // namespace

int run_parent_assignments_with_redirections(
    ShellState &state, const parser::ast::SimpleCommand &cmd) {
    SavedStdio saved{};
    if (!save_stdio(saved)) {
        return 1;
    }

    std::cout.flush();

    if (!apply_redirections(cmd)) {
        restore_stdio(saved);
        return 1;
    }

    builtins::env::apply_persistent_assignments(state, cmd.assignments);

    std::cout.flush();

    restore_stdio(saved);
    return 0;
}

int run_parent_builtin_with_redirections(
    ShellState &state, const parser::ast::SimpleCommand &cmd,
    const builtins::BuiltinPlan &plan,
    std::vector<diagnostics::Diagnostic> &diagnostics) {
    SavedStdio saved{};
    if (!save_stdio(saved)) {
        return 1;
    }

    std::cout.flush();

    if (!apply_redirections(cmd)) {
        restore_stdio(saved);
        return 1;
    }

    builtins::env::AssignmentSnapshot snapshot;
    if (!cmd.assignments.empty()) {
        snapshot =
            builtins::env::apply_temporary_assignments(state, cmd.assignments);
    }

    int status = builtins::run_builtin(state, cmd, plan.kind, diagnostics);

    if (!cmd.assignments.empty()) {
        builtins::env::restore_temporary_assignments(state, snapshot);
    }

    std::cout.flush();

    restore_stdio(saved);

    return status;
}

} // namespace shell::exec
