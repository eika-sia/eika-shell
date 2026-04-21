#include "features/history/history.hpp"
#include "shell/input/input.hpp"
#include "shell/prompt/prompt.hpp"
#include "shell/shell.hpp"
#include "shell/terminal/terminal.hpp"
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    shell::ShellState state;
    shell::prompt::InputRenderState prompt_render_state;
    shell::init_shell(state);

    if (argc > 1) {
        const std::string first_arg = argv[1];
        shell::ExecuteOptions options{};
        options.save_history = false;

        if (first_arg == "-c") {
            if (argc != 3) {
                std::cerr << argv[0] << ": -c requires exactly one command\n";
                shell::terminal::shutdown_terminal(state);
                return 2;
            }

            shell::execute_command_line(state, argv[2], options);
            process::cleanup_finished_processes(state);
            shell::terminal::shutdown_terminal(state);
            features::save_shell_history(state);
            return state.last_status;
        }

        if (argc != 2) {
            std::cerr << argv[0]
                      << ": expected either no arguments, -c <command>, or a "
                         "script path\n";
            shell::terminal::shutdown_terminal(state);
            return 2;
        }

        std::ifstream script(argv[1]);
        if (!script.is_open()) {
            perror(argv[1]);
            shell::terminal::shutdown_terminal(state);
            return 1;
        }

        const int status = shell::execute_stream(state, script, options);
        shell::terminal::shutdown_terminal(state);
        features::save_shell_history(state);
        return status;
    }

    bool prompt_already_rendered = false;
    while (state.running) {
        process::cleanup_finished_processes(state);

        if (state.interactive && !prompt_already_rendered) {
            shell::terminal::write_stdout(
                shell::prompt::build_prompt(state, prompt_render_state));
        }
        prompt_already_rendered = false;

        shell::input::InputResult input =
            shell::input::read_command_line(state, prompt_render_state);

        if (input.interrupted) {
            prompt_already_rendered = input.prompt_rendered;
            continue;
        }
        if (input.eof) {
            shell::terminal::write_stdout_line("exit");
            shell::execute_command_line(state, "exit");
            break;
        }

        shell::execute_command_line(state, input.line);
        process::cleanup_finished_processes(state);
    }

    shell::terminal::shutdown_terminal(state);
    features::save_shell_history(state);
    return state.last_status;
}
