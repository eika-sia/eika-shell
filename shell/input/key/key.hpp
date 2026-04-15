#pragma once

#include <string>

namespace shell::input::key {

enum class InputEventKind {
    TextInput,
    Key,
    Paste,
    Resized,
    Interrupted,
    ReadEof,
    Ignored,
};

enum class EditorKey {
    Enter,
    Tab,
    Backspace,
    Delete,
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
    Home,
    End,
    CtrlA,
    CtrlD,
    CtrlE,
    CtrlL,
};

enum KeyModifier {
    KeyModNone = 0,
    KeyModShift = 1 << 0,
    KeyModAlt = 1 << 1,
    KeyModCtrl = 1 << 2,
};

struct InputEvent {
    InputEventKind kind = InputEventKind::Ignored;
    EditorKey key = EditorKey::Enter;
    unsigned modifiers = KeyModNone;
    std::string text;
};

using ReadByteFn = bool (*)(void *context, char &ch);

struct DecodeContext {
    ReadByteFn read_byte = nullptr;
    void *reader_context = nullptr;
};

using EscapeDecodeFn = InputEvent (*)(DecodeContext &context);

struct EscapeDecoderSpec {
    char introducer = '\0';
    EscapeDecodeFn decode = nullptr;
};

inline bool read_next_byte(DecodeContext &context, char &ch) {
    return context.read_byte != nullptr &&
           context.read_byte(context.reader_context, ch);
}

inline InputEvent make_key_event(EditorKey key,
                                 unsigned modifiers = KeyModNone) {
    return InputEvent{InputEventKind::Key, key, modifiers, {}};
}

inline InputEvent make_text_event(const std::string &text,
                                  unsigned modifiers = KeyModNone) {
    return InputEvent{InputEventKind::TextInput, EditorKey::Enter, modifiers,
                      text};
}

inline InputEvent make_paste_event(const std::string &text) {
    return InputEvent{InputEventKind::Paste, EditorKey::Enter, KeyModNone,
                      text};
}

inline InputEvent make_system_event(InputEventKind kind) {
    return InputEvent{kind, EditorKey::Enter, KeyModNone, {}};
}

inline bool has_modifier(const InputEvent &event, KeyModifier modifier) {
    return (event.modifiers & static_cast<unsigned>(modifier)) != 0U;
}

InputEvent read_event();

} // namespace shell::input::key
