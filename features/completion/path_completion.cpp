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

template <typename Callback>
bool for_each_path_directory(const shell::ShellState &state,
                             Callback &&callback) {
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

        if (callback(dir)) {
            return true;
        }
    }

    return false;
}

} // namespace

std::string get_basename_part(const std::string &token) {
    size_t pos = token.find_last_of('/');

    if (pos == std::string::npos) {
        return token;
    }

    return token.substr(pos + 1);
}

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

std::vector<CompletionCandidate>
complete_path_token(const shell::ShellState &state,
                    const std::string &logical_prefix,
                    bool preserve_dot_slash) {
    std::vector<CompletionCandidate> results;

    const std::string dir_part = get_directory_part(logical_prefix);
    const std::string base_part = get_basename_part(logical_prefix);
    const std::string expanded_dir = expand_tilde_prefix(state, dir_part);

    for (const std::string &name :
         list_directory_matches(expanded_dir, base_part)) {
        CompletionCandidate c{};

        if (dir_part == ".") {
            c.text = preserve_dot_slash ? "./" + name : name;
        } else if (dir_part == "/") {
            c.text = "/" + name;
        } else {
            c.text = dir_part + "/" + name;
        }

        const std::string full_path = (dir_part == ".") ? ("./" + name)
                                      : (dir_part == "/")
                                          ? ("/" + name)
                                          : (expanded_dir + "/" + name);

        c.is_directory = is_directory(full_path);
        results.push_back(std::move(c));
    }

    return results;
}

std::vector<CompletionCandidate>
complete_command_token(const shell::ShellState &state,
                       const std::string &logical_prefix) {
    std::set<std::string> unique_matches;

    for_each_path_directory(state, [&](const std::string &dir) {
        std::vector<std::string> names =
            list_directory_matches(dir, logical_prefix);

        for (const std::string &name : names) {
            const std::string full_path = dir + "/" + name;

            if (access(full_path.c_str(), X_OK) == 0 &&
                !is_directory(full_path)) {
                unique_matches.insert(name);
            }
        }

        return false;
    });

    std::vector<CompletionCandidate> result;

    for (std::string match : unique_matches) {
        result.push_back(CompletionCandidate{match, false});
    }
    return result;
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

std::string resolve_command_in_path(const shell::ShellState &state,
                                    const std::string &token) {
    if (token.empty() || looks_like_path_token(token)) {
        return "";
    }

    std::string resolved;
    for_each_path_directory(state, [&](const std::string &dir) {
        const std::string full_path = dir + "/" + token;
        if (access(full_path.c_str(), X_OK) == 0 && !is_directory(full_path)) {
            resolved = full_path;
            return true;
        }
        return false;
    });

    return resolved;
}

bool command_exists_in_path(const shell::ShellState &state,
                            const std::string &token) {
    return !resolve_command_in_path(state, token).empty();
}

} // namespace features
