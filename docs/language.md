# Language

## Language Shape

esh should be command-first and block-structured.

Example target syntax:

```esh
fn greet(name)
    echo "hello $name"
end

if test -d shell
    echo "shell exists"
else
    echo "shell missing"
end

fn __zoxide_add()
    command zoxide add -- "$PWD"
end

hook chdir __zoxide_add
```


### Function Syntax

Canonical function declaration:

```esh
fn IDENT(param_list)
    statements
end
```

Examples:

```esh
fn hello()
    echo "hello"
end

fn greet(name)
    echo "hello $name"
end

fn copy_file(source, target)
    cp "$source" "$target"
end
```

The parameter list should be comma-separated:

```text
param_list -> empty | IDENT ("," IDENT)*
```

Semantics:
- Parameters are local variables inside the function.
- Missing arguments bind as empty strings.
- Extra arguments remain available through `$argv`.
- `$argv` contains all arguments passed to the function.
- `$argc` exposes argument count.
- A function runs in the current shell process, not in a child process.
- A function can mutate shell state, change directories, set variables, and register hooks.

Example:

```esh
fn z(query)
    if test -z "$query"
        builtin cd "$HOME"
        return $?
    end

    echo "query is $query"
end
```

### Command Calls

Most statements are command calls:

```esh
echo hello
test -d shell && echo yes
echo hello > out.txt
sleep 10 &
```

### Command Resolution

Command lookup order:

```text
special forms / syntax
functions
builtins
aliases
external commands
```

There are also explicit bypass commands:

- `command name args...`
  Run an external command and bypass functions/aliases.
- `builtin name args...`
  Run a shell builtin and bypass functions/aliases.

This is required for integration scripts.

Example:

```esh
fn cd(path)
    echo "custom cd wrapper"
    builtin cd "$path"
end

fn zoxide_add()
    command zoxide add -- "$PWD"
end
```

### Variables

Current shell variables are scalar strings:

```cpp
struct ShellVariable {
    std::string value;
    bool exported;
};
```

- `$name` expands a variable.
- Undefined variables expand to an empty string.
- `$?` expands the last command status.
- Single quotes disable expansion.
- Double quotes allow expansion but preserve one argument.
- No automatic word splitting after variable expansion.

Example:

```esh
let name = "hello world"
echo "$name"
```

#### Assignment

The shell currently supports assignment words and `set` / `export`.

For scripts, a clearer assignment form is added:

```esh
let name = value
let dir = "$HOME/code"
let here = $(pwd)
```

Why `let`:
- It is explicit.
- It does not conflict with current `set` behavior.

### Quoting

Rules:
- Single quotes produce literal text.
- Double quotes allow variable expansion and command substitution.
- Backslash escapes preserve existing shell behavior.
- No word splitting happens just because a variable contains spaces.

Example:

```esh
let file = "hello world.txt"
cat "$file"
```

This should read one file named `hello world.txt`.

### Command Substitution

Command substitution captures stdout from a command:

```esh
let here = $(pwd)
let target = $(command zoxide query -- "$query")
```

Rules:
- Run the inner command in a capture context.
- Remove trailing newlines.
- Preserve other whitespace.
- The captured output becomes one string.

### Conditionals

Conditionals are command-status based:

```esh
if command
    statements
else
    statements
end
```

Example:

```esh
if test -d shell
    echo "shell exists"
else
    echo "missing"
end
```

The `if` condition succeeds when the command returns status `0`.

Negation can be achieved via builtin `not`:

```esh
if not exists zoxide
    echo "zoxide is missing"
end
```

### Loops

Syntax rules:

```esh
while test -n "$work"
    echo "$work"
    let work = ""
end

for item in shell parser builtins
    echo "$item"
end
```

Loop control:

```esh
break
continue
```

### Return

```esh
fn fail()
    return 1
end
```

Rules:
- `return` without an argument returns the current status.
- `return N` returns status `N`.

### Tests and Predicates

A minimal `test` builtin is enough for many scripts:

```esh
test -d path
test -f path
test -e path
test -n string
test -z string
test a = b
test a != b
```

Also useful:

```esh
true
false
exists command_name
not command
```

`exists` should be script-friendly:
- return `0` if the command exists
- return nonzero if it does not
- print nothing by default

### Hooks

Initial hooks:
- `chdir`
  Runs after a successful directory change.
- `prompt`
  Runs before prompt rendering.
- `preexec`
  Runs before executing a user command.
- `postexec`
  Runs after executing a user command.

Syntax rules:
```esh
hook hook_name function_name
```

Example:

```esh
fn log_dir()
    echo "$PWD" >> /tmp/esh_dirs
end

hook chdir log_dir
```

Hook behavior should be explicit:
- Hooks run in the current shell process.
- Hook failures don't undo the action that triggered them.
- Hook order should be registration order.

## Native Integration Scripts

`esh` should ship native integration scripts instead of trying to source scripts generated for other shells.

Repository examples can live in:
```text
scripts/
    zoxide.esh
    direnv.esh
    fzf.esh
```

User-loadable scripts are searched from:
```text
~/.config/esh/scripts
~/.local/share/esh/scripts
```

Then users can write:

```esh
source zoxide.esh
```

## Config Layout

Preferred config layout:

```text
~/.config/esh/eshrc
~/.config/esh/scripts/
~/.local/share/esh/history
```

Compatibility rule:
- If `~/.config/esh/eshrc` exists, source it.
- Otherwise, fall back to `~/.eshrc`.
- Do not source both automatically.

This avoids double initialization bugs.

## Grammar Sketch

This is a rough target grammar.

```text
program            -> statement_list EOF
block              -> statement_list

statement_list     -> terminator*
                      (statement (terminator+ statement)*)?
                      terminator*

statement          -> fn_decl
                    | if_stmt
                    | while_stmt
                    | for_stmt
                    | hook_stmt
                    | return_stmt
                    | break_stmt
                    | continue_stmt
                    | command_chain

fn_decl            -> "fn" IDENT "(" param_list? ")" terminator+ block "end"
param_list         -> IDENT ("," IDENT)*

if_stmt            -> "if" command_chain terminator+ block else_part? "end"
else_part          -> "else" terminator+ block

while_stmt         -> "while" command_chain terminator+ block "end"

for_stmt           -> "for" IDENT "in" word_list terminator+ block "end"

hook_stmt          -> "hook" IDENT IDENT

return_stmt        -> "return" word?
break_stmt         -> "break"
continue_stmt      -> "continue"

block              -> statement_list?

command_chain      -> pipeline (("&&" | "||") pipeline)*
pipeline           -> simple_command ("|" simple_command)*
simple_command     -> command_prefix+ command_invocation?
                    | command_invocation

command_invocation -> command_word command_suffix*

command_prefix     -> assignment_word | redirect
command_suffix     -> word | redirect

redirect           -> "<" word | ">" word | ">>" word
word_list          -> word*
terminator         -> NEWLINE | ";"
```

The lexer should only recognize `assignment_word` while reading the command prefix. Once a real `command_word` has been consumed, every later token is parsed as a normal `word`, even if it looks like `IDENT=value`.

## Runtime Architecture

Target flow:

```text
interactive input or script file
    |
    v
  lexer
    |
    v
  parser
    |
    v
   AST
    |
    v
script interpreter
    |
    +-- functions / call frames / hooks / control flow
    |
    v
command runtime
    |
    +-- expansion
    +-- function lookup
    +-- builtin lookup
    +-- external command lookup
    |
    v
shell/exec
```
