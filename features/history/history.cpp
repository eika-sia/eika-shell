#include "history.hpp"

#include <iostream>

namespace features {

bool expand_history(shell::ShellState &state, std::string &line) {
    if (line[0] == '!') {
        int num = 0;
        try {
            num = std::stoi(line.substr(1));
        } catch (const std::invalid_argument &e) {
            std::cerr << "history: invalid argument" << std::endl;
            return false;
        } catch (const std::out_of_range &e) {
            std::cerr << "history: number out of range" << std::endl;
            return false;
        }

        if (num < 1 || num > (int)state.history.size()) {
            std::cerr << "history: invalid history number " << num << std::endl;
            return false;
        }
        line = state.history[num - 1];
        std::cout << line << std::endl;
    }
    return true;
}

} // namespace features
