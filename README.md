# EikaShell

Small Unix-like shell written in C++.

## Documentation

- [Input stack](docs/input.md)
  Detailed notes on raw input, key decoding, editor state, undo/redo, completion panels, and reverse history search.
- [Prompt system](docs/prompt.md)
  Detailed notes on prompt templates, prompt tokens, right prompts, multiline prompt geometry, and redraw behavior.
- [Language design](docs/language.md)
  Design-roadmap notes for the future esh scripting language, parser direction, hooks, and native integration scripts.

## Build

### Requirements
- CMake to build the shell
- Nerd font of your choice to render git icon in header

Use the provided helper:

```sh
./make.sh      # debug
./make.sh -r   # release
```

Install system-wide:

```sh
sudo ./install.sh
```

By default this installs the binary as `/usr/local/bin/esh`. To install as
`/usr/bin/esh`, run:

```sh
sudo ./install.sh --system-bin
```

For a user-local binary install:

```sh
INSTALL_BIN="$HOME/.local/bin/esh" ./install.sh
```

## Run

Interactive shell:

```sh
./build/shell
```

Run one command:

```sh
./build/shell -c 'echo hello'
```

Run a script file:

```sh
./build/shell script.esh
```

Read commands from stdin:

```sh
printf 'echo hello\npwd\n' | ./build/shell
```

## Implemented Features

### Command execution

- external command execution
- pipelines with `|`
- `&&`, `||`, `;`, and background execution with `&`
- input redirection with `<`
- output redirection with `>` and `>>`
- foreground and background process execution

### Builtins

- `help`
- `cd`
- `pwd`
- `exit`
- `type`
- `source`
- `history`
- `ps`
- `kill`
- `alias`
- `unalias`
- `set`
- `export`
- `unset`

### Expansion and shell state

- history expansion with `!!`, `!N`, and `!-N`
- variable expansion with `$NAME` and `$?`
- tilde expansion
- alias expansion
- shell-local variables with `NAME=value`
- temporary per-command assignments with `NAME=value cmd`
- exported variables with `export NAME` and `export NAME=value`
- assignment-only commands such as `NAME=value` and `NAME=value > file`

### Interactive features

- custom raw-mode input handling
- prefix aware arrow-key history navigation
- reverse history search with `Ctrl+R`
- left/right, word, home, and end cursor movement
- backspace, delete, word delete, and line kill bindings
- kill ring yank and yank-pop
- undo with `Ctrl+Z` and redo with `Alt+Z`
- bracketed paste handling
- tab completion for commands and paths with selectable candidates
- syntax highlighting
- comment highlighting
- see [docs/input.md](docs/input.md) for the detailed input architecture

### Prompt features

- configurable `PROMPT` and `RPROMPT` templates
- prompt tokens for user, host, directory, status, git state, background jobs, and time
- ANSI color/style prompt tokens
- multiline prompts
- powerline style prompt segment helpers
- see [docs/prompt.md](docs/prompt.md) for prompt template and redraw details

### Startup and persistence

- `~/.config/esh/eshrc` is sourced on interactive startup if it exists
- `~/.eshrc` is used as a fallback only when `~/.config/esh/eshrc` does not exist
- `~/.local/share/esh/history` is loaded on interactive startup if it exists
- `~/.eshrc_history` is used as a fallback when `~/.local/share/esh/history` does not exist
- interactive history is saved back to `~/.local/share/esh/history` on exit
- missing startup/history files are ignored silently

### Scripting and comments

- `#` starts a comment outside quotes
- `source file` runs in the current shell
- `source ./file.esh` and `source /path/file.esh` load that exact path
- `source name.esh` searches `~/.config/esh/scripts`, then `~/.local/share/esh/scripts`
- `source`, `-c`, and script-file execution do not record inner commands into history
- see [docs/language.md](docs/language.md) for the planned scripting language direction

## Current Semantics

### Variables

- `NAME=value`
  sets a shell-local variable in the current shell
- `NAME=value cmd`
  applies the assignment only to that command
- `export NAME` / `export NAME=value`
  marks an existing variable for export, or creates an empty exported variable
- `unset NAME`
  removes the variable from shell state, and from the process environment if it was exported

### Directory state

- `cd -` switches to `OLDPWD`
- `OLDPWD` is tracked as a shell-local variable, not an exported environment variable

### Assignment-only commands

- foreground standalone assignment-only commands update the shell
- background or pipeline assignment-only commands run in a child and do not mutate the parent shell
- example:

```sh
FOO=bar
set | grep FOO        # sees FOO

FOO=bar env | grep FOO   # env sees FOO
FOO=bar | env | grep FOO # env does not see FOO
```

## Project Layout

- `main.cpp`
  program entry point and top-level mode selection
- `shell/`
  shell control flow, input, prompt rendering, terminal handling, signals, and exec helpers
- `parser/`
  tokenization and parsing into the shell AST
- `builtins/`
  builtin dispatch and builtin-specific modules
- `features/`
  history, highlighting, completion, expansion, and shared shell-text scanning
- `process/`
  tracked child process state and wait-status normalization

## Execution Flow

At a high level the shell does this:

1. Read a line or script line.
2. Expand history.
3. Parse into a command list.
4. Expand aliases on the parsed structure.
5. Expand variables and `~` per command.
6. Decide whether a command runs in the parent, in a child, or as an external command.
7. Apply redirections, launch pipelines, and wait for foreground work.

## Not Implemented

- full job control: `jobs`, `fg`, `bg`, stopped jobs, `SIGTSTP`/`SIGCONT` handling
- command substitution
- globbing
- subshells
- heredocs
- shell functions
- background conditional chains such as `a && b &`

## Notes

- Alias expansion stays textual and reparses the command line after each expansion step.
- External commands use `execvpe` with an explicitly constructed environment block.
- Parent-run builtins and assignment-only commands use dedicated parent execution helpers so redirections still work without forking.
- The shell is intentionally simpler than a full POSIX shell, especially around escape handling and job control.
