#pragma once

#include <optional>
#include <string>

namespace shell {
struct ShellState;
}

namespace shell::prompt::prompt_segments {

enum class PromptColor {
    Default,
    Black,
    Orange,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,
    BrightBlack,
    BrightRed,
    BrightGreen,
    BrightYellow,
    BrightBlue,
    BrightMagenta,
    BrightCyan,
    BrightWhite,
};

struct PromptStyleEffect {
    bool changes_background = false;
    PromptColor background = PromptColor::Default;
};

enum class PromptTokenKind {
    Data,
    Style,
    AutoPowerlineLeft,
    AutoPowerlineRight,
    Newline,
};

struct RenderedToken {
    std::string rendered;
    PromptTokenKind kind = PromptTokenKind::Data;
    PromptStyleEffect style;
};

std::optional<RenderedToken> render_token(const shell::ShellState &state,
                                          const std::string &token);
std::string render_powerline_left_transition(PromptColor from_background,
                                             PromptColor to_background);
std::string render_powerline_right_transition(PromptColor from_background,
                                              PromptColor to_background);
std::string final_reset_sequence();

} // namespace shell::prompt::prompt_segments
