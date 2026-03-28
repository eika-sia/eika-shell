#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

struct Command {
    std::string raw;
    std::vector<std::string> args;
    bool background = false;

    std::string input_file = "";
    std::string output_file = "";
    bool append_output = false;
};

struct Pipeline {
    std::vector<Command> commands;
    bool background = false;

    bool valid = true;
};

Command parse_command(const std::string &line);
Pipeline parse_pipeline(const std::string &line);

std::vector<std::string> split_chained_commands(const std::string &line);
std::vector<std::string> split_pipeline_commands(const std::string &line);
