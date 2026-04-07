# EikaShell

This repository contains a small Unix-like shell written in C++17 (no particular reason for c++ version though)

The project supports:

- external command execution
- pipelines
- `&&`, `||` (only in foreground), `;`, and background execution with `&`
- input and output redirections
- shell builtins
- aliases
- shell variables, exported variables, and prefix assignments
- history expansion
- interactive line editing, tab completion, and syntax highlighting

## Build

Use the provided bash helper:
```sh
./make.sh # debug mode
./make.sh -r # release mode
```

## Run

Interactive shell:

```sh
./build/shell
```

Non-interactive input from stdin:

```sh
printf 'echo hello\npwd\n' | ./build/shell
```

## Implemented Features

### Parsing and Execution

- simple commands
- pipelines with `|`
- conditional execution with `&&` and `||`
- sequential execution with `;`
- background execution with `&`
- input redirection with `<`
- output redirection with `>` and `>>`

### Builtins

- `cd`
- `pwd`
- `exit`
- `history`
- `ps`
- `kill`
- `alias`
- `unalias`
- `set`
- `export`
- `unset`
- `type`
- `source`

### Expansion

- history expansion with `!!` and `!n`
- variable expansion with `$NAME` and `$?`
- tilde expansion
- alias expansion

### Environment Model

The shell keeps its own variable table in `ShellState`.

- `NAME=value` stores a shell variable
- `export NAME` exports variable based on already set value
- `export NAME=value` sets and exports in one step
- `unset NAME` removes a variable
- `NAME=value cmd` applies the assignment only to that command (sets to `envp` via `execvpe`, also able to preload different `PATH`)

Assignment execution semantics are handled separately from parsing in
`builtins/env/envexec`.

### Interactive Mode

- custom raw mode input handling
- arrow key history navigation
- left/right movement
- backspace and delete
- tab completion for commands and paths
- syntax highlighting

When stdin is not a TTY, the shell switches to non-interactive mode and reads plain lines from stdin without prompt rendering or raw input handling.

## Project Layout

- `main.cpp`
  Program entry point.
- `shell/`
  High-level shell control flow, terminal handling, input, prompt rendering, signal handling, and process launching.
- `parser/`
  Tokenization and parsing into the shell AST.
- `builtins/`
  Builtin dispatch plus builtin-specific modules such as alias and environment.
- `features/`
  Interactive and textual features such as completion, highlighting, history, and expansion.
- `process/`
  Tracking child processes and normalizing wait status.

## Execution Flow

At a high level the shell does the following:

1. Read a line.
2. Expand history.
3. Parse into a command list.
4. Expand aliases on the parsed structure.
5. Expand variables and `~` per command before execution.
6. Decide whether a command should run as a parent builtin, child builtin, or
   external command.
7. Launch pipelines, apply redirections, and wait for foreground jobs.

## Parser Structure

The parser currently has these layers:

- `parser/internals/tokenize.*`
  Converts raw input into shell tokens.
- `parser/internals/parse_simple_command.cpp`
  Parses words, assignments, and redirections into a single command.
- `parser/internals/parse_structure.cpp`
  Builds pipelines and conditional chains.
- `parser/parser.cpp`
  Public entry points for parsing a command, pipeline, or full command line.

## Current Limitations

The shell does not yet implement:

- full job control (`jobs`, `fg`, `bg`, stopped jobs)
- command substitution
- globbing
- subshells
- heredocs
- background conditional chains such as `a && b &`

The shell also uses a custom interactive input path, so some escape handling is still intentionally simpler than a full POSIX shell.

## Extra notes

- Alias expansion is textual and reparses the full command line after each expansion step.
- Variable/tilde expansion is reparsed per command.
- Parent-run builtins and assignment-only commands use dedicated helpers in `shell/exec/exec.cpp` so redirections still work in the shell process.
- External commands use `execvpe` with an explicitly constructed environment block.
