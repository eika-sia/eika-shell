#include "config_paths.hpp"

#include <unistd.h>

namespace shell::config {
namespace {

const ShellVariable *find_variable(const ShellState &state,
                                   const std::string &name) {
    const auto it = state.variables.find(name);
    if (it == state.variables.end()) {
        return nullptr;
    }

    return &it->second;
}

std::string variable_value(const ShellState &state, const std::string &name) {
    const ShellVariable *var = find_variable(state, name);
    return var == nullptr ? "" : var->value;
}

bool path_exists(const std::string &path) {
    return access(path.c_str(), F_OK) == 0;
}

bool is_path_like(const std::string &path) {
    return path.find('/') != std::string::npos;
}

std::string join_path(const std::string &base, const std::string &name) {
    if (base.empty()) {
        return name;
    }

    if (base.back() == '/') {
        return base + name;
    }

    return base + "/" + name;
}

std::string home_directory(const ShellState &state) {
    return variable_value(state, "HOME");
}

std::string xdg_config_home(const ShellState &state) {
    const std::string configured = variable_value(state, "XDG_CONFIG_HOME");
    if (!configured.empty()) {
        return configured;
    }

    const std::string home = home_directory(state);
    return home.empty() ? "" : join_path(home, ".config");
}

std::string xdg_data_home(const ShellState &state) {
    const std::string configured = variable_value(state, "XDG_DATA_HOME");
    if (!configured.empty()) {
        return configured;
    }

    const std::string home = home_directory(state);
    return home.empty() ? "" : join_path(home, ".local/share");
}

std::string primary_config_path(const ShellState &state) {
    const std::string config_home = xdg_config_home(state);
    return config_home.empty() ? "" : join_path(config_home, "esh/eshrc");
}

std::string legacy_config_path(const ShellState &state) {
    const std::string home = home_directory(state);
    return home.empty() ? "" : join_path(home, ".eshrc");
}

void add_if_nonempty(std::vector<std::string> &paths, std::string path) {
    if (!path.empty()) {
        paths.push_back(path);
    }
}

} // namespace

std::string startup_config_path(const ShellState &state) {
    const std::string primary = primary_config_path(state);
    if (!primary.empty() && path_exists(primary)) {
        return primary;
    }

    const std::string legacy = legacy_config_path(state);
    if (!legacy.empty() && path_exists(legacy)) {
        return legacy;
    }

    return "";
}

std::string history_path(const ShellState &state) {
    const std::string data_home = xdg_data_home(state);
    return data_home.empty() ? "" : join_path(data_home, "esh/history");
}

std::string legacy_history_path(const ShellState &state) {
    const std::string home = home_directory(state);
    return home.empty() ? "" : join_path(home, ".eshrc_history");
}

std::string config_scripts_path(const ShellState &state) {
    const std::string config_home = xdg_config_home(state);
    return config_home.empty() ? "" : join_path(config_home, "esh/scripts");
}

std::string data_scripts_path(const ShellState &state) {
    const std::string data_home = xdg_data_home(state);
    return data_home.empty() ? "" : join_path(data_home, "esh/scripts");
}

std::vector<std::string> script_search_paths(const ShellState &state) {
    std::vector<std::string> paths;
    add_if_nonempty(paths, config_scripts_path(state));
    add_if_nonempty(paths, data_scripts_path(state));

    return paths;
}

std::string resolve_source_path(const ShellState &state,
                                const std::string &requested_path) {
    if (requested_path.empty() || is_path_like(requested_path)) {
        return requested_path;
    }

    for (const std::string &base : script_search_paths(state)) {
        const std::string candidate = join_path(base, requested_path);
        if (path_exists(candidate)) {
            return candidate;
        }
    }

    return "";
}

} // namespace shell::config
