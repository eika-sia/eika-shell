#pragma once

#include <string>
#include <vector>

namespace features {

bool looks_like_path_token(const std::string &token);
std::vector<std::string> complete_path_token(const std::string &token);
std::vector<std::string> complete_command_token(const std::string &token);

} // namespace features
