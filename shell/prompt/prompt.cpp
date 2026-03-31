#include "prompt.hpp"

#include <cstdlib>
#include <linux/limits.h>
#include <unistd.h>

namespace shell::prompt {
namespace {

const std::string purple = "\033[1;35m";
const std::string cyan = "\033[1;36m";
const std::string reset = "\033[0m";

} // namespace

std::string build_prompt_header() {
    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        std::string path = cwd;

        const char *home = getenv("HOME");
        const char *user = getenv("USER");
        const char *display_user = (user && user[0] != '\0') ? user : "shell";

        if (home) {
            std::string h(home);
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

std::string build_prompt() {
    return build_prompt_header() + "\n" + build_prompt_prefix();
}

} // namespace shell::prompt
