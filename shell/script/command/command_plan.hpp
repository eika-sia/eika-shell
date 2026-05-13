#pragma once

#include "../../../builtins/builtins.hpp"
#include "../../../parser/ast.hpp"
#include "../../shell.hpp"

namespace shell::script {

enum class CommandKind {
    Noop,
    Function,
    Builtin,
    External,
    Reject,
};

struct CommandPlan {
    CommandKind kind = CommandKind::External;
    parser::ast::SimpleCommand *cmd = nullptr;
    ShellFunction *function = nullptr;
    builtins::BuiltinPlan builtin{};
    std::string reject_message;
};

CommandPlan plan_command(ShellState &state, parser::ast::SimpleCommand &cmd,
                         builtins::ExecContext context);

} // namespace shell::script
