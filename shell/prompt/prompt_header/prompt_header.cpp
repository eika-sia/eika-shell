#include "prompt_header.hpp"

#include <cerrno>
#include <fcntl.h>
#include <linux/limits.h>
#include <optional>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "../../../builtins/env/env.hpp"
#include "../../shell.hpp"

namespace shell::prompt::prompt_header {
namespace {

const std::string purple = "\033[1;35m";
const std::string cyan_bold = "\033[1;36m";
const std::string cyan = "\033[0;36m";
const std::string green = "\033[0;32m";
const std::string yellow = "\033[0;33m";
const std::string blue = "\033[0;34m";
const std::string bold_red = "\033[1;31m";
const std::string reset = "\033[0m";

struct GitPromptInfo {
    bool in_repo = false;
    bool detached = false;
    std::string ref_name;
    int ahead = 0;
    int behind = 0;
    int staged = 0;
    int unstaged = 0;
    int untracked = 0;
    int conflicted = 0;
};

size_t display_width(const std::string &text) {
    size_t width = 0;

    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);

        if (c == '\033') {
            ++i;
            if (i < text.size() && text[i] == '[') {
                ++i;
                while (i < text.size() &&
                       !((text[i] >= '@' && text[i] <= '~'))) {
                    ++i;
                }
                if (i < text.size()) {
                    ++i;
                }
            }
            continue;
        }

        if ((c & 0xC0) != 0x80) {
            ++width;
        }
        ++i;
    }

    return width;
}

std::string trim_whitespace(const std::string &text) {
    const size_t start = text.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return "";
    }

    const size_t end = text.find_last_not_of(" \t");
    return text.substr(start, end - start + 1);
}

std::string get_current_working_directory() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        return "";
    }

    return cwd;
}

std::optional<std::string>
capture_command_output(const std::vector<std::string> &argv) {
    if (argv.empty()) {
        return std::nullopt;
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return std::nullopt;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return std::nullopt;
    }

    if (pid == 0) {
        close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            _exit(127);
        }

        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd != -1) {
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }

        close(pipefd[1]);

        std::vector<char *> exec_argv;
        exec_argv.reserve(argv.size() + 1);
        for (const std::string &arg : argv) {
            exec_argv.push_back(const_cast<char *>(arg.c_str()));
        }
        exec_argv.push_back(nullptr);

        execvp(exec_argv[0], exec_argv.data());
        _exit(127);
    }

    close(pipefd[1]);

    std::string output;
    char buffer[4096];
    ssize_t nread = 0;
    while ((nread = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, static_cast<size_t>(nread));
    }
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            return std::nullopt;
        }
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return std::nullopt;
    }

    return output;
}

void parse_ahead_behind(std::string text, GitPromptInfo &info) {
    size_t start = 0;
    while (start < text.size()) {
        size_t comma = text.find(',', start);
        std::string part = trim_whitespace(
            text.substr(start, comma == std::string::npos ? std::string::npos
                                                          : comma - start));

        if (part.rfind("ahead ", 0) == 0) {
            info.ahead = std::stoi(part.substr(6));
        } else if (part.rfind("behind ", 0) == 0) {
            info.behind = std::stoi(part.substr(7));
        }

        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
}

void parse_git_branch_line(const std::string &line, GitPromptInfo &info) {
    if (line.rfind("## ", 0) != 0) {
        return;
    }

    info.in_repo = true;
    const std::string head = line.substr(3);

    if (head.rfind("No commits yet on ", 0) == 0) {
        info.ref_name = head.substr(std::string("No commits yet on ").size());
        return;
    }

    const size_t bracket_pos = head.find(" [");
    if (bracket_pos != std::string::npos && head.back() == ']') {
        parse_ahead_behind(
            head.substr(bracket_pos + 2, head.size() - bracket_pos - 3), info);
    }

    std::string branch_part =
        bracket_pos == std::string::npos ? head : head.substr(0, bracket_pos);
    if (branch_part.rfind("HEAD", 0) == 0) {
        info.detached = true;
        return;
    }

    const size_t remote_sep = branch_part.find("...");
    if (remote_sep != std::string::npos) {
        branch_part = branch_part.substr(0, remote_sep);
    }

    info.ref_name = trim_whitespace(branch_part);
}

void parse_git_status_entry(const std::string &line, GitPromptInfo &info) {
    if (line.size() < 2) {
        return;
    }

    const char x = line[0];
    const char y = line[1];

    if (x == '?' && y == '?') {
        info.untracked++;
        return;
    }

    if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') ||
        (x == 'D' && y == 'D')) {
        info.conflicted++;
        return;
    }

    if (x != ' ' && x != '?') {
        info.staged++;
    }
    if (y != ' ' && y != '?') {
        info.unstaged++;
    }
}

GitPromptInfo query_git_prompt_info(const shell::ShellState &) {
    const std::string cwd = get_current_working_directory();
    if (cwd.empty()) {
        return {};
    }

    const std::optional<std::string> output = capture_command_output(
        {"git", "-C", cwd, "status", "--porcelain=v1", "--branch"});
    if (!output.has_value()) {
        return {};
    }

    GitPromptInfo info{};
    std::istringstream stream(*output);
    std::string line;
    bool first_line = true;

    while (std::getline(stream, line)) {
        if (first_line) {
            parse_git_branch_line(line, info);
            first_line = false;
            continue;
        }

        parse_git_status_entry(line, info);
    }

    if (info.in_repo && info.detached && info.ref_name.empty()) {
        const std::optional<std::string> head = capture_command_output(
            {"git", "-C", cwd, "rev-parse", "--short", "HEAD"});
        if (head.has_value()) {
            info.ref_name = trim_whitespace(*head);
        }
        if (info.ref_name.empty()) {
            info.ref_name = "HEAD";
        }
    }

    return info;
}

std::string build_location_segment(const shell::ShellState &state) {
    std::string res = "";

    const std::string cwd = get_current_working_directory();
    if (!cwd.empty()) {
        std::string path = cwd;

        const shell::ShellVariable *home =
            builtins::env::find_variable(state, "HOME");
        const std::string user =
            builtins::env::get_variable_value(state, "USER");
        const std::string display_user = user.empty() ? "shell" : user;

        if (home != nullptr && !home->value.empty()) {
            std::string h(home->value);
            if (path.compare(0, h.size(), h) == 0) {
                path = "~" + path.substr(h.size());
            }
        }

        res = purple + display_user + cyan_bold + " → " + path + reset;
    }

    return res;
}

std::string signal_label_from_status(int status) {
    switch (status) {
    case 2:
        return "SIGINT";
    case 3:
        return "SIGQUIT";
    case 9:
        return "SIGKILL";
    case 15:
        return "SIGTERM";
    default:
        return std::to_string(status);
    }
}

std::string build_status_segment(const shell::ShellState &state) {
    int status = state.last_status;
    if (status == 0) {
        return "";
    }

    return bold_red + "✘ " +
           (status >= 128 ? signal_label_from_status(status - 128)
                          : std::to_string(status)) +
           reset;
}

std::string build_background_segment(const shell::ShellState &state) {
    int count = 0;

    for (const process::ProcessInfo &proc : state.processes) {
        if (proc.running && proc.background)
            count++;
    }

    return count == 0 ? "" : cyan + "| bg: " + std::to_string(count) + reset;
}

std::string build_git_segment(const shell::ShellState &state) {
    const GitPromptInfo info = query_git_prompt_info(state);
    if (!info.in_repo || info.ref_name.empty()) {
        return "";
    }

    std::string segment = green + "on 󰊢 " + info.ref_name + reset;
    if (info.ahead > 0) {
        segment += " " + cyan + "↑" + std::to_string(info.ahead) + reset;
    }
    if (info.behind > 0) {
        segment += " " + cyan + "↓" + std::to_string(info.behind) + reset;
    }
    if (info.staged > 0) {
        segment += " " + green + "+" + std::to_string(info.staged) + reset;
    }
    if (info.unstaged > 0) {
        segment += " " + yellow + "~" + std::to_string(info.unstaged) + reset;
    }
    if (info.untracked > 0) {
        segment += " " + blue + "?" + std::to_string(info.untracked) + reset;
    }
    if (info.conflicted > 0) {
        segment +=
            " " + bold_red + "!" + std::to_string(info.conflicted) + reset;
    }

    return segment;
}

} // namespace

HeaderInfo build_header(const shell::ShellState &state) {
    std::string res = purple + "╭─" + reset;

    std::string loc = build_location_segment(state);
    std::string status = build_status_segment(state);
    std::string git = build_git_segment(state);
    std::string bg = build_background_segment(state);

    if (!loc.empty())
        res += " " + loc;
    if (!status.empty())
        res += " " + status;
    if (!git.empty())
        res += " " + git;
    if (!bg.empty())
        res += " " + bg;

    return HeaderInfo{res, display_width(res)};
}
} // namespace shell::prompt::prompt_header
