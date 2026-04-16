# Input Stack

- `input.cpp` main orchestration and dispatching
- `key/` converts input bytes to semantic actions (like pasted text, escape sequences)
- `editor_state/` mutates the line buffer and history browse state
- `session_state/` handles per line stuff like a kill ring

## High-Level Pipeline

```text
terminal bytes
    |
    v
key::read_event()
    |
    +-- TextInput
    +-- Key
    +-- Paste
    +-- Resized
    +-- Interrupted
    +-- ReadEof
    v
input::read_command_line()
    |
    +-- editor_state: pure buffer/history mutations
    +-- session_state: kill ring, yank-pop, history invalidation
    +-- completion: compute completion action
    +-- prompt: redraw current input block
    v
InputResult { line, eof, interrupted }
```

## Entry Point

The public API is:

```cpp
InputResult read_command_line(shell::ShellState &state,
                              shell::prompt::InputRenderState &render_state);
```

`read_command_line()` has 3 modes:

1. Non-interactive mode
   Uses `std::getline()` directly and returns one line or EOF.

2. Interactive raw mode
   Enables raw terminal input in `input.cpp`, then reads one semantic event at a time through `key::read_event()`.

3. Interactive fallback mode
   If raw mode setup fails, it falls back to `std::getline()` but still watches the global signal flags for interrupt and resize behavior.

## `input.cpp`: Coordinator Layer

`input.cpp` should be read as the top level state machine for reading a single command line.

Key local types:
- `InputSession`
  Owns the saved `termios` state and restores it on scope exit.
- `InputContext`
  Bundles the current shell state, render state, editable buffer, per line session state, initial history size, and output `InputResult`.

The main loop does this:
1. `key::read_event()` reads one semantic event.
2. System events are handled first:
   - `ReadEof` -> finish with `eof = true`
   - `Interrupted` -> finalize prompt block and finish with
     `interrupted = true`
3. Content events are dispatched:
   - `TextInput` -> insert text into the line buffer
   - `Paste` -> normalize newlines/tabs into spaces, then insert
   - `Key` -> route through character bindings or special-key bindings
   - `Resized` -> redraw current buffer
   - `Ignored` -> do nothing
4. On finish, copy `buffer.text` into `InputResult.line`.

### Current Responsibilities in `input.cpp`

`input.cpp` is still responsible for:
- raw terminal mode lifetime
- one line event loop
- mapping key bindings to editor actions
- paste normalization for a single-line editor
- calling completion and choosing whether to redraw or print candidates
- deciding when input is finished

`input.cpp` should not own:
- byte level terminal escape parsing
- text buffer algorithms
- kill ring internals
- history browse internals

## `key/`: Terminal Byte Decoding

`key/` converts raw terminal bytes into `InputEvent`.

### `InputEvent`

The decoder emits one of these kinds:
- `TextInput`
- `Key`
- `Paste`
- `Resized`
- `Interrupted`
- `ReadEof`
- `Ignored`

For `Key` events it also carries:
- `EditorKey`
- modifier flags
- `key_character` for logical bindings like `Ctrl+K` or `Alt+F`

### Decoder Flow

`key::read_event()` does byte level decoding:
1. Read one byte from `STDIN_FILENO`.
2. Handle direct single-byte cases:
   - `\n`, `\r` -> `Enter`
   - `8`, `127` -> `Backspace`
   - `9` -> `Tab`
   - `1..26` -> control-byte decoding such as `Ctrl+A`
3. If byte is `ESC`, decode an escape sequence.
4. Otherwise emit a `TextInput` event containing that byte.

### Signals and `read_event()`

If a read is interrupted by `EINTR`, `key::read_event()` checks the signal
flags:
- `g_input_interrupted` -> `Interrupted`
- `g_resize_pending` -> `Resized`

### Escape Decoding

Escape decoding is split by introducer byte:
- `ESC [` -> `csi::decode(...)`
- `ESC O` -> `ss3::decode(...)`
- anything else -> treated as Alt-prefixed input when possible

This matters because there are 2 different jobs here:
- terminal protocol decoding belongs in `key/`
- shell-language escapes like `\n` or future `\x49` do not

### `character_key.cpp`

This file handles:

- control bytes `1..26` -> `Ctrl+A`..`Ctrl+Z`
- Alt-prefixed bytes after a leading `ESC`

That means `Alt+f` and `Ctrl+W` both enter the system as regular `Key` events with modifier flags, not as special cases in the main loop.

### `csi/`

`CSI` means `ESC [`. This module handles:
- arrows
- `Home` / `End`
- `Delete`
- modifier-aware arrow/home/end/delete forms
- bracketed paste start/end sequences

Current supported examples:
- `ESC [ A` -> Up
- `ESC [ 3 ~` -> Delete
- `ESC [ 1 ; 5 D` -> Ctrl+Left
- `ESC [ 200 ~ ... ESC [ 201 ~` -> bracketed paste payload

### `ss3/`

`SS3` means `ESC O`. This is the compatibility/application-mode family for cursor keys. It currently handles arrow keys plus `Home` and `End`.

## `editor_state/`: Pure Line Editing

`editor_state` is the text-buffer layer. It owns:
- `LineBuffer { text, cursor }`
- `HistoryBrowseState { active, index, draft }`
- cursor movement
- erase operations
- kill target selection
- history up/down navigation

It does not own:
- kill ring storage
- yank state
- prompt redraw
- signal handling
- completion policy

### Important API Shape

These functions are intentionally narrow:
- `apply_movement(...)`
- `insert_text(...)`
- `replace_range(...)`
- `apply_erase(...)`
- `apply_kill(...)`
- `apply_history_navigation(...)`
- `reset_history_browse(...)`

`apply_kill(...)` returns:
```cpp
struct KillResult {
    bool changed;
    std::string killed_text;
    KillDirection direction;
};
```

That is important because `editor_state` decides what text was removed, but `session_state` decides what that means for the kill ring.

### Word Semantics

Word movement and word-kill behavior now follow shell-token boundaries rather than identifier boundaries.

In practice that means:
- shell separators like space, tab, `|`, `;`, `&`, `<`, `>` break words
- paths like `foo/bar-baz.txt` stay one movement/kill unit
- flags like `--long-option=value` stay one movement/kill unit
- quoted or escaped separators are treated as part of the same shell word

## `session_state/`: Per-Line Interaction State

`session_state` owns everything that spans multiple edits during a single input session but is not part of the raw text buffer itself.

Current session state:
- history browse state
- kill ring entries
- kill coalescing state
- yank pop state

### Kill Ring Model

`KillRingState` stores:
- `entries`
- `max_entries`
- whether the last command was a kill
- last kill direction
- yank session state

Behavior:
- `Ctrl+K`, `Ctrl+U`, `Ctrl+W`, `Alt+D` record kills
- consecutive kills in the same direction coalesce
  - forward kills append
  - backward kills prepend
- `Ctrl+Y` yanks the latest kill without consuming it
- `Alt+Y` rotates through older ring entries, replacing the just-yanked text
- ordinary edits/movement/history/completion reset kill chaining and usually invalidate yank-pop

### History Coupling

When a real edit happens while history browsing is active, `session_state` resets history browse first. This is why insert/erase/replace helpers in `session_state` take `history_size`:
- the buffer layer stays pure
- the session layer decides when browsing is invalidated

## Completion in the Input Pipeline

Completion lives in `features/completion`, but the input stack decides how to apply the result.

`complete_at_cursor(...)` returns a pure `CompletionResult`:
- `None`
- `ReplaceToken`
- `ShowCandidates`

The input layer then chooses behavior:
- `None` -> no text change
- `ReplaceToken` -> replace a buffer range and redraw
- `ShowCandidates` -> print the candidates, then redraw the full prompt/input

## Prompt Coupling

The input stack depends on `shell::prompt::InputRenderState`, but the ownership is explicit.

`input.cpp` never reaches into prompt globals because there are none. It only does 3 things:
- initial prompt is built elsewhere
- redraw current line through `prompt::redraw_input_line(...)`
- finalize interrupted input through `prompt::finalize_interrupted_input_line(...)`

## Current Binding Map

The bindings are intentionally implemented in `input.cpp`, not in `key/`.

### Ctrl bindings

- `Ctrl+A` -> home
- `Ctrl+D` -> delete at cursor, or EOF when buffer is empty
- `Ctrl+E` -> end
- `Ctrl+K` -> kill to end of line
- `Ctrl+L` -> clear screen and redraw full prompt
- `Ctrl+U` -> kill to start of line
- `Ctrl+W` -> kill previous word
- `Ctrl+Y` -> yank latest kill

### Alt bindings

- `Alt+B` -> word-left
- `Alt+Backspace` -> kill previous word
- `Alt+F` -> word-right
- `Alt+D` -> kill next word
- `Alt+Y` -> yank-pop

### Special keys

- arrows -> movement / history
- `Ctrl+Left`, `Ctrl+Right` -> word movement
- `Ctrl+Backspace` -> kill previous word
- `Home`, `End`
- `Backspace`, `Delete`
- `Tab` -> completion
- `Enter` -> finish line

## Resize, Interrupt, and EOF Behavior

### Resize

- signal handler sets `g_resize_pending`
- `key::read_event()` converts that into `InputEventKind::Resized`
- `input.cpp` redraws the current buffer using the current render state

### Interrupt

- signal handler sets `g_input_interrupted`
- `key::read_event()` converts that into `Interrupted`
- `read_command_line()` finalizes the rendered input block and returns
  `InputResult{ interrupted = true }`

### EOF

- direct read EOF in interactive mode becomes `ReadEof`
- `Ctrl+D` on an empty buffer also becomes EOF at the binding layer

## Where To Put Future Changes

Use this rule set when adding features:

### Add it to `key/` if...
- the change is about terminal bytes or escape protocols
- examples: more CSI sequences, SS3 variants, mouse, bracketed paste toggles, focus events, new modifier encodings

### Add it to `editor_state/` if...
- the change is a pure line-buffer or history-browse operation
- examples: transpose chars, delete word range, smarter word boundaries, pure cursor movement

### Add it to `session_state/` if...
- the feature depends on previous editor commands during the same line read
- examples: kill-ring rotation, completion session state, search session state, undo groups

### Add it to `input.cpp` if...
- the change is binding policy or top-level orchestration
- examples: mapping `Ctrl+T` to transpose, deciding that repeated `Tab` cycles completion state, choosing what redraw happens after candidate output

## Design Rules

These are the constraints the current refactor is trying to preserve:
1. `key/` decodes terminal protocol, not shell syntax.
2. `editor_state/` mutates text, not editor session behavior.
3. `session_state/` owns kill/yank/history interaction rules.
4. `input.cpp` is allowed to decide bindings and redraw policy.
5. Prompt state must stay explicit.
6. Raw mode lifetime stays in `input.cpp` with the line-read loop.
