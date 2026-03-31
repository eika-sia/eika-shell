#pragma once

#include "../parser/parser.hpp"
#include "../shell/shell.hpp"

namespace builtins {

enum class BuiltinKind {
    None,
    Exit,
    Cd,
    History,
    Ps,
    Kill,
    AliasList,
    AliasSet
};
enum class ExecContext {
    ForegroundStandalone,
    BackgroundStandalone,
    PipelineStage
};
enum class BuiltinDecision { External, RunInParent, RunInChild, Reject };

struct BuiltinPlan {
    BuiltinKind kind;
    BuiltinDecision decision;
};

BuiltinPlan plan_builtin(const parser::Command &cmd, ExecContext ctx);
int run_builtin(shell::ShellState &state, const parser::Command &cmd,
                BuiltinKind kind);

} // namespace builtins
