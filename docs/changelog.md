# Changelog

## Planned: esh v1.2.0 - Scripting Foundation

Status: planned future milestone.

Expected feature areas:
- script parser and AST execution
- functions with call frames
- local parameters
- `return`
- `if` / `else` / `end`
- command substitution
- `command` and `builtin`
- hooks, starting with `chdir`
- native scripting

## esh v1.1.2 - Installation, XDG Paths, and Language Roadmap

Anchor commits:
- `f8fc728` - `Documentation :3`
- `3a4a89a` - `add better source mechanics, xdg based config and data paths`

Highlights:
- Centralized docs under `docs/`.
- Added the language design roadmap in `docs/language.md`.
- Added XDG-style startup config:
  - primary config: `~/.config/esh/eshrc`
  - legacy fallback: `~/.eshrc`
- Added XDG-style history:
  - primary history: `~/.local/share/esh/history`
  - legacy load fallback: `~/.eshrc_history`
  - saves now go to the XDG history path.
- Added script lookup for bare `source` names:
  - `~/.config/esh/scripts`
  - `~/.local/share/esh/scripts`
- Kept direct source paths exact:
  - `source ./file.esh`
- Added `install.sh`:
  - builds the shell
  - installs as `esh`
  - creates config, script, and history directories/files.

## esh v1.1.1 - Panel Render Polish

Anchor commit:
- `635153e` - `panel: fix small render bug with frame rendering`

Highlights:
- Fixed a small panel render bug in `shell/input/panels/panel.cpp`.
- Kept the input-revamp architecture unchanged.

## esh v1.1.0 - Input Revamp / UX Update

Status: merged through `4b92156` - `Eikashell V1.1 - UX update`.

This version is the interactive UX revamp. It replaced the old ad hoc input path with a structured terminal input stack and significantly expanded prompt, completion, history, and editing behavior.

Input and editor highlights:
- Added semantic input events for text, keys, paste, resize, interrupt, EOF, and ignored input.
- Added RAII-style input session handling for terminal raw mode.
- Added safer fallback behavior when raw mode setup fails.
- Added structured key decoding:
  - control keys
  - Alt-prefixed keys
  - CSI / SS3 terminal escape sequences
  - bracketed paste wrappers
  - resize events
- Added bracketed paste support and paste normalization for the single-line editor.
- Added word movement and deletion.
- Added kill ring behavior:
  - kill line left/right
  - kill word left/right
  - yank
  - yank-pop
- Added undo/redo for line editing.
- Added prefix-aware history navigation.
- Added reverse history search with a below-input panel.
- Added transient input panels shared by completion and search.

Completion highlights:
- Reworked completion into action-oriented behavior.
- Added completion formatting cleanup.
- Added candidate panel rendering.
- Added selectable tab completion.
- Added completion redraw fixes for prompt/panel geometry.
- Improved path completion behavior, including command-position path handling.

Prompt highlights:
- Reworked prompt rendering around explicit layout data.
- Split prompt template parsing from prompt segment computation.
- Added configurable `PROMPT` and `RPROMPT`.
- Added right prompt support.
- Added multiline prompt geometry.
- Added prompt tokens for:
  - user
  - host
  - directory
  - git
  - status
  - background jobs
  - execution time
  - current time
- Added ANSI style/color tokens.
- Added powerline-style segment helpers.
- Improved prompt redraw handling with panels and terminal resize.

Architecture highlights:
- Added `shell/input/key/` for byte-to-event terminal decoding.
- Added `shell/input/editor_state/` for pure line buffer operations.
- Added `shell/input/session_state/` for per-line interaction state.
- Added `shell/input/panels/` for below-input panel rendering.
- Added `shell/prompt/prompt_utils/` for prompt templates and segments.
- Added `shell/prompt/render_utils.*` for ANSI-aware display geometry.
- Unified interactive terminal writes behind terminal output helpers.

## esh v1.0.1 - Pre-Revamp Polish

Approximate range:
- `d8c7014` - `big refactor for builtins, small refactors elsewhere :3`
- through `54d857e` - `Fix weird git icon in header (switched to nerd font)`

Highlights:
- Refactored builtin organization.
- Improved background process tracking.
- Changed history saving so it only wrote when the history file existed.
- Improved the prompt header and visual prompt presentation.
- Fixed external command display names in error output.
- Fixed path completion in command position, including preserving `./`.
- Switched the git icon in the prompt/header to a Nerd Font glyph.

## esh v1.0.0 - First usable shell with wide features

Command execution:
- External command execution through `execvpe`.
- Pipelines with `|`.
- Conditional execution with `&&` and `||`.
- Command sequencing with `;`.
- Background execution with `&`.
- Input redirection with `<`.
- Output redirection with `>` and `>>`.
- Parent-run builtins where parent state must mutate.
- Child-run builtins where pipeline/background context requires it.

Parser and expansion:
- Tokenization for words, quotes, redirects, pipes, conditionals, sequences, and background execution.
- Command lists, conditional chains, pipelines, and simple command parsing.
- Variable assignment words.
- Variable expansion with `$NAME` and `$?`.
- Tilde expansion.
- Comment handling with `#` outside quotes.
- History expansion with `!!`, `!N`, and `!-N`.

Shell state:
- Shell-local variables.
- Exported variables.
- Temporary per-command assignments.
- Assignment-only commands.
- `PWD` / `OLDPWD` directory state.
- Environment import from the parent process.

Builtins:
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

Interactive features:
- Initial custom prompt.
- Syntax highlighting.
- Basic command/path completion.
- Startup rc file support through `~/.eshrc`.
- History file support through `~/.eshrc_history`.
- Non-TTY input support.
- EOF handling.

Process handling:
- Foreground process waiting.
- Background process tracking.
- Basic process cleanup.
- Process table display through `ps`.
- Tracked-process kill support.

Why this version exists:
- It established the shell as more than a command launcher.
- It provided enough parser, builtin, variable, and process infrastructure to
  support the later input revamp and scripting work.

## esh v0.8 - Startup, History, and Highlighting

Approximate range:
- `9cd1596` - `syntax highlighting and prompt logic revamp`
- through `3ef6c81` - `rc file and history file`

Highlights:
- Added syntax highlighting.
- Reworked prompt logic.
- Improved path completion behavior.
- Added `type`.
- Added `pwd`.
- Improved escape character handling.
- Added EOF support.
- Added comments with `#`.
- Added startup rc file support through `~/.eshrc`.
- Added shell history file support through `~/.eshrc_history`.

## esh v0.7 - Parser Reconstruction and Variables

Approximate range:
- `020c437` - `big parse reconstruction and variable support`
- through `9faa485` - `bug fixes/cleaning up/alias chains`

Highlights:
- Split parsing into smaller internal modules:
  - tokenizer
  - simple command parsing
  - pipeline parsing
  - conditional-chain parsing
- Added assignment parsing helpers.
- Moved environment handling under `builtins/env`.
- Added shell variables.
- Added exported variables.
- Added temporary assignment handling for commands.
- Added variable expansion behavior.
- Improved alias chain handling and cleanup.

## esh v0.6 - Non-TTY and Alias Usability

Approximate range:
- `90cc8a7` - `string match/replace factored`
- through `479fe58` - `added non tty input !!`

Highlights:
- Factored string matching/replacement helpers.
- Added `unalias`.
- Added non-TTY input support.

Why this checkpoint matters:
- Non-TTY support made the shell more scriptable/testable even before the scripting language existed.
- Alias management became less one-way because aliases could be removed.

## esh v0.5 - Conditional Pipelines

Anchor commit:
- `920f31e` - `conditional pipelines!`

Highlights:
- Added `&&`.
- Added `||`.
- Integrated conditional execution with parsed pipelines.

## esh v0.4 - Builtin and Shell Refactors

Approximate range:
- `74d8116` - `builtin refactor`
- through `ce7106c` - `big refactor`

Highlights:
- Refactored builtin dispatch.
- Reworked builtin structure.
- Cleaned up shell internals after early feature growth.

## esh v0.3 - Parser and Expansion Redesign

Approximate range:
- `490a0cf` - `parsing redisgn`
- through `ce0da0d` - `fixing expansions (primarily ~)`

Highlights:
- Redesigned parser behavior.
- Improved alias/env interaction with parsed commands.
- Fixed tilde expansion.
- Improved expansion handling around shell words.

## esh v0.2 - Pipelines

Anchor commit:
- `d55fdf7` - `yippie pipes`

Highlights:
- Added `|` support.
- Added process piping between commands.
- Established the need for command structures that can contain multiple command stages.

## esh v0.1 - Initial Shell Baseline

Anchor commit:
- `12bb231` - `a lot of features because I needed to make this repo earlier`

Highlights:
- CMake build setup.
- Initial `make.sh`.
- Initial parser.
- Initial shell state.
- Initial builtins.
- Initial aliases.
- Initial completion.
- Initial history.
- Initial prompt.
- Initial process execution.
- Initial terminal/input handling.
