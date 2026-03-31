#include "prompt.hpp"

#include <linux/limits.h>
#include <unistd.h>

const std::string purple = "\033[1;35m";
const std::string cyan = "\033[1;36m";
const std::string reset = "\033[0m";

std::string build_prompt_header() {
    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        std::string path = cwd;

        const char *home = getenv("HOME");
        const char *user = getenv("USER");

        if (home) {
            std::string h(home);
            if (path.compare(0, h.size(), h) == 0) {
                path = "~" + path.substr(h.size());
            }
        }

        return purple + "╭─ " + user + cyan + " → " + path + reset;
    }

    return purple + "╭─" + reset;
}

std::string build_prompt_prefix() {
    return purple + std::string("╰─❯ ") + reset;
}

std::string build_prompt() {
    return build_prompt_header() + "\n" + build_prompt_prefix();
}
