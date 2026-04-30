# Input Stack

- `input.cpp` main orchestration and dispatching
- `panels/panel.cpp` generic cursor and frame helpers for panels rendered below the current prompt/input block
- `panels/completion/completion_panel.cpp` builds the completion candidate panel block
- `key/` converts input bytes to semantic actions (like pasted text, escape sequences)
- `editor_state/` mutates the line buffer and history browse state
- `session_state/` handles per-line interaction state such as the kill ring, active completion selection, and undo/redo history

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
    +-- session_state: kill ring, yank-pop, history invalidation, completion selection, undo/redo snapshots
    +-- completion: compute completion action
    +-- prompt: build redraw frames for the input block
    +-- panels: generic below-input panel frames
    +-- panels/completion: build candidate grid blocks for the panel renderer
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
  Bundles the current shell state, prompt render state, editable buffer, per line session state, active panel render state, initial history size, and output `InputResult`.

The main loop does this:
1. `key::read_event()` reads one semantic event.
2. System events are handled first:
   - `ReadEof` -> finish with `eof = true`
   - `Interrupted` -> replace the rendered prompt/panel block with a fresh prompt and finish with `interrupted = true`
3. Content events are dispatched:
   - `TextInput` -> insert text into the line buffer
   - `Paste` -> normalize newlines/tabs into spaces, then insert
   - `Key` -> route through character bindings or special-key bindings
   - `Resized` -> clear and redraw the prompt, then redraw the active panel if one exists
   - `Ignored` -> do nothing
4. On finish, copy `buffer.text` into `InputResult.line`.

### Current Responsibilities in `input.cpp`

`input.cpp` is still responsible for:
- raw terminal mode lifetime
- one line event loop
- mapping key bindings to editor actions
- paste normalization for a single-line editor
- calling completion and deciding whether to replace text or open a completion-selection session
- deciding whether undo/redo should act on history or cancel an active transient preview first
- deciding when prompt redraws should include panel show/update/dismiss work
- deciding when input is finished

`input.cpp` should not own:
- byte level terminal escape parsing
- text buffer algorithms
- kill ring internals
- history browse internals
- completion candidate layout or ANSI panel rendering

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
- `ESC [ Z` -> Shift+Tab
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
- `restore_buffer(...)`
- `replace_range_from_anchor(...)`
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

Word movement and word-kill behavior now use explicit editor-word boundaries instead of shell parsing rules.

In practice that means:
- word chars are ASCII letters, digits, and `_`
- everything else is treated as a separator run
- paths like `foo/bar-baz.txt` are traversed in smaller editor-friendly chunks
- flags and assignments stop at punctuation instead of behaving like one shell token

## `session_state/`: Per-Line Interaction State

`session_state` owns everything that spans multiple edits during a single input session but is not part of the raw text buffer itself.

Current session state:
- history browse state
- kill ring entries
- kill coalescing state
- yank pop state
- completion selection state
- undo stack
- redo stack
- current undo-group kind

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

### Undo / Redo Model

Undo state currently stores full line snapshots:
- buffer text
- cursor position

Undo/redo lives in `session_state` rather than `editor_state` because restoring a line also has to clear or reset other per-line interaction state:
- history browse mode
- completion selection preview state
- yank-pop validity
- current undo-group boundaries

Current grouping rules:
- consecutive typed text inserts are one undo step
- consecutive typed blank separators are one undo step
- one paste is one undo step
- consecutive backspaces are one undo step
- consecutive deletes are one undo step
- consecutive same-direction kills are one undo step
- each yank is one undo step
- each yank-pop is one undo step
- each history navigation step is one undo step
- direct range replacements are one undo step

Current boundary rules:
- movement closes the current undo group
- `Esc`, `Enter`, `Ctrl+L`, and opening completion close the current undo group
- undo/redo restore clears transient history/preview/yank state before restoring the target snapshot

Current completion interaction rules:
- preview cycling does not create undo history
- `Esc` cancels preview and restores the anchor buffer without creating an undo step
- accepting a preview creates one undo step back to the anchor buffer
- if completion selection is active, `Ctrl+Z` and `Alt+Z` cancel the transient preview first instead of immediately walking undo/redo history

Transient preview helpers:
- `active_transient_preview_kind(...)` reports which preview mode currently owns the input
- `cancel_active_preview(...)` restores or clears the active preview
- `accept_active_preview(...)` commits the active preview as one undoable edit when needed
- `clear_transient_previews(...)` is the shared reset point for restore paths

### History Coupling

When a real edit happens while history browsing is active, `session_state` resets history browse first. This is why insert/erase/replace helpers in `session_state` take `history_size`:
- the buffer layer stays pure
- the session layer decides when browsing is invalidated

### Completion Selection Model

`CompletionSelectionState` stores:
- whether a selection session is active
- whether a preview has been applied to the buffer yet
- the original anchor buffer text and cursor
- the raw replacement range that completions target
- the candidate list and selected index

Behavior:
- first ambiguous `Tab` starts a completion selection session and renders the panel
- the first cycling `Tab` applies the first candidate as a preview from the anchor buffer
- later `Tab`s rotate through candidates by rebuilding from the same anchor buffer
- `Shift+Tab` reverses that rotation when the terminal reports it as a modified `Tab` event
- `Esc` cancels and restores the anchor buffer
- `Enter` accepts the currently previewed text but stays in the input editor
- ordinary edits, paste, erase, movement, and history navigation accept the preview first, then continue with that command
- undo and redo are special: while completion selection is active they cancel the preview/panel first instead of touching snapshot history

## Completion in the Input Pipeline

Completion lives in `features/completion`, but the input stack decides how to apply the result.

`complete_at_cursor(...)` returns a pure `CompletionResult`:
- `None`
- `ReplaceToken`
- `ShowCandidates`

The input layer then chooses behavior:
- `None` -> no text change
- `ReplaceToken` -> replace a buffer range and redraw
- `ShowCandidates` -> begin a completion selection session and render the panel below the prompt

## Prompt Coupling

The input stack depends on `shell::prompt::InputRenderState`, but the ownership is explicit.

The prompt layer now exposes 2 useful levels:
- `prompt::redraw_input_line(...)` for ordinary prompt-only redraws
- `prompt::build_redraw_input_frame(...)` when `input.cpp` needs to batch a prompt redraw together with panel rendering into one terminal write

That split matters because the completion panel sits below the prompt block. The prompt layer owns prompt geometry, while `panels/completion/completion_panel.cpp` owns completion layout relative to the prompt.

`panels/panel.cpp` owns:
- moving from the live input cursor to the row below the rendered input
- restoring the live input cursor after a panel render
- clearing or dismissing arbitrary panel rows below the prompt
- clearing prompt-plus-panel blocks for resize and interrupt paths
- generic panel helpers such as text truncation, visible row caps, viewport positioning, line appending, and footer text

`panels/completion/completion_panel.cpp` owns:
- candidate label styling and column layout
- turning completion candidates into a generic below-prompt panel block
- completion-specific styling such as directory coloring and selected reverse-video cells

## Current Binding Map

The bindings are intentionally implemented in `input.cpp`, not in `key/`.

### Ctrl bindings

- `Ctrl+A` -> home
- `Ctrl+B` -> left
- `Ctrl+D` -> delete at cursor, or EOF when buffer is empty
- `Ctrl+E` -> end
- `Ctrl+F` -> right
- `Ctrl+K` -> kill to end of line
- `Ctrl+L` -> clear screen and redraw full prompt
- `Ctrl+N` -> history down
- `Ctrl+P` -> history up
- `Ctrl+U` -> kill to start of line
- `Ctrl+W` -> kill previous word
- `Ctrl+Y` -> yank latest kill
- `Ctrl+Z` -> undo

### Alt bindings

- `Alt+B` -> word-left
- `Alt+Backspace` -> kill previous word
- `Alt+F` -> word-right
- `Alt+D` -> kill next word
- `Alt+Y` -> yank-pop
- `Alt+Z` -> redo

### Special keys

- arrows -> movement / history
- `Ctrl+Left`, `Ctrl+Right` -> word movement
- `Ctrl+Backspace` -> kill previous word
- `Home`, `End`
- `Backspace`, `Delete`
- `Tab` -> completion
- `Shift+Tab` -> reverse completion cycling when completion selection is active
- `Enter` -> finish line

Current limitation:
- literal `Ctrl+Shift+Z` redo is not available yet because the current control-byte decoder can represent `Ctrl+Z`, but not Shift on that same path

## Resize, Interrupt, and EOF Behavior

### Resize

- signal handler sets `g_resize_pending`
- `key::read_event()` converts that into `InputEventKind::Resized`
- `input.cpp` clears the old prompt/panel block
- it rebuilds the prompt with current terminal width
- if a transient panel mode is still active, it also rebuilds the panel under the resized prompt

### Interrupt

- signal handler sets `g_input_interrupted`
- `key::read_event()` converts that into `Interrupted`
- `read_command_line()` clears the prompt/panel block, redraws a fresh prompt in the same frame, and returns
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

### Add it to `panels/completion/completion_panel.cpp` if...
- the change is about completion candidate layout or completion-specific panel styling
- examples: column packing, selection highlighting, prettier candidate labels, completion-specific coloring

### Add it to `panels/panel.cpp` if...
- the change is generic to any UI panel that lives below the prompt/input block
- examples: generic cursor restore rules, panel clearing, prompt-plus-panel redraw framing, shared truncation, panel row budgeting, viewport footer text

### Add it to `input.cpp` if...
- the change is binding policy or top-level orchestration
- examples: mapping `Ctrl+T` to transpose, deciding that repeated `Tab` cycles completion state, choosing when a key confirms or cancels completion preview

## Design Rules

These are the constraints the current refactor is trying to preserve:
1. `key/` decodes terminal protocol, not shell syntax.
2. `editor_state/` mutates text, not editor session behavior.
3. `session_state/` owns kill/yank/history interaction rules.
4. `panels/panel.cpp` owns generic below-prompt panel frames.
5. `panels/completion/completion_panel.cpp` owns candidate-panel layout and completion-specific panel styling.
6. `input.cpp` is allowed to decide bindings and redraw policy.
7. Prompt state must stay explicit.
8. Raw mode lifetime stays in `input.cpp` with the line-read loop.
