#include "prompt_segments.hpp"

#include <cerrno>
#include <chrono>
#include <ctime>
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
    bool changes_background = false;
    PromptColor background = PromptColor::Default;
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

std::string get_current_time_text() {
    const std::time_t now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local_time{};
    if (localtime_r(&now, &local_time) == nullptr) {
        return "";
    }

    char buffer[6] = {};
    if (std::strftime(buffer, sizeof(buffer), "%H:%M", &local_time) == 0) {
        return "";
    }

    return buffer;
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

std::string build_exec_time_segment(const shell::ShellState &state) {
    return std::to_string(state.last_exec_seconds) + "s";
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

const char *foreground_sgr_code(PromptColor color) {
    switch (color) {
    case PromptColor::Default:
        return "39";
    case PromptColor::Black:
        return "30";
    case PromptColor::Orange:
        return "38;5;208";
    case PromptColor::Red:
        return "31";
    case PromptColor::Green:
        return "32";
    case PromptColor::Yellow:
        return "33";
    case PromptColor::Blue:
        return "38;5;24";
    case PromptColor::Magenta:
        return "35";
    case PromptColor::Cyan:
        return "36";
    case PromptColor::White:
        return "97";
    case PromptColor::BrightBlack:
        return "90";
    case PromptColor::BrightRed:
        return "91";
    case PromptColor::BrightGreen:
        return "92";
    case PromptColor::BrightYellow:
        return "93";
    case PromptColor::BrightBlue:
        return "94";
    case PromptColor::BrightMagenta:
        return "95";
    case PromptColor::BrightCyan:
        return "96";
    case PromptColor::BrightWhite:
        return "97";
    }

    return "39";
}

const char *background_sgr_code(PromptColor color) {
    switch (color) {
    case PromptColor::Default:
        return "49";
    case PromptColor::Black:
        return "40";
    case PromptColor::Orange:
        return "48;5;208";
    case PromptColor::Red:
        return "41";
    case PromptColor::Green:
        return "42";
    case PromptColor::Yellow:
        return "43";
    case PromptColor::Blue:
        return "48;5;24";
    case PromptColor::Magenta:
        return "45";
    case PromptColor::Cyan:
        return "46";
    case PromptColor::White:
        return "107";
    case PromptColor::BrightBlack:
        return "100";
    case PromptColor::BrightRed:
        return "101";
    case PromptColor::BrightGreen:
        return "102";
    case PromptColor::BrightYellow:
        return "103";
    case PromptColor::BrightBlue:
        return "104";
    case PromptColor::BrightMagenta:
        return "105";
    case PromptColor::BrightCyan:
        return "106";
    case PromptColor::BrightWhite:
        return "107";
    }

    return "49";
}

std::optional<RenderedToken> render_style_token(const std::string &token) {
    static const StyleToken style_tokens[] = {
        {"reset", "0", true, PromptColor::Default},
        {"fg_reset", "39"},
        {"bg_reset", "49", true, PromptColor::Default},
        {"bold", "1"},
        {"dim", "2"},
        {"underline", "4"},
        {"black", "30"},
        {"orange", "38;5;208"},
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
        {"bg_black", "40", true, PromptColor::Black},
        {"bg_orange", "48;5;208", true, PromptColor::Orange},
        {"bg_red", "41", true, PromptColor::Red},
        {"bg_green", "42", true, PromptColor::Green},
        {"bg_yellow", "43", true, PromptColor::Yellow},
        {"bg_blue", "48;5;24", true, PromptColor::Blue},
        {"bg_magenta", "45", true, PromptColor::Magenta},
        {"bg_purple", "45", true, PromptColor::Magenta},
        {"bg_cyan", "46", true, PromptColor::Cyan},
        {"bg_white", "107", true, PromptColor::White},
        {"bg_bright_black", "100", true, PromptColor::BrightBlack},
        {"bg_bright_red", "101", true, PromptColor::BrightRed},
        {"bg_bright_green", "102", true, PromptColor::BrightGreen},
        {"bg_bright_yellow", "103", true, PromptColor::BrightYellow},
        {"bg_bright_blue", "104", true, PromptColor::BrightBlue},
        {"bg_bright_magenta", "105", true, PromptColor::BrightMagenta},
        {"bg_bright_purple", "105", true, PromptColor::BrightMagenta},
        {"bg_bright_cyan", "106", true, PromptColor::BrightCyan},
        {"bg_bright_white", "107", true, PromptColor::BrightWhite},
        {"bold_black", "1;30"},
        {"bold_orange", "1;38;5;208"},
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
            return RenderedToken{
                sgr(style.code), PromptTokenKind::Style,
                PromptStyleEffect{style.changes_background, style.background}};
        }
    }

    return std::nullopt;
}

std::optional<RenderedToken> render_named_token(const shell::ShellState &state,
                                                const std::string &token) {
    if (const std::optional<RenderedToken> style = render_style_token(token);
        style.has_value()) {
        return style;
    }

    if (token == "n") {
        return RenderedToken{std::string("\n"), PromptTokenKind::Newline, {}};
    }
    if (token == "powerline_left_auto" || token == "pl_left_auto") {
        return RenderedToken{"", PromptTokenKind::AutoPowerlineLeft, {}};
    }
    if (token == "powerline_right_auto" || token == "pl_right_auto") {
        return RenderedToken{"", PromptTokenKind::AutoPowerlineRight, {}};
    }
    if (token == "arrow" || token == "prompt") {
        return RenderedToken{std::string("╰─❯ "), PromptTokenKind::Data, {}};
    }
    if (token == "powerline_right" || token == "pl_right") {
        return RenderedToken{std::string(""), PromptTokenKind::Data, {}};
    }
    if (token == "powerline_left" || token == "pl_left") {
        return RenderedToken{std::string(""), PromptTokenKind::Data, {}};
    }
    if (token == "powerline_thin_right" || token == "pl_right_thin") {
        return RenderedToken{std::string(""), PromptTokenKind::Data, {}};
    }
    if (token == "powerline_thin_left" || token == "pl_left_thin") {
        return RenderedToken{std::string(""), PromptTokenKind::Data, {}};
    }
    if (token == "user") {
        return RenderedToken{
            get_display_user(state), PromptTokenKind::Data, {}};
    }
    if (token == "host" || token == "hostname") {
        return RenderedToken{get_hostname_text(), PromptTokenKind::Data, {}};
    }
    if (token == "dir") {
        return RenderedToken{
            get_display_directory(state), PromptTokenKind::Data, {}};
    }
    if (token == "curr_time") {
        return RenderedToken{
            get_current_time_text(), PromptTokenKind::Data, {}};
    }
    if (token == "cwd") {
        return RenderedToken{
            get_current_working_directory(), PromptTokenKind::Data, {}};
    }
    if (token == "location") {
        return RenderedToken{
            build_location_segment(state), PromptTokenKind::Data, {}};
    }
    if (token == "status") {
        return RenderedToken{
            build_status_segment(state), PromptTokenKind::Data, {}};
    }
    if (token == "git") {
        return RenderedToken{
            build_git_segment(state), PromptTokenKind::Data, {}};
    }
    if (token == "bg") {
        return RenderedToken{
            build_background_segment(state), PromptTokenKind::Data, {}};
    }
    if (token == "exec_time") {
        return RenderedToken{
            build_exec_time_segment(state), PromptTokenKind::Data, {}};
    }

    return std::nullopt;
}

} // namespace

std::optional<RenderedToken> render_token(const shell::ShellState &state,
                                          const std::string &token) {
    return render_named_token(state, token);
}

std::string render_powerline_left_transition(PromptColor from_background,
                                             PromptColor to_background) {
    return sgr(foreground_sgr_code(to_background)) +
           sgr(background_sgr_code(from_background)) + "";
}

std::string render_powerline_right_transition(PromptColor from_background,
                                              PromptColor to_background) {
    return sgr(foreground_sgr_code(from_background)) +
           sgr(background_sgr_code(to_background)) + "";
}

std::string final_reset_sequence() { return sgr("0") + sgr("49"); }

} // namespace shell::prompt::prompt_segments
