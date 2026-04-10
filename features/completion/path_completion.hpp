#pragma once

#include <string>
#include <vector>

namespace shell {
struct ShellState;
}

namespace features {

std::string expand_tilde_prefix(const shell::ShellState &state,
                                const std::string &token);
bool looks_like_path_token(const std::string &token);
std::vector<std::string>
complete_path_token(const shell::ShellState &state, const std::string &token,
                    bool keep_current_dir_prefix = false);
std::vector<std::string> complete_command_token(const shell::ShellState &state,
                                                const std::string &token);

bool path_exists(const shell::ShellState &state, const std::string &token);
bool path_is_executable_file(const shell::ShellState &state,
                             const std::string &token);
std::string resolve_command_in_path(const shell::ShellState &state,
                                    const std::string &token);
bool command_exists_in_path(const shell::ShellState &state,
                            const std::string &token);
} // namespace features
