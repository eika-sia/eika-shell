#include <iostream>
#include <string>

#include "shell/input/input.hpp"
#include "shell/prompt/prompt.hpp"
#include "shell/shell.hpp"

int main() {
    shell::ShellState state;
    shell::init_shell(state);

    while (state.running) {
        process::cleanup_finished_processes(state);

        if (state.interactive) {
            std::cout << shell::prompt::build_prompt(state);
            std::cout.flush();
        }

        shell::input::InputResult input = shell::input::read_command_line(state);

        if (input.interrupted) {
            continue;
        }
        if (input.eof) {
            if (state.interactive) {
                std::cout << '\n';
            }
            break;
        }

        std::string line = shell::trim(input.line);
        if (line.empty()) {
            continue;
        }
        shell::execute_command_line(state, line);

        process::cleanup_finished_processes(state);
    }

    return 0;
}
