#include "parser.hpp"

#include <iostream>
#include <string>
#include <vector>

#include "../shell/shell.hpp"

std::vector<std::string> tokenize(const std::string &line) {
    std::vector<std::string> tokens;
    std::string current;

    bool in_double_quote = false;
    bool escape = false;

    for (char c : line) {
        if (escape) {
            current.push_back(c);
            escape = false;
            continue;
        }

        if (c == '\\') {
            escape = true;
            continue;
        }

        if (c == '"') {
            in_double_quote = !in_double_quote;
            continue; // do NOT include quotes
        }

        if ((c == ' ' || c == '\t') && !in_double_quote) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        current.push_back(c);
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

Command parse_command(const std::string &line) {
    Command cmd = Command{};
    cmd.raw = line;

    std::vector<std::string> parts = tokenize(cmd.raw);

    if (parts.empty()) {
        cmd.args = {};
        return cmd;
    }

    if (parts[parts.size() - 1] == "&") {
        cmd.background = true;
        parts.pop_back();
    }

    std::vector<std::string> cleaned_args;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] == ">" || parts[i] == ">>") {
            if (i + 1 >= parts.size()) {
                std::cerr << "syntax error: expected filename after "
                          << parts[i] << "\n";
                return cmd;
            }

            if (!cmd.output_file.empty()) {
                std::cerr << "syntax error: multiple output redirections\n";
                return cmd;
            }

            cmd.output_file = parts[i + 1];
            cmd.append_output = (parts[i] == ">>");
            i++;
        } else if (parts[i] == "<") {
            if (i + 1 >= parts.size()) {
                std::cerr << "syntax error: expected filename after <\n";
                return cmd;
            }

            if (!cmd.input_file.empty()) {
                std::cerr << "syntax error: multiple input redirections\n";
                return cmd;
            }

            cmd.input_file = parts[i + 1];
            i++;
        } else {
            cleaned_args.push_back(parts[i]);
        }
    }

    cmd.args = cleaned_args;

    return cmd;
}

Pipeline parse_pipeline(const std::string &line) {
    Pipeline pipe{};
    std::string work = trim(line);

    if (work.empty()) {
        pipe.valid = false;
        return pipe;
    }

    if (!work.empty() && work.back() == '&') {
        pipe.background = true;
        work.pop_back();
        work = trim(work);
    }

    std::vector<std::string> parts = split_pipeline_commands(work);

    for (const std::string &part : parts) {
        std::string t = trim(part);
        if (t.empty()) {
            std::cerr << "syntax error: empty pipeline stage\n";
            pipe.valid = false;
            return pipe;
        }

        Command cmd = parse_command(t);
        cmd.background = pipe.background;
        pipe.commands.push_back(cmd);
    }

    return pipe;
}

std::vector<std::string> split_line_by(const std::string &line, char split) {
    std::vector<std::string> vec;
    size_t s = 0;

    bool in_double_quote = false;
    bool next_escaped = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (c == split && !in_double_quote && !next_escaped) {
            vec.push_back(line.substr(s, i - s));
            s = i + 1;
        } else if (c == '"' && !next_escaped) {
            in_double_quote = !in_double_quote;
        }

        if (c == '\\' && !next_escaped) {
            next_escaped = true;
        } else {
            next_escaped = false;
        }
    }

    vec.push_back(line.substr(s));
    return vec;
}

std::vector<std::string> split_chained_commands(const std::string &line) {
    return split_line_by(line, ';');
}

std::vector<std::string> split_pipeline_commands(const std::string &line) {
    return split_line_by(line, '|');
}
