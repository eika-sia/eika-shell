#include "prompt.hpp"

#include "../../builtins/env/env.hpp"
#include "../shell.hpp"

#include <linux/limits.h>
#include <unistd.h>

namespace shell::prompt {
namespace {

const std::string purple = "\033[1;35m";
const std::string cyan = "\033[1;36m";
const std::string reset = "\033[0m";

std::string build_prompt_header(const shell::ShellState &state) {
    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        std::string path = cwd;

        const shell::ShellVariable *home =
            builtins::env::find_variable(state, "HOME");
        const std::string user = builtins::env::get_variable_value(state, "USER");
        const std::string display_user = user.empty() ? "shell" : user;

        if (home != nullptr && !home->value.empty()) {
            std::string h(home->value);
            if (path.compare(0, h.size(), h) == 0) {
                path = "~" + path.substr(h.size());
            }
        }

        return purple + "╭─ " + display_user + cyan + " → " + path + reset;
    }

    return purple + "╭─" + reset;
}

std::string build_prompt_prefix() {
    return purple + std::string("╰─❯ ") + reset;
}

} // namespace

std::string build_prompt(const shell::ShellState &state) {
    return build_prompt_header(state) + "\n" + build_prompt_prefix();
}

void redraw_input_line(const shell::ShellState &state, const std::string &line,
                       size_t cursor, bool full_prompt) {
    const std::string prefix =
        full_prompt ? build_prompt(state) : build_prompt_prefix();

    write(STDOUT_FILENO, "\r", 1);
    write(STDOUT_FILENO, "\033[2K", 4);
    write(STDOUT_FILENO, prefix.c_str(), prefix.size());
    write(STDOUT_FILENO, line.c_str(), line.size());

    const size_t right_edge = line.size();
    if (right_edge > cursor) {
        const std::string move_left =
            "\033[" + std::to_string(right_edge - cursor) + "D";
        write(STDOUT_FILENO, move_left.c_str(), move_left.size());
    }
}

} // namespace shell::prompt
