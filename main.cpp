#include <fstream>
#include <iostream>
#include <string>

#include "shell/input/input.hpp"
#include "shell/prompt/prompt.hpp"
#include "shell/shell.hpp"

namespace {

void run_line(shell::ShellState &state, const std::string &line) {
    if (shell::trim(line).empty()) {
        return;
    }

    shell::execute_command_line(state, line);
    process::cleanup_finished_processes(state);
}

int run_stream(shell::ShellState &state, std::istream &stream) {
    std::string line;
    while (state.running && std::getline(stream, line)) {
        run_line(state, line);
    }

    return state.last_status;
}

} // namespace

int main(int argc, char **argv) {
    shell::ShellState state;
    shell::init_shell(state);

    if (argc > 1) {
        const std::string first_arg = argv[1];

        if (first_arg == "-c") {
            if (argc != 3) {
                std::cerr << argv[0] << ": -c requires exactly one command\n";
                return 2;
            }

            run_line(state, argv[2]);
            return state.last_status;
        }

        if (argc != 2) {
            std::cerr << argv[0]
                      << ": expected either no arguments, -c <command>, or a "
                         "script path\n";
            return 2;
        }

        std::ifstream script(argv[1]);
        if (!script.is_open()) {
            perror(argv[1]);
            return 1;
        }

        return run_stream(state, script);
    }

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
            std::cout << "exit" << '\n';
            std::cout.flush();
            shell::execute_command_line(state, "exit");
            break;
        }

        run_line(state, input.line);
    }

    return state.last_status;
}
