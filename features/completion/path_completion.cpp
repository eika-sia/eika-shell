#include "path_completion.hpp"

#include <algorithm>
#include <dirent.h>
#include <filesystem>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "../../builtins/env/env.hpp"

namespace features {
namespace {

std::string get_directory_part(const std::string &token) {
    size_t pos = token.find_last_of('/');

    if (pos == std::string::npos) {
        return ".";
    }

    if (pos == 0) {
        return "/";
    }

    return token.substr(0, pos);
}

std::string get_basename_part(const std::string &token) {
    size_t pos = token.find_last_of('/');

    if (pos == std::string::npos) {
        return token;
    }

    return token.substr(pos + 1);
}

std::vector<std::string> list_directory_matches(const std::string &dir,
                                                const std::string &prefix) {
    std::vector<std::string> res;

    DIR *dirp = opendir(dir.c_str());
    if (!dirp) {
        return res;
    }

    struct dirent *dp;
    while ((dp = readdir(dirp)) != nullptr) {
        std::string name = dp->d_name;
        if (name == "." || name == "..")
            continue;

        if (name.compare(0, prefix.size(), prefix) == 0) {
            res.push_back(name);
        }
    }

    closedir(dirp);
    std::sort(res.begin(), res.end());
    return res;
}

bool is_directory(const std::string &path) {
    struct stat s;
    if (stat(path.c_str(), &s) != 0) {
        return false;
    }

    return S_ISDIR(s.st_mode);
}

} // namespace

std::string expand_tilde_prefix(const shell::ShellState &state,
                                const std::string &token) {
    if (token.empty() || token[0] != '~') {
        return token;
    }

    const shell::ShellVariable *home =
        builtins::env::find_variable(state, "HOME");
    if (!home) {
        return token;
    }

    return home->value + token.substr(1);
}

bool looks_like_path_token(const std::string &token) {
    if (token.empty()) {
        return false;
    }

    if (token[0] == '~' || token[0] == '.') {
        return true;
    }

    return token.find('/') != std::string::npos;
}

std::vector<std::string> complete_path_token(const shell::ShellState &state,
                                             const std::string &token) {
    std::vector<std::string> results;

    std::string dir_part = get_directory_part(token);
    std::string base_part = get_basename_part(token);

    std::string expanded_dir = expand_tilde_prefix(state, dir_part);
    std::vector<std::string> names =
        list_directory_matches(expanded_dir, base_part);

    for (const std::string &name : names) {
        std::string candidate;

        if (dir_part == ".") {
            candidate = name;
        } else if (dir_part == "/") {
            candidate = "/" + name;
        } else {
            candidate = dir_part + "/" + name;
        }

        std::string full_path;
        if (dir_part == ".") {
            full_path = "./" + name;
        } else if (dir_part == "/") {
            full_path = "/" + name;
        } else {
            full_path = expanded_dir + "/" + name;
        }

        if (is_directory(full_path)) {
            candidate += "/";
        }

        results.push_back(candidate);
    }

    return results;
}

std::vector<std::string> complete_command_token(const shell::ShellState &state,
                                                const std::string &token) {
    std::set<std::string> unique_matches;

    const std::string path_str =
        builtins::env::get_variable_value(state, "PATH");
    if (path_str.empty()) {
        return {};
    }

    size_t start = 0;

    while (start <= path_str.size()) {
        size_t end = path_str.find(':', start);
        std::string dir;

        if (end == std::string::npos) {
            dir = path_str.substr(start);
            start = path_str.size() + 1;
        } else {
            dir = path_str.substr(start, end - start);
            start = end + 1;
        }

        if (dir.empty()) {
            dir = ".";
        }

        std::vector<std::string> names = list_directory_matches(dir, token);

        for (const std::string &name : names) {
            std::string full_path = dir + "/" + name;

            if (access(full_path.c_str(), X_OK) == 0 &&
                !is_directory(full_path)) {
                unique_matches.insert(name);
            }
        }
    }

    return std::vector<std::string>(unique_matches.begin(),
                                    unique_matches.end());
}

bool path_exists(const shell::ShellState &state, const std::string &token) {
    namespace fs = std::filesystem;

    fs::path p(expand_tilde_prefix(state, token));

    if (p.is_relative()) {
        std::string base_dir = builtins::env::get_variable_value(state, "PWD");
        p = base_dir / p;
    }

    return fs::exists(p);
}

bool path_is_executable_file(const shell::ShellState &state,
                             const std::string &token) {
    namespace fs = std::filesystem;

    fs::path p(expand_tilde_prefix(state, token));

    if (p.is_relative()) {
        std::string base_dir = builtins::env::get_variable_value(state, "PWD");
        p = base_dir / p;
    }

    const std::string resolved = p.string();
    return fs::exists(p) && fs::is_regular_file(p) &&
           access(resolved.c_str(), X_OK) == 0;
}

bool command_exists_in_path(const shell::ShellState &state,
                            const std::string &token) {
    if (token.empty() || looks_like_path_token(token)) {
        return false;
    }

    const std::string path_str =
        builtins::env::get_variable_value(state, "PATH");
    if (path_str.empty()) {
        return false;
    }

    size_t start = 0;
    while (start <= path_str.size()) {
        const size_t end = path_str.find(':', start);
        std::string dir;

        if (end == std::string::npos) {
            dir = path_str.substr(start);
            start = path_str.size() + 1;
        } else {
            dir = path_str.substr(start, end - start);
            start = end + 1;
        }

        if (dir.empty()) {
            dir = ".";
        }

        const std::string full_path = dir + "/" + token;
        if (access(full_path.c_str(), X_OK) == 0 && !is_directory(full_path)) {
            return true;
        }
    }

    return false;
}

} // namespace features
