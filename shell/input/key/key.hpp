#pragma once

namespace shell::input::key {

enum class KeyKind {
    Character,
    Enter,
    CtrlA,
    CtrlD,
    CtrlE,
    CtrlL,
    Tab,
    Backspace,
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
    Home,
    End,
    Delete,
    ReadEof,
    Interrupted,
    Resized,
    Ignored,
};

enum KeyModifier {
    KeyModNone = 0,
    KeyModShift = 1 << 0,
    KeyModAlt = 1 << 1,
    KeyModCtrl = 1 << 2,
};

struct KeyPress {
    KeyKind kind = KeyKind::Ignored;
    unsigned modifiers = KeyModNone;
    char character = '\0';
};

using ReadByteFn = bool (*)(void *context, char &ch);

struct DecodeContext {
    ReadByteFn read_byte = nullptr;
    void *reader_context = nullptr;
};

using EscapeDecodeFn = KeyPress (*)(DecodeContext &context);

struct EscapeDecoderSpec {
    char introducer = '\0';
    EscapeDecodeFn decode = nullptr;
};

inline bool read_next_byte(DecodeContext &context, char &ch) {
    return context.read_byte != nullptr &&
           context.read_byte(context.reader_context, ch);
}

inline KeyPress make_key_press(KeyKind kind, unsigned modifiers = KeyModNone,
                               char character = '\0') {
    return KeyPress{kind, modifiers, character};
}

inline bool has_modifier(const KeyPress &key_press, KeyModifier modifier) {
    return (key_press.modifiers & static_cast<unsigned>(modifier)) != 0U;
}

KeyPress read_key();

} // namespace shell::input::key
