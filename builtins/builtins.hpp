#pragma once

#include "../parser/parser.hpp"
#include "../shell/shell.hpp"
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

bool handle_builtin(ShellState &state, const Command &cmd);

BuiltinKind classify_builtin(const Command &cmd);
BuiltinDecision decide_builtin(const Command &cmd, ExecContext ctx);
BuiltinPlan plan_builtin(const Command &cmd, ExecContext ctx);
int run_builtin(ShellState &state, const Command &cmd, BuiltinKind kind);
