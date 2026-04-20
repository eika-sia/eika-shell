#include "input.hpp"

#include <cstdio>
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

#include "../../features/completion/completion.hpp"
#include "../../features/completion/completion_format.hpp"
#include "../prompt/prompt.hpp"
#include "../prompt/render_utils.hpp"
#include "../shell.hpp"
#include "../signals/signals.hpp"
#include "../terminal/terminal.hpp"
#include "./editor_state/editor_state.hpp"
#include "./key/key.hpp"
#include "./session_state/session_state.hpp"

namespace shell::input {
namespace {
const std::string blue_bold = "\033[1;34m";
const std::string reverse_video = "\033[7m";
const std::string reset = "\033[0m";

enum class KeyHandlingResult {
    ContinueLoop,
    FinishInput,
    Ignore,
};

struct InputSession {
    struct termios saved_state_{};
    bool active = false;

    ~InputSession() {
        if (!active) {
            return;
        }

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_state_) == -1) {
            perror("tcsetattr");
        }
        active = false;
    }
};

struct CompletionMenuRenderState {
    size_t rows = 0;
};

struct InputContext {
    const shell::ShellState &state;
    shell::prompt::InputRenderState &render_state;
    editor_state::LineBuffer &buffer;
    session_state::EditorSessionState &session;
    size_t history_size = 0;
    InputResult &result;
    CompletionMenuRenderState completion_render_state;
};

bool begin_input_session(InputSession &session) {
    session.active = false;

    if (tcgetattr(STDIN_FILENO, &session.saved_state_) == -1) {
        perror("tcgetattr");
        return false;
    }

    key::set_backspace_byte(
        static_cast<unsigned char>(session.saved_state_.c_cc[VERASE]));

    struct termios raw = session.saved_state_;
    raw.c_lflag &= ~ICANON;
    raw.c_lflag &= ~ECHO;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        return false;
    }

    session.active = true;
    return true;
}

void redraw_buffer(const InputContext &context, bool full_prompt = false) {
    shell::prompt::redraw_input_line(context.render_state, context.state,
                                     context.buffer.text, context.buffer.cursor,
                                     full_prompt);
}

std::string format_completion_candidate(const std::string &candidate,
                                        bool colorize = true) {
    if (candidate.empty()) {
        return candidate;
    }

    const bool has_trailing_slash = candidate.back() == '/';
    std::string basename_source = candidate;
    if (has_trailing_slash && candidate.size() > 1) {
        basename_source.pop_back();
    }

    const std::string basename = features::get_basename_part(basename_source);
    if (basename.empty()) {
        return has_trailing_slash ? "/" : candidate;
    }

    if (!has_trailing_slash) {
        return basename;
    }

    if (!colorize) {
        return basename + "/";
    }

    return blue_bold + basename + "/" + reset;
}

size_t print_completion_candidates(const std::vector<std::string> &items,
                                   bool has_selected_candidate = false,
                                   size_t selected_index = 0) {
    if (items.empty()) {
        return 0;
    }

    const size_t term_cols =
        shell::prompt::render_utils::get_terminal_columns();

    std::vector<std::string> cleaned;
    cleaned.reserve(items.size());
    for (const std::string &item : items) {
        cleaned.push_back(format_completion_candidate(item));
    }

    shell::terminal::write_stdout_line("");

    size_t max_width = 0;
    for (const std::string &item : cleaned) {
        max_width = std::max(max_width,
                             prompt::render_utils::measure_display_width(item));
    }

    const size_t gutter = 2;
    const size_t cell_width = max_width + gutter;
    const size_t cols =
        cell_width == 0
            ? 1
            : std::max<size_t>(1, (term_cols + gutter) / cell_width);
    const size_t rows = (cleaned.size() + cols - 1) / cols;

    for (size_t row = 0; row < rows; ++row) {
        std::string line;

        for (size_t col = 0; col < cols; ++col) {
            const size_t index = row * cols + col;
            if (index >= cleaned.size()) {
                break;
            }

            const std::string &item = cleaned[index];
            const bool is_selected =
                has_selected_candidate && index == selected_index;

            if (!is_selected) {
                line += item;

                if (col + 1 < cols && index + 1 < cleaned.size()) {
                    const size_t padding =
                        cell_width -
                        prompt::render_utils::measure_display_width(item);
                    line.append(padding, ' ');
                }
                continue;
            }

            std::string highlighted_cell =
                format_completion_candidate(items[index], false);
            const size_t highlighted_width =
                prompt::render_utils::measure_display_width(highlighted_cell);
            const size_t padding = cell_width > highlighted_width
                                       ? cell_width - highlighted_width
                                       : 0;
            highlighted_cell.append(padding, ' ');

            line += reverse_video + highlighted_cell + reset;
        }

        shell::terminal::write_stdout_line(line);
    }
    return rows + 1;
}

std::string normalize_paste_for_single_line(const std::string &text) {
    std::string normalized;
    normalized.reserve(text.size());

    for (unsigned char ch : text) {
        switch (ch) {
        case '\r':
        case '\n':
        case '\t':
        case '\v':
        case '\f':
            normalized.push_back(' ');
            break;
        default:
            if (ch >= 32U && ch != 127U) {
                normalized.push_back(static_cast<char>(ch));
            }
            break;
        }
    }

    return normalized;
}

bool has_rendered_completion_menu(const InputContext &context) {
    return context.completion_render_state.rows > 0;
}

bool has_active_completion_selection(const InputContext &context) {
    return context.session.completion.active;
}

void reset_completion_menu_render_state(InputContext &context) {
    context.completion_render_state = {};
}

void clear_completion_menu_and_prompt_block(const InputContext &context) {
    const size_t columns = context.render_state.terminal_columns;
    const prompt::render_utils::RenderMetrics metrics =
        prompt::render_utils::measure_render_state(context.render_state,
                                                   columns);
    const size_t rows_above_cursor = metrics.header_rows + metrics.cursor_row;
    const size_t rows_to_clear =
        metrics.total_rows + context.completion_render_state.rows;

    shell::terminal::write_stdout(prompt::render_utils::clear_render_block(
        rows_above_cursor, rows_to_clear));
}

prompt::render_utils::CursorGeometry
input_cursor_geometry(const InputContext &context, size_t columns) {
    const bool cursor_at_line_end = context.render_state.cursor_display_width ==
                                    context.render_state.input_display_width;

    return prompt::render_utils::compute_cursor_geometry(
        context.render_state.prompt_prefix_display_width,
        context.render_state.cursor_display_width, columns, cursor_at_line_end);
}

prompt::render_utils::CursorGeometry
input_end_geometry(const InputContext &context, size_t columns) {
    return prompt::render_utils::compute_cursor_geometry(
        context.render_state.prompt_prefix_display_width,
        context.render_state.input_display_width, columns, true);
}

std::string restore_input_cursor(const InputContext &context, size_t columns,
                                 size_t rows_below_input_end) {
    const prompt::render_utils::CursorGeometry cursor =
        input_cursor_geometry(context, columns);
    const prompt::render_utils::CursorGeometry end =
        input_end_geometry(context, columns);

    std::string frame = "\r";
    const size_t current_row = end.row + rows_below_input_end;
    if (current_row > cursor.row) {
        frame += "\033[" + std::to_string(current_row - cursor.row) + "A";
    } else if (cursor.row > current_row) {
        frame += "\033[" + std::to_string(cursor.row - current_row) + "B";
    }
    if (cursor.column > 0) {
        frame += "\033[" + std::to_string(cursor.column) + "C";
    }
    return frame;
}

std::string move_from_cursor_to_input_end(const InputContext &context,
                                          size_t columns) {
    const size_t cursor_row = prompt::render_utils::measure_render_state(
                                  context.render_state, columns)
                                  .cursor_row;
    const size_t end_row = input_end_geometry(context, columns).row;

    std::string frame;
    if (end_row > cursor_row) {
        frame += "\033[" + std::to_string(end_row - cursor_row) + "B";
    } else if (cursor_row > end_row) {
        frame += "\033[" + std::to_string(cursor_row - end_row) + "A";
    }
    frame += "\r";
    return frame;
}

std::string move_from_cursor_to_menu_start(const InputContext &context,
                                           size_t columns) {
    const size_t cursor_row = prompt::render_utils::measure_render_state(
                                  context.render_state, columns)
                                  .cursor_row;
    const size_t end_row = input_end_geometry(context, columns).row;

    std::string frame;
    if (end_row > cursor_row) {
        frame += "\033[" + std::to_string(end_row - cursor_row) + "B";
    } else if (cursor_row > end_row) {
        frame += "\033[" + std::to_string(cursor_row - end_row) + "A";
    }
    frame += "\033[1B";
    frame += "\r";
    return frame;
}

void render_completion_menu_below_prompt(InputContext &context) {
    const size_t columns = context.render_state.terminal_columns;
    shell::terminal::write_stdout(
        move_from_cursor_to_input_end(context, columns));

    const bool has_selected_candidate =
        context.session.completion.active &&
        context.session.completion.preview_active &&
        !context.session.completion.candidates.empty();
    const size_t selected_index =
        has_selected_candidate
            ? context.session.completion.selected_index %
                  context.session.completion.candidates.size()
            : 0;

    const size_t rendered_rows =
        print_completion_candidates(context.session.completion.candidates,
                                    has_selected_candidate, selected_index);
    context.completion_render_state.rows = rendered_rows;
    shell::terminal::write_stdout(
        restore_input_cursor(context, columns, rendered_rows));
}

void redraw_completion_menu_only(InputContext &context) {
    if (!has_rendered_completion_menu(context)) {
        render_completion_menu_below_prompt(context);
        return;
    }

    const size_t columns = context.render_state.terminal_columns;
    const size_t previous_rows = context.completion_render_state.rows;

    std::string frame = move_from_cursor_to_menu_start(context, columns);
    frame += prompt::render_utils::clear_render_block(0, previous_rows);
    frame += "\033[1A\r";
    shell::terminal::write_stdout(frame);

    render_completion_menu_below_prompt(context);
}

void dismiss_completion_menu(InputContext &context) {
    if (!has_rendered_completion_menu(context)) {
        return;
    }

    const size_t columns = context.render_state.terminal_columns;
    const size_t rendered_rows = context.completion_render_state.rows;
    std::string frame = move_from_cursor_to_menu_start(context, columns);
    frame += prompt::render_utils::clear_render_block(0, rendered_rows);
    frame += restore_input_cursor(context, columns, 1);
    shell::terminal::write_stdout(frame);
    reset_completion_menu_render_state(context);
}

void hide_completion_menu_and_redraw(InputContext &context,
                                     bool full_prompt = false) {
    dismiss_completion_menu(context);
    redraw_buffer(context, full_prompt);
}

void redraw_completion_menu_and_prompt(InputContext &context) {
    clear_completion_menu_and_prompt_block(context);
    reset_completion_menu_render_state(context);
    redraw_buffer(context, true);
    render_completion_menu_below_prompt(context);
}

void redraw_if_changed(InputContext &context, bool changed,
                       bool full_prompt = false) {
    if (has_rendered_completion_menu(context) &&
        !has_active_completion_selection(context)) {
        hide_completion_menu_and_redraw(context);
        return;
    }

    if (!changed) {
        return;
    }

    redraw_buffer(context, full_prompt);
}

void apply_movement_and_redraw(InputContext &context,
                               editor_state::Movement movement) {
    redraw_if_changed(context,
                      editor_state::apply_movement(context.buffer, movement));
}

void apply_erase_and_redraw(InputContext &context,
                            editor_state::Erase erase_action) {
    redraw_if_changed(context, session_state::apply_erase(
                                   context.session, context.buffer,
                                   context.history_size, erase_action));
}

void apply_kill_and_redraw(InputContext &context,
                           editor_state::Kill kill_action) {
    redraw_if_changed(
        context, session_state::apply_kill(context.session, context.buffer,
                                           context.history_size, kill_action)
                     .changed);
}

void yank_kill_buffer_and_redraw(InputContext &context) {
    redraw_if_changed(
        context, session_state::yank_latest(context.session, context.buffer,
                                            context.history_size));
}

void yank_pop_and_redraw(InputContext &context) {
    redraw_if_changed(context,
                      session_state::yank_pop(context.session, context.buffer,
                                              context.history_size));
}

void apply_history_navigation_and_redraw(
    InputContext &context, editor_state::HistoryNavigation navigation) {
    redraw_if_changed(context, session_state::apply_history_navigation(
                                   context.session, context.buffer, navigation,
                                   context.state.history));
}

void replace_range_and_redraw(InputContext &context, size_t replace_begin,
                              size_t replace_end,
                              const std::string &replacement) {
    redraw_if_changed(context,
                      session_state::replace_range(
                          context.session, context.buffer, context.history_size,
                          replace_begin, replace_end, replacement));
}

void insert_input_text(InputContext &context, const std::string &text) {
    redraw_if_changed(
        context, session_state::insert_text(context.session, context.buffer,
                                            context.history_size, text));
}

void handle_completion_selection_escape(InputContext &context) {
    const bool changed = session_state::cancel_completion_selection(
        context.session, context.buffer, context.history_size);
    if (has_rendered_completion_menu(context)) {
        hide_completion_menu_and_redraw(context);
        return;
    }

    redraw_if_changed(context, changed);
}

void handle_tab_completion(InputContext &context) {
    if (has_active_completion_selection(context)) {
        const bool changed = session_state::cycle_completion_selection(
            context.session, context.buffer, context.history_size);
        if (!changed) {
            return;
        }

        if (has_rendered_completion_menu(context)) {
            redraw_buffer(context);
            redraw_completion_menu_only(context);
        } else {
            redraw_if_changed(context, true);
        }
        return;
    }

    const features::CompletionResult completion = features::complete_at_cursor(
        context.state, context.buffer.text, context.buffer.cursor);

    switch (completion.action) {
    case features::CompletionAction::None:
        session_state::note_non_kill_command(context.session);
        return;
    case features::CompletionAction::ReplaceToken:
        replace_range_and_redraw(context, completion.replace_begin,
                                 completion.replace_end,
                                 completion.replacement);
        return;
    case features::CompletionAction::ShowCandidates:
        session_state::note_non_kill_command(context.session);
        session_state::begin_completion_selection(
            context.session, context.buffer, completion.replace_begin,
            completion.replace_end, completion.candidates);
        render_completion_menu_below_prompt(context);
        return;
    }
}

KeyHandlingResult handle_character_key(InputContext &context,
                                       const key::InputEvent &event) {
    if (event.key != key::EditorKey::Character) {
        return KeyHandlingResult::Ignore;
    }

    const char binding = event.key_character;
    if (key::has_modifier(event, key::KeyModCtrl)) {
        switch (binding) {
        case 'a':
            session_state::note_non_kill_command(context.session);
            apply_movement_and_redraw(context, editor_state::Movement::Home);
            return KeyHandlingResult::ContinueLoop;
        case 'd':
            if (context.buffer.text.empty()) {
                session_state::note_non_kill_command(context.session);
                context.result.eof = true;
                return KeyHandlingResult::FinishInput;
            }

            apply_erase_and_redraw(context, editor_state::Erase::AtCursor);
            return KeyHandlingResult::ContinueLoop;
        case 'e':
            session_state::note_non_kill_command(context.session);
            apply_movement_and_redraw(context, editor_state::Movement::End);
            return KeyHandlingResult::ContinueLoop;
        case 'k':
            apply_kill_and_redraw(context, editor_state::Kill::ToLineEnd);
            return KeyHandlingResult::ContinueLoop;
        case 'l':
            session_state::note_non_kill_command(context.session);
            shell::terminal::write_stdout("\033[2J\033[H");
            redraw_buffer(context, true);
            return KeyHandlingResult::ContinueLoop;
        case 'u':
            apply_kill_and_redraw(context, editor_state::Kill::ToLineStart);
            return KeyHandlingResult::ContinueLoop;
        case 'w':
            apply_kill_and_redraw(context, editor_state::Kill::WordLeft);
            return KeyHandlingResult::ContinueLoop;
        case 'y':
            yank_kill_buffer_and_redraw(context);
            return KeyHandlingResult::ContinueLoop;
        case 'b':
            session_state::note_non_kill_command(context.session);
            apply_movement_and_redraw(context, editor_state::Movement::Left);
            return KeyHandlingResult::ContinueLoop;
        case 'f':
            session_state::note_non_kill_command(context.session);
            apply_movement_and_redraw(context, editor_state::Movement::Right);
            return KeyHandlingResult::ContinueLoop;
        case 'p':
            apply_history_navigation_and_redraw(
                context, editor_state::HistoryNavigation::Up);
            return KeyHandlingResult::ContinueLoop;
        case 'n':
            apply_history_navigation_and_redraw(
                context, editor_state::HistoryNavigation::Down);
            return KeyHandlingResult::ContinueLoop;
        default:
            break;
        }
    }

    if (key::has_modifier(event, key::KeyModAlt)) {
        switch (binding) {
        case 'b':
            session_state::note_non_kill_command(context.session);
            apply_movement_and_redraw(context,
                                      editor_state::Movement::WordLeft);
            return KeyHandlingResult::ContinueLoop;
        case 'f':
            session_state::note_non_kill_command(context.session);
            apply_movement_and_redraw(context,
                                      editor_state::Movement::WordRight);
            return KeyHandlingResult::ContinueLoop;
        case 'd':
            apply_kill_and_redraw(context, editor_state::Kill::WordRight);
            return KeyHandlingResult::ContinueLoop;
        case 'y':
            yank_pop_and_redraw(context);
            return KeyHandlingResult::ContinueLoop;
        default:
            break;
        }
    }

    return KeyHandlingResult::Ignore;
}

KeyHandlingResult handle_special_key(InputContext &context,
                                     const key::InputEvent &event) {
    switch (event.key) {
    case key::EditorKey::Character:
        return KeyHandlingResult::Ignore;
    case key::EditorKey::Escape:
        session_state::note_non_kill_command(context.session);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::Enter:
        session_state::note_non_kill_command(context.session);
        shell::terminal::write_stdout_line("");
        return KeyHandlingResult::FinishInput;
    case key::EditorKey::Tab:
        handle_tab_completion(context);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::Backspace:
        if (key::has_modifier(event, key::KeyModAlt) ||
            key::has_modifier(event, key::KeyModCtrl)) {
            apply_kill_and_redraw(context, editor_state::Kill::WordLeft);
            return KeyHandlingResult::ContinueLoop;
        }

        apply_erase_and_redraw(context, editor_state::Erase::BeforeCursor);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::Delete:
        apply_erase_and_redraw(context, editor_state::Erase::AtCursor);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::ArrowUp:
        apply_history_navigation_and_redraw(
            context, editor_state::HistoryNavigation::Up);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::ArrowDown:
        apply_history_navigation_and_redraw(
            context, editor_state::HistoryNavigation::Down);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::ArrowRight:
        session_state::note_non_kill_command(context.session);
        apply_movement_and_redraw(context,
                                  key::has_modifier(event, key::KeyModCtrl)
                                      ? editor_state::Movement::WordRight
                                      : editor_state::Movement::Right);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::ArrowLeft:
        session_state::note_non_kill_command(context.session);
        apply_movement_and_redraw(context,
                                  key::has_modifier(event, key::KeyModCtrl)
                                      ? editor_state::Movement::WordLeft
                                      : editor_state::Movement::Left);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::Home:
        session_state::note_non_kill_command(context.session);
        apply_movement_and_redraw(context, editor_state::Movement::Home);
        return KeyHandlingResult::ContinueLoop;
    case key::EditorKey::End:
        session_state::note_non_kill_command(context.session);
        apply_movement_and_redraw(context, editor_state::Movement::End);
        return KeyHandlingResult::ContinueLoop;
    }

    return KeyHandlingResult::Ignore;
}

InputResult read_interactive_fallback_command_line() {
    InputResult result{};

    while (true) {
        if (std::getline(std::cin, result.line)) {
            return result;
        }

        if (shell::signals::g_input_interrupted != 0) {
            shell::signals::g_input_interrupted = 0;
            std::cin.clear();
            result.interrupted = true;
            return result;
        }

        if (shell::signals::g_resize_pending != 0) {
            shell::signals::g_resize_pending = 0;
            std::cin.clear();
            continue;
        }

        if (std::cin.eof()) {
            result.eof = true;
            return result;
        }

        std::cin.clear();
        result.eof = true;
        return result;
    }
}

} // namespace

InputResult read_non_interactive_command_line() {
    InputResult result{};

    if (!std::getline(std::cin, result.line)) {
        result.eof = true;
    }

    return result;
}

InputResult read_command_line(shell::ShellState &state,
                              shell::prompt::InputRenderState &render_state) {
    if (!state.interactive) {
        return read_non_interactive_command_line();
    }

    InputResult result{};
    editor_state::LineBuffer buffer{};
    session_state::EditorSessionState session{};
    const size_t history_size = state.history.size();
    session_state::initialize_editor_session(session, history_size);
    InputContext context{state,        render_state, buffer, session,
                         history_size, result,       {}};

    InputSession input_session{};
    if (!begin_input_session(input_session)) {
        return read_interactive_fallback_command_line();
    }

    while (true) {
        const key::InputEvent event = key::read_event();

        if (event.kind == key::InputEventKind::ReadEof) {
            result.eof = true;
            break;
        }

        if (event.kind == key::InputEventKind::Interrupted) {
            clear_completion_menu_and_prompt_block(context);
            reset_completion_menu_render_state(context);
            render_state = {};
            result.interrupted = true;
            break;
        }

        if (has_active_completion_selection(context)) {
            if (event.kind == key::InputEventKind::Key &&
                event.key == key::EditorKey::Escape) {
                handle_completion_selection_escape(context);
                continue;
            }

            if (event.kind == key::InputEventKind::Key &&
                event.key == key::EditorKey::Enter) {
                session_state::confirm_completion_selection(context.session);
                hide_completion_menu_and_redraw(context);
                continue;
            }

            if (event.kind != key::InputEventKind::Ignored &&
                !(event.kind == key::InputEventKind::Key &&
                  event.key == key::EditorKey::Tab) &&
                event.kind != key::InputEventKind::Resized) {
                session_state::confirm_completion_selection(context.session);
            }
        }

        switch (event.kind) {
        case key::InputEventKind::TextInput:
            insert_input_text(context, event.text);
            continue;
        case key::InputEventKind::Paste:
            insert_input_text(context,
                              normalize_paste_for_single_line(event.text));
            continue;
        case key::InputEventKind::Key: {
            KeyHandlingResult key_result = handle_character_key(context, event);
            if (key_result == KeyHandlingResult::Ignore) {
                key_result = handle_special_key(context, event);
            }

            switch (key_result) {
            case KeyHandlingResult::ContinueLoop:
            case KeyHandlingResult::Ignore:
                continue;
            case KeyHandlingResult::FinishInput:
                break;
            }
            break;
        }
        case key::InputEventKind::Resized:
            if (has_rendered_completion_menu(context) &&
                has_active_completion_selection(context)) {
                redraw_completion_menu_and_prompt(context);
            } else {
                redraw_buffer(context);
            }
            continue;
        case key::InputEventKind::Ignored:
            continue;
        default:
            break;
        }

        break;
    }

    result.line = buffer.text;
    return result;
}

} // namespace shell::input
