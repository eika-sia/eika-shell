#include "features/history/history.hpp"
#include "shell/input/input.hpp"
#include "shell/prompt/prompt.hpp"
#include "shell/shell.hpp"
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    shell::ShellState state;
    shell::init_shell(state);

    if (argc > 1) {
        const std::string first_arg = argv[1];
        shell::ExecuteOptions options{};
        options.save_history = false;

        if (first_arg == "-c") {
            if (argc != 3) {
                std::cerr << argv[0] << ": -c requires exactly one command\n";
                return 2;
            }

            shell::execute_command_line(state, argv[2], options);
            process::cleanup_finished_processes(state);
            features::save_shell_history(state);
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

        const int status = shell::execute_stream(state, script, options);
        features::save_shell_history(state);
        return status;
    }

    while (state.running) {
        process::cleanup_finished_processes(state);

        if (state.interactive) {
            std::cout << shell::prompt::build_prompt(state);
            std::cout.flush();
        }

        shell::input::InputResult input =
            shell::input::read_command_line(state);

        if (input.interrupted) {
            continue;
        }
        if (input.eof) {
            std::cout << "exit" << '\n';
            std::cout.flush();
            shell::execute_command_line(state, "exit");
            break;
        }

        shell::execute_command_line(state, input.line);
        process::cleanup_finished_processes(state);
    }

    features::save_shell_history(state);
    return state.last_status;
}
