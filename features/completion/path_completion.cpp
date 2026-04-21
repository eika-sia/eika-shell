#include "path_completion.hpp"
#include "completion_format.hpp"

#include <algorithm>
#include <dirent.h>
#include <filesystem>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "../../builtins/builtins.hpp"
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

bool is_executable_file(const std::string &path) {
    return access(path.c_str(), X_OK) == 0 && !is_directory(path);
}

int command_candidate_priority(CompletionDisplayKind kind) {
    switch (kind) {
    case CompletionDisplayKind::Alias:
        return 3;
    case CompletionDisplayKind::Builtin:
        return 2;
    case CompletionDisplayKind::Executable:
        return 1;
    case CompletionDisplayKind::Plain:
    case CompletionDisplayKind::Directory:
        return 0;
    }

    return 0;
}

void record_command_candidate(
    std::map<std::string, CompletionDisplayKind> &matches,
    const std::string &name, CompletionDisplayKind kind) {
    const auto [it, inserted] = matches.emplace(name, kind);
    if (!inserted && command_candidate_priority(kind) >
                         command_candidate_priority(it->second)) {
        it->second = kind;
    }
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
                    PathCompletionOptions options) {
    std::vector<CompletionCandidate> results;

    const std::string dir_part = get_directory_part(logical_prefix);
    const std::string base_part = get_basename_part(logical_prefix);
    const std::string expanded_dir = expand_tilde_prefix(state, dir_part);

    for (const std::string &name :
         list_directory_matches(expanded_dir, base_part)) {
        const std::string full_path = (dir_part == ".") ? ("./" + name)
                                      : (dir_part == "/")
                                          ? ("/" + name)
                                          : (expanded_dir + "/" + name);

        const bool candidate_is_directory = is_directory(full_path);
        const bool candidate_is_executable = is_executable_file(full_path);
        if (options.executable_only && !candidate_is_directory &&
            !candidate_is_executable) {
            continue;
        }

        CompletionCandidate c{};
        if (dir_part == ".") {
            c.text = options.keep_current_dir_prefix ? "./" + name : name;
        } else if (dir_part == "/") {
            c.text = "/" + name;
        } else {
            c.text = dir_part + "/" + name;
        }

        c.kind = candidate_is_directory   ? CompletionDisplayKind::Directory
                 : candidate_is_executable ? CompletionDisplayKind::Executable
                                           : CompletionDisplayKind::Plain;
        results.push_back(std::move(c));
    }

    return results;
}

std::vector<CompletionCandidate>
complete_command_token(const shell::ShellState &state,
                       const std::string &logical_prefix) {
    std::map<std::string, CompletionDisplayKind> unique_matches;

    for_each_path_directory(state, [&](const std::string &dir) {
        std::vector<std::string> names =
            list_directory_matches(dir, logical_prefix);

        for (const std::string &name : names) {
            const std::string full_path = dir + "/" + name;

            if (is_executable_file(full_path)) {
                record_command_candidate(unique_matches, name,
                                         CompletionDisplayKind::Executable);
            }
        }

        return false;
    });

    for (const std::string &name : builtins::builtin_names()) {
        if (name.compare(0, logical_prefix.size(), logical_prefix) == 0) {
            record_command_candidate(unique_matches, name,
                                     CompletionDisplayKind::Builtin);
        }
    }

    for (const auto &[name, value] : state.alias) {
        (void)value;
        if (name.compare(0, logical_prefix.size(), logical_prefix) == 0) {
            record_command_candidate(unique_matches, name,
                                     CompletionDisplayKind::Alias);
        }
    }

    std::vector<CompletionCandidate> result;
    result.reserve(unique_matches.size());

    for (const auto &[match, kind] : unique_matches) {
        result.push_back(CompletionCandidate{match, kind});
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
           is_executable_file(resolved);
}

std::string resolve_command_in_path(const shell::ShellState &state,
                                    const std::string &token) {
    if (token.empty() || looks_like_path_token(token)) {
        return "";
    }

    std::string resolved;
    for_each_path_directory(state, [&](const std::string &dir) {
        const std::string full_path = dir + "/" + token;
        if (is_executable_file(full_path)) {
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
