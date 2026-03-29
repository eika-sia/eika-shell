#include <iostream>
#include <string>

#include "shell/input/input.hpp"
#include "shell/prompt/prompt.hpp"
#include "shell/shell.hpp"

int main() {
    ShellState state;
    init_shell(state);

    while (state.running) {
        cleanup_finished_processes(state);

        std::cout << build_prompt();
        std::cout.flush();

        InputResult input = read_command_line(state.history);

        if (input.interrupted) {
            continue;
        }
        if (input.eof) {
            std::cout << '\n';
            break;
        }

        std::string line = trim(input.line);
        if (line.empty()) {
            continue;
        }
        state.history.push_back(line);
        execute_command_line(state, line);
    }

    return 0;
}
