#include "parser.hpp"

#include <iostream>
#include <string>
#include <vector>

#include "../shell/shell.hpp"

enum class TokenKind {
    Word,
    InputRedirect,
    OutputRedirect,
    AppendRedirect,
    Pipe,
    Sequence,
    Background,
};

struct Token {
    TokenKind kind;
    std::string text;
    size_t raw_start = std::string::npos;
    size_t raw_end = std::string::npos;
};

bool is_redirect(TokenKind kind) {
    return kind == TokenKind::InputRedirect ||
           kind == TokenKind::OutputRedirect ||
           kind == TokenKind::AppendRedirect;
}

void flush_word(std::vector<Token> &tokens, std::string &current,
                size_t &current_start, size_t &current_end) {
    if (current_start == std::string::npos) {
        return;
    }

    tokens.push_back(
        Token{TokenKind::Word, current, current_start, current_end});
    current.clear();
    current_start = std::string::npos;
    current_end = std::string::npos;
}

bool tokenize_line(const std::string &line, std::vector<Token> &tokens) {
    std::string current;
    size_t current_start = std::string::npos;
    size_t current_end = std::string::npos;

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escape = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (escape) {
            current.push_back(c);
            current_end = i + 1;
            escape = false;
            continue;
        }

        if (c == '\\' && !in_single_quote) {
            if (current_start == std::string::npos) {
                current_start = i;
            }
            current_end = i + 1;
            escape = true;
            continue;
        }

        if (c == '\'' && !in_double_quote) {
            if (current_start == std::string::npos) {
                current_start = i;
            }
            current_end = i + 1;
            in_single_quote = !in_single_quote;
            continue;
        }

        if (c == '"' && !in_single_quote) {
            if (current_start == std::string::npos) {
                current_start = i;
            }
            current_end = i + 1;
            in_double_quote = !in_double_quote;
            continue;
        }

        if (!in_single_quote && !in_double_quote && (c == ' ' || c == '\t')) {
            flush_word(tokens, current, current_start, current_end);
            continue;
        }

        if (!in_single_quote && !in_double_quote &&
            (c == '<' || c == '>' || c == '|' || c == '&' || c == ';')) {
            flush_word(tokens, current, current_start, current_end);

            if (c == '<') {
                tokens.push_back(
                    Token{TokenKind::InputRedirect, "<", i, i + 1});
                continue;
            }

            if (c == '|') {
                tokens.push_back(Token{TokenKind::Pipe, "|", i, i + 1});
                continue;
            }

            if (c == ';') {
                tokens.push_back(Token{TokenKind::Sequence, ";", i, i + 1});
                continue;
            }

            if (c == '&') {
                tokens.push_back(Token{TokenKind::Background, "&", i, i + 1});
                continue;
            }

            if (i + 1 < line.size() && line[i + 1] == '>') {
                tokens.push_back(
                    Token{TokenKind::AppendRedirect, ">>", i, i + 2});
                ++i;
            } else {
                tokens.push_back(
                    Token{TokenKind::OutputRedirect, ">", i, i + 1});
            }
            continue;
        }

        if (current_start == std::string::npos) {
            current_start = i;
        }

        current.push_back(c);
        current_end = i + 1;
    }

    if (escape) {
        current.push_back('\\');
        current_end = line.size();
    }

    if (in_single_quote || in_double_quote) {
        std::cerr << "syntax error: unmatched quote\n";
        return false;
    }

    flush_word(tokens, current, current_start, current_end);
    return true;
}

bool parse_simple_command(const std::vector<Token> &tokens,
                          const std::string &source, Command &cmd) {
    cmd = Command{};
    cmd.valid = false;

    if (tokens.empty()) {
        std::cerr << "syntax error: missing command\n";
        return false;
    }

    const size_t raw_start = tokens.front().raw_start;
    const size_t raw_end = tokens.back().raw_end;
    cmd.raw = trim(source.substr(raw_start, raw_end - raw_start));

    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token &token = tokens[i];

        if (token.kind == TokenKind::Word) {
            cmd.args.push_back(token.text);
            if (cmd.command_name_offset == std::string::npos) {
                cmd.command_name_offset = token.raw_start - raw_start;
                cmd.command_name_length = token.raw_end - token.raw_start;
            }
            continue;
        }

        if (is_redirect(token.kind)) {
            if (i + 1 >= tokens.size() ||
                tokens[i + 1].kind != TokenKind::Word) {
                std::cerr << "syntax error: expected filename after "
                          << token.text << "\n";
                return false;
            }

            const std::string &filename = tokens[i + 1].text;

            if (token.kind == TokenKind::InputRedirect) {
                if (!cmd.input_file.empty()) {
                    std::cerr << "syntax error: multiple input redirections\n";
                    return false;
                }
                cmd.input_file = filename;
            } else {
                if (!cmd.output_file.empty()) {
                    std::cerr << "syntax error: multiple output redirections\n";
                    return false;
                }
                cmd.output_file = filename;
                cmd.append_output = (token.kind == TokenKind::AppendRedirect);
            }

            ++i;
            continue;
        }

        std::cerr << "syntax error: unexpected token " << token.text << "\n";
        return false;
    }

    if (cmd.args.empty() || cmd.args[0].empty()) {
        std::cerr << "syntax error: missing command\n";
        return false;
    }

    cmd.valid = true;
    return true;
}

bool parse_pipeline_tokens(const std::vector<Token> &tokens,
                           const std::string &source, Pipeline &pipe) {
    pipe = Pipeline{};
    pipe.valid = false;

    if (tokens.empty()) {
        std::cerr << "syntax error: missing command\n";
        return false;
    }

    std::vector<Token> current_command_tokens;
    for (const Token &token : tokens) {
        if (token.kind != TokenKind::Pipe) {
            current_command_tokens.push_back(token);
            continue;
        }

        if (current_command_tokens.empty()) {
            std::cerr << "syntax error: empty pipeline stage\n";
            return false;
        }

        Command cmd{};
        if (!parse_simple_command(current_command_tokens, source, cmd)) {
            return false;
        }

        pipe.commands.push_back(cmd);
        current_command_tokens.clear();
    }

    if (current_command_tokens.empty()) {
        std::cerr << "syntax error: empty pipeline stage\n";
        return false;
    }

    Command cmd{};
    if (!parse_simple_command(current_command_tokens, source, cmd)) {
        return false;
    }

    pipe.commands.push_back(cmd);
    pipe.valid = true;
    return true;
}

Command parse_command(const std::string &line) {
    Command cmd{};
    cmd.valid = false;

    std::string work = trim(line);
    if (work.empty()) {
        return cmd;
    }

    std::vector<Token> tokens;
    if (!tokenize_line(work, tokens)) {
        return cmd;
    }

    if (tokens.empty()) {
        return cmd;
    }

    for (const Token &token : tokens) {
        if (token.kind == TokenKind::Pipe ||
            token.kind == TokenKind::Sequence ||
            token.kind == TokenKind::Background) {
            std::cerr << "syntax error: unexpected token " << token.text
                      << "\n";
            return cmd;
        }
    }

    parse_simple_command(tokens, work, cmd);
    return cmd;
}

Pipeline parse_pipeline(const std::string &line) {
    Pipeline pipe{};
    pipe.valid = false;

    std::string work = trim(line);
    if (work.empty()) {
        return pipe;
    }

    std::vector<Token> tokens;
    if (!tokenize_line(work, tokens)) {
        return pipe;
    }

    if (tokens.empty()) {
        return pipe;
    }

    if (tokens.back().kind == TokenKind::Background) {
        pipe.background = true;
        tokens.pop_back();
    }

    if (tokens.empty()) {
        std::cerr << "syntax error: missing command\n";
        return pipe;
    }

    for (const Token &token : tokens) {
        if (token.kind == TokenKind::Sequence ||
            token.kind == TokenKind::Background) {
            std::cerr << "syntax error: unexpected token " << token.text
                      << "\n";
            return pipe;
        }
    }

    if (!parse_pipeline_tokens(tokens, work, pipe)) {
        return pipe;
    }

    for (Command &cmd : pipe.commands) {
        cmd.background = pipe.background;
    }

    return pipe;
}

CommandList parse_command_line(const std::string &line) {
    CommandList list{};
    list.valid = false;

    std::string work = trim(line);
    if (work.empty()) {
        return list;
    }

    std::vector<Token> tokens;
    if (!tokenize_line(work, tokens)) {
        return list;
    }

    if (tokens.empty()) {
        return list;
    }

    std::vector<Token> current_pipeline_tokens;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token &token = tokens[i];

        if (token.kind != TokenKind::Sequence &&
            token.kind != TokenKind::Background) {
            current_pipeline_tokens.push_back(token);
            continue;
        }

        if (current_pipeline_tokens.empty()) {
            if (token.kind == TokenKind::Sequence && i == tokens.size() - 1) {
                list.valid = true;
                return list;
            }

            std::cerr << "syntax error: missing command before " << token.text
                      << "\n";
            return list;
        }

        Pipeline pipe{};
        if (!parse_pipeline_tokens(current_pipeline_tokens, work, pipe)) {
            return list;
        }

        pipe.background = (token.kind == TokenKind::Background);
        for (Command &cmd : pipe.commands) {
            cmd.background = pipe.background;
        }

        list.pipelines.push_back(pipe);
        current_pipeline_tokens.clear();
    }

    if (current_pipeline_tokens.empty()) {
        if (!tokens.empty() && tokens.back().kind == TokenKind::Sequence) {
            list.valid = true;
            return list;
        }

        if (!tokens.empty() && tokens.back().kind == TokenKind::Background) {
            list.valid = true;
            return list;
        }

        std::cerr << "syntax error: missing command\n";
        return list;
    }

    Pipeline pipe{};
    if (!parse_pipeline_tokens(current_pipeline_tokens, work, pipe)) {
        return list;
    }

    list.pipelines.push_back(pipe);
    list.valid = true;
    return list;
}
