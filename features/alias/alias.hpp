#pragma once

#include "../../shell/shell.hpp"
#include <string>

bool handle_alias_builtin(ShellState &state, const std::string &line);
std::string expand_aliases(const ShellState &state, const std::string &line);
