#pragma once

#include <string>
#include <vector>

namespace shell {
struct ShellState;
}

namespace features {

enum class CompletionAction {
    None,
    ReplaceToken,
    ShowCandidates,
};

enum class CompletionDisplayKind {
    Plain,
    Directory,
    Executable,
    Builtin,
    Alias,
};

inline int command_kind_rank(CompletionDisplayKind kind) {
    switch (kind) {
    case CompletionDisplayKind::Alias:
        return 0;
    case CompletionDisplayKind::Builtin:
        return 1;
    case CompletionDisplayKind::Plain:
        return 2;
    case CompletionDisplayKind::Executable:
        return 3;
    case CompletionDisplayKind::Directory:
        return 4;
    }

    return 5;
}

struct CompletionDisplayCandidate {
    std::string text;
    CompletionDisplayKind kind = CompletionDisplayKind::Plain;
};

struct CompletionResult {
    CompletionAction action = CompletionAction::None;
    size_t replace_begin = 0;
    size_t replace_end = 0;
    std::string replacement;
    std::vector<std::string> candidates;
    std::vector<CompletionDisplayCandidate> display_candidates;
};

CompletionResult complete_at_cursor(const shell::ShellState &state,
                                    const std::string &buf, size_t cursor);

} // namespace features
