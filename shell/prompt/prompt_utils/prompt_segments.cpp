#include "prompt_segments.hpp"

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

namespace shell::prompt::prompt_segments {
namespace {

std::string sgr(const char *code) { return std::string("\033[") + code + "m"; }

struct StyleToken {
    const char *name;
    const char *code;
};

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

std::string get_display_user(const shell::ShellState &state) {
    const std::string user = builtins::env::get_variable_value(state, "USER");
    return user.empty() ? "shell" : user;
}

std::string get_display_directory(const shell::ShellState &state) {
    std::string path = get_current_working_directory();
    if (path.empty()) {
        return path;
    }

    const shell::ShellVariable *home =
        builtins::env::find_variable(state, "HOME");
    if (home != nullptr && !home->value.empty() &&
        path.compare(0, home->value.size(), home->value) == 0) {
        path = "~" + path.substr(home->value.size());
    }

    return path;
}

std::string get_hostname_text() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == -1) {
        return "";
    }

    hostname[sizeof(hostname) - 1] = '\0';
    return hostname;
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
        const size_t comma = text.find(',', start);
        const std::string part = trim_whitespace(
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
    const std::string path = get_display_directory(state);
    if (path.empty()) {
        return "";
    }

    return get_display_user(state) + " → " + path;
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
    const int status = state.last_status;
    if (status == 0) {
        return "";
    }

    return "✘ " + (status >= 128 ? signal_label_from_status(status - 128)
                                 : std::to_string(status));
}

std::string build_background_segment(const shell::ShellState &state) {
    int count = 0;
    for (const process::ProcessInfo &proc : state.processes) {
        if (proc.running && proc.background) {
            ++count;
        }
    }

    return count == 0 ? "" : "| bg: " + std::to_string(count);
}

std::string build_git_segment(const shell::ShellState &state) {
    const GitPromptInfo info = query_git_prompt_info(state);
    if (!info.in_repo || info.ref_name.empty()) {
        return "";
    }

    std::string segment = "on 󰊢 " + info.ref_name;
    if (info.ahead > 0) {
        segment += " ↑" + std::to_string(info.ahead);
    }
    if (info.behind > 0) {
        segment += " ↓" + std::to_string(info.behind);
    }
    if (info.staged > 0) {
        segment += " +" + std::to_string(info.staged);
    }
    if (info.unstaged > 0) {
        segment += " ~" + std::to_string(info.unstaged);
    }
    if (info.untracked > 0) {
        segment += " ?" + std::to_string(info.untracked);
    }
    if (info.conflicted > 0) {
        segment += " !" + std::to_string(info.conflicted);
    }

    return segment;
}

std::optional<std::string> render_style_token(const std::string &token) {
    static const StyleToken style_tokens[] = {
        {"reset", "0"},
        {"fg_reset", "39"},
        {"bg_reset", "49"},
        {"bold", "1"},
        {"dim", "2"},
        {"underline", "4"},
        {"black", "30"},
        {"red", "31"},
        {"green", "32"},
        {"yellow", "33"},
        {"blue", "38;5;24"},
        {"magenta", "35"},
        {"purple", "35"},
        {"cyan", "36"},
        {"white", "97"},
        {"bright_black", "90"},
        {"bright_red", "91"},
        {"bright_green", "92"},
        {"bright_yellow", "93"},
        {"bright_blue", "94"},
        {"bright_magenta", "95"},
        {"bright_purple", "95"},
        {"bright_cyan", "96"},
        {"bright_white", "97"},
        {"bg_black", "40"},
        {"bg_red", "41"},
        {"bg_green", "42"},
        {"bg_yellow", "43"},
        {"bg_blue", "48;5;24"},
        {"bg_magenta", "45"},
        {"bg_purple", "45"},
        {"bg_cyan", "46"},
        {"bg_white", "107"},
        {"bg_bright_black", "100"},
        {"bg_bright_red", "101"},
        {"bg_bright_green", "102"},
        {"bg_bright_yellow", "103"},
        {"bg_bright_blue", "104"},
        {"bg_bright_magenta", "105"},
        {"bg_bright_purple", "105"},
        {"bg_bright_cyan", "106"},
        {"bg_bright_white", "107"},
        {"bold_black", "1;30"},
        {"bold_red", "1;31"},
        {"bold_green", "1;32"},
        {"bold_yellow", "1;33"},
        {"bold_blue", "1;38;5;24"},
        {"bold_magenta", "1;35"},
        {"bold_purple", "1;35"},
        {"bold_cyan", "1;36"},
        {"bold_white", "1;97"},
    };

    for (const StyleToken &style : style_tokens) {
        if (token == style.name) {
            return sgr(style.code);
        }
    }

    return std::nullopt;
}

std::optional<std::string> render_named_token(const shell::ShellState &state,
                                              const std::string &token) {
    if (const std::optional<std::string> style = render_style_token(token);
        style.has_value()) {
        return style;
    }

    if (token == "n") {
        return std::string("\n");
    }
    if (token == "arrow" || token == "prompt") {
        return std::string("╰─❯ ");
    }
    if (token == "powerline_right" || token == "pl_right") {
        return std::string("");
    }
    if (token == "powerline_left" || token == "pl_left") {
        return std::string("");
    }
    if (token == "powerline_thin_right" || token == "pl_right_thin") {
        return std::string("");
    }
    if (token == "powerline_thin_left" || token == "pl_left_thin") {
        return std::string("");
    }
    if (token == "user") {
        return get_display_user(state);
    }
    if (token == "host" || token == "hostname") {
        return get_hostname_text();
    }
    if (token == "dir") {
        return get_display_directory(state);
    }
    if (token == "cwd") {
        return get_current_working_directory();
    }
    if (token == "location") {
        return build_location_segment(state);
    }
    if (token == "status") {
        return build_status_segment(state);
    }
    if (token == "git") {
        return build_git_segment(state);
    }
    if (token == "bg") {
        return build_background_segment(state);
    }

    return std::nullopt;
}

} // namespace

std::optional<std::string> render_token(const shell::ShellState &state,
                                        const std::string &token) {
    return render_named_token(state, token);
}

} // namespace shell::prompt::prompt_segments
