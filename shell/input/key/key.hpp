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
    Character,
    Escape,
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
};

enum KeyModifier {
    KeyModNone = 0,
    KeyModShift = 1 << 0,
    KeyModAlt = 1 << 1,
    KeyModCtrl = 1 << 2,
};

struct InputEvent {
    InputEventKind kind = InputEventKind::Ignored;
    EditorKey key = EditorKey::Character;
    unsigned modifiers = KeyModNone;
    char key_character = '\0';
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

inline char normalize_binding_character(char ch) {
    const unsigned char byte = static_cast<unsigned char>(ch);
    if (byte >= 'A' && byte <= 'Z') {
        return static_cast<char>(byte - 'A' + 'a');
    }

    return ch;
}

inline InputEvent make_special_key_event(EditorKey key,
                                         unsigned modifiers = KeyModNone) {
    return InputEvent{InputEventKind::Key, key, modifiers, '\0', {}};
}

inline InputEvent make_character_key_event(char key_character,
                                           unsigned modifiers = KeyModNone) {
    return InputEvent{InputEventKind::Key,
                      EditorKey::Character,
                      modifiers,
                      normalize_binding_character(key_character),
                      {}};
}

inline InputEvent make_text_event(const std::string &text,
                                  unsigned modifiers = KeyModNone) {
    return InputEvent{InputEventKind::TextInput, EditorKey::Character,
                      modifiers, '\0', text};
}

inline InputEvent make_paste_event(const std::string &text) {
    return InputEvent{InputEventKind::Paste, EditorKey::Character, KeyModNone,
                      '\0', text};
}

inline InputEvent make_system_event(InputEventKind kind) {
    return InputEvent{kind, EditorKey::Character, KeyModNone, '\0', {}};
}

inline InputEvent make_ignored_event() {
    return make_system_event(InputEventKind::Ignored);
}

inline bool has_modifier(const InputEvent &event, KeyModifier modifier) {
    return (event.modifiers & static_cast<unsigned>(modifier)) != 0U;
}

void set_backspace_byte(unsigned char backspace_byte);
InputEvent read_event();

} // namespace shell::input::key
