#pragma once

#include <istream>

#include "../parser/parser.hpp"
#include "../shell/shell.hpp"

namespace builtins {

enum class BuiltinKind {
    None,
    Exit,
    Cd,
    Pwd,
    Type,
    Help,
    Source,
    History,
    Ps,
    Kill,
    Alias,
    Unalias,
    Set,
    Export,
    Unset
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
int source_file(shell::ShellState &state, const std::string &path,
                bool silent_missing = false);
int source_stream(shell::ShellState &state, std::istream &stream);

bool is_builtin_name(const std::string &name);

} // namespace builtins
