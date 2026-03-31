#pragma once

#include <cstddef>
#include <string>
#include <sys/types.h>
#include <vector>

namespace parser {

struct Command {
    std::string raw;
    std::vector<std::string> args;
    bool background = false;

    std::string input_file = "";
    std::string output_file = "";
    bool append_output = false;

    bool valid = true;
    size_t command_name_offset = std::string::npos;
    size_t command_name_length = 0;
};

struct Pipeline {
    std::vector<Command> commands;
    bool background = false;

    bool valid = true;
};

struct CommandList {
    std::vector<Pipeline> pipelines;
    bool valid = true;
};

Command parse_command(const std::string &line);
Pipeline parse_pipeline(const std::string &line);
CommandList parse_command_line(const std::string &line);

} // namespace parser
