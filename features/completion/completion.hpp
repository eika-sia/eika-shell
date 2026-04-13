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

struct CompletionResult {
    CompletionAction action = CompletionAction::None;
    size_t replace_begin = 0;
    size_t replace_end = 0;
    std::string replacement;
    std::vector<std::string> candidates;
};

CompletionResult complete_at_cursor(const shell::ShellState &state,
                                    const std::string &buf, size_t cursor);

} // namespace features
