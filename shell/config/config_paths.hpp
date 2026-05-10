#pragma once

#include <string>
#include <vector>

#include "../shell.hpp"

namespace shell::config {

std::string startup_config_path(const ShellState &state);
std::string history_path(const ShellState &state);
std::string legacy_history_path(const ShellState &state);
std::string config_scripts_path(const ShellState &state);
std::string data_scripts_path(const ShellState &state);
std::vector<std::string> script_search_paths(const ShellState &state);
std::string resolve_source_path(const ShellState &state,
                                const std::string &requested_path);

} // namespace shell::config
