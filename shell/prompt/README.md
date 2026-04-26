# Prompt

The prompt stack is responsible for two separate jobs:

- building the rendered prompt text from the shell state and the `PROMPT` template
- integrating that prompt layout with the interactive input redraw geometry

The code is split so prompt syntax and prompt data live outside the redraw loop:

```text
prompt.cpp
  +-- prompt_utils/prompt_template.*
  |     Parses the `PROMPT` template and expands `%tokens`
  |
  +-- prompt_utils/prompt_segments.*
  |     Computes the values for prompt tokens like `%git`, `%dir`, `%status`
  |
  +-- render_utils.*
        Measures display width and computes cursor / block geometry
```

## Files

- `prompt.cpp`
  Owns prompt redraw integration. It does not know how `%git` works or how templates are parsed. It only consumes a `PromptLayout` and uses that to redraw the prompt and editable input line.

- `prompt.hpp`
  Defines the public prompt types:
  - `PromptLayout`
  - `InputRenderState`
  - `InputFrame`

- `prompt_utils/prompt_template.*`
  Owns `PROMPT` template parsing and rendering.

- `prompt_utils/prompt_segments.*`
  Owns token value lookup and style token expansion.

- `render_utils.*`
  Owns ANSI-aware display-width measurement and prompt block geometry.

## Core Types

### `PromptLayout`

`PromptLayout` is the bridge between template rendering and redraw geometry.

It stores:
- `header_rendered`
  Everything above the editable prompt line.
- `header_display_width`
  Display width of the widest header line.
- `input_prefix_rendered`
  The actual editable prompt prefix shown on the last prompt line.
- `prompt_prefix_display_width`
  Display width of that editable prompt prefix.

This split matters because the line editor only edits the last line. Completion menus and redraw clear logic need to know how many rows are above the editable line and how wide the prompt prefix is.

### `InputRenderState`

`InputRenderState` stores:

- the current `PromptLayout`
- the current input line display width
- the current cursor display width
- the last terminal column count
- whether the next redraw must rebuild the full prompt block

`prompt.cpp` updates this state after every redraw so later clears and cursor restores can be computed without re-reading the screen.

## Prompt Flow

The interactive prompt path is:

1. `main.cpp` calls `prompt::build_prompt(...)` for the initial prompt render.
2. `prompt_template::build_layout(...)` reads `PROMPT` and expands `%tokens`.
3. `prompt.cpp` stores that `PromptLayout` inside `InputRenderState`.
4. During editing, `input.cpp` asks `prompt.cpp` for redraw frames.
5. `prompt.cpp` reuses or rebuilds the `PromptLayout`, renders the highlighted input line, and computes cursor geometry with `render_utils`.

The prompt layer does not write directly to the terminal except through its public redraw helpers. `input.cpp` decides when full redraws happen.

## `PROMPT`

The prompt is configured through one shell variable:
- `PROMPT`

If `PROMPT` is:
- unset: the shell uses the built-in default template
- set to an empty string: the shell also uses the built-in default template
- set to a non-empty string: that string is parsed as the prompt template

The built-in default template is a multiline template that renders:
- one header line
- one editable prompt prefix line

## Multiline Rule

`PROMPT` is one string, but the renderer splits it into header vs editable prefix using the last newline:
- everything before the last newline becomes `header_rendered`
- everything after the last newline becomes `input_prefix_rendered`
- if there is no newline, the whole prompt becomes the editable prefix and there is no header

That means a 3-line prompt is just a prompt template with 2 newline insertions:

```sh
PROMPT='%user%{n}second line%{n}-> '
```

This renders as:
```text
<line 1 header>
<line 2 header>
<line 3 editable prompt prefix>
```

## Token Syntax

The template parser supports 3 forms:
- `%%`
  Literal `%`
- `%name`
  Bare token syntax
- `%{name}`
  Braced token syntax

Braced tokens exist for the adjacency problem. Bare tokens consume letters, digits, and underscores after `%`, so `%nheader` is read as the token `nheader`, not `%n` followed by `header`. If you want text immediately after a token and that text starts with a letter, digit, or underscore, use braces:
```text
%{n}header
%{user}_suffix
%{red}warning
```

Unknown tokens are left literal in the rendered prompt:
- `%wat` stays `%wat`
- `%{wat}` stays `%{wat}`

## Whitespace Behavior

Prompt tokens like `%status`, `%git`, and `%bg` can expand to an empty string.

The template renderer tries to avoid ugly doubled spaces when an empty token sits between visible segments. The rule is that if an empty token is surrounded by blank separators, the renderer collapses that gap to a single visible blank. This also works when style tokens such as `%{green}` or `%{reset}` sit around the empty token. Style tokens are zero-width and do not break blank-gap collapse.

## Data Tokens

These tokens produce prompt content, not ANSI styling:
- `user`
  Display user name. Falls back to `shell` if `$USER` is empty.
- `host`
- `hostname`
- `dir`
  Display current working directory. Replaces the home prefix with `~` when possible.
- `cwd`
  Raw current working directory without `~` shortening.
- `location`
  `user → dir`
- `status`
  Last non-zero shell status, rendered as text like `✘ 1` or `✘ SIGINT`. Empty when status is `0`.
- `git`
  Current git segment text. Empty outside a repository.
- `bg`
  Background job counter text like `| bg: 2`. Empty when there are no running background jobs.
- `arrow`
- `prompt`
  The prompt arrow text: `╰─❯ `
- `powerline_right`
- `pl_right`
  Powerline right separator: ``
- `powerline_left`
- `pl_left`
  Powerline left separator: ``
- `powerline_thin_right`
- `pl_right_thin`
  Thin right separator: ``
- `powerline_thin_left`
- `pl_left_thin`
  Thin left separator: ``
- `n`
  Newline

## Style Tokens

These tokens expand to ANSI SGR sequences. They do not produce visible prompt text by themselves.
General style tokens:
- `reset`
- `fg_reset`
- `bg_reset`
- `bold`
- `dim`
- `underline`

Foreground colors:
- `black`
- `red`
- `green`
- `yellow`
- `blue`
  Uses a darker 256-color blue so foreground and background blue match better for powerline-style prompt segments.
- `magenta`
- `purple`
- `cyan`
- `white`
  Uses bright white rather than ANSI `37`, which tends to look gray on many terminals.

Bright foreground colors:
- `bright_black`
- `bright_red`
- `bright_green`
- `bright_yellow`
- `bright_blue`
- `bright_magenta`
- `bright_purple`
- `bright_cyan`
- `bright_white`

Bold foreground colors:
- `bold_black`
- `bold_red`
- `bold_green`
- `bold_yellow`
- `bold_blue`
- `bold_magenta`
- `bold_purple`
- `bold_cyan`
- `bold_white`

Background colors:
- `bg_black`
- `bg_red`
- `bg_green`
- `bg_yellow`
- `bg_blue`
  Uses a darker 256-color blue so filled prompt segments are less bright.
- `bg_magenta`
- `bg_purple`
- `bg_cyan`
- `bg_white`
  Uses bright white background rather than ANSI `47`.

Bright background colors:
- `bg_bright_black`
- `bg_bright_red`
- `bg_bright_green`
- `bg_bright_yellow`
- `bg_bright_blue`
- `bg_bright_magenta`
- `bg_bright_purple`
- `bg_bright_cyan`
- `bg_bright_white`

## Color Model

Prompt data tokens like `%git`, `%status`, `%dir`, and `%user` are intentionally raw text now. They do not hardcode their own ANSI colors. That means prompt styling is controlled by the template:

```sh
PROMPT='%{bold_magenta}╭─%{reset} %{bold_magenta}%user%{bold_cyan} → %dir%{reset}%{n}%{bold_magenta}%arrow%{reset}'
```

Note that if `PROMPT` already contains real ANSI escape bytes, those bytes still pass through unchanged. The prompt template system does not strip them.

## Examples

### Single-line Prompt

```sh
PROMPT='%{bold_green}%user%{reset} -> '
```

### Two Header Lines Plus Input Prefix

```sh
PROMPT='%user on %dir%{n}%git%{n}-> '
```

### Prompt With Explicit Colors

```sh
PROMPT='%{bold_magenta}%user%{reset} %{cyan}@%{reset} %host%{n}%{green}%dir%{reset}%{n}%{bold_magenta}-> %{reset}'
```

### Powerline-Style Segment

```sh
PROMPT='%{bg_blue}%{white} %user %{blue}%{bg_green}%{pl_right}%{white} %dir %{green}%{bg_reset}%{pl_right}%{reset} '
```

The important trick is:

- the separator glyph itself is a visible character
- its foreground color should match the previous segment background
- its background color should match the next segment background
- for the final separator, `%{bg_reset}` clears the filled background so the arrow points into the terminal default background

That is how you get the p10k-style “filled arrow” transition between blocks.

### Literal Percent Sign

```sh
PROMPT='%% ready %{n}%arrow'
```

## Template Parser Notes

The parser is intentionally small:
- it does not run shell command substitution
- it does not recursively expand prompt tokens
- it does not interpret shell-style `\033` or `\e` escapes inside `PROMPT`
- it only understands prompt `%tokens` and literal text

If you need a literal newline in a template, use `%{n}`. Do not rely on embedded literal newlines in assignment syntax.

## Geometry Rules

Prompt layout is not just string rendering. The redraw system also needs:
- header row count
- prompt prefix display width
- input line display width
- cursor display width

That is why `prompt.cpp` keeps owning redraw integration even though prompt construction moved into `prompt_utils`.

## Extending The Prompt

### Add a new data token if...

- it depends on shell state or external shell context
- examples: `%jobs`, `%venv`, `%time`, `%branch`

Implementation home:

- `prompt_utils/prompt_segments.cpp`

### Add a new syntax feature if...

- it changes how templates are parsed or expanded
- examples: `%{token}` support, conditionals, alignment markers

Implementation home:

- `prompt_utils/prompt_template.cpp`

### Change redraw behavior if...

- it affects screen clearing, prompt rows, cursor restore, or terminal geometry

Implementation home:

- `prompt.cpp`
- `render_utils.cpp`

## Current Limitations

- Prompt tokens are simple text expansions. There are no conditionals yet.
- Tokens that probe git still do subprocess work on each full prompt rebuild.
- Literal shell escape syntaxes like `\033` are not interpreted by the prompt template engine.
- Right prompt is not implemented yet. `PromptLayout` currently models only:
  - header block
  - editable prompt prefix
