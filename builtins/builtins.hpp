#pragma once

#include <string>
#include <vector>

#include "../parser/ast.hpp"
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

BuiltinPlan plan_builtin(const parser::ast::SimpleCommand &cmd,
                         ExecContext ctx);
int run_builtin(shell::ShellState &state, const parser::ast::SimpleCommand &cmd,
                BuiltinKind kind,
                std::vector<shell::diagnostics::Diagnostic> &diagnostics);
int source_file(shell::ShellState &state, const std::string &path,
                bool silent_missing = false);

bool is_builtin_name(const std::string &name);
std::vector<std::string> builtin_names();

} // namespace builtins
