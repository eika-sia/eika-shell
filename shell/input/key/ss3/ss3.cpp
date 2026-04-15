#include "ss3.hpp"

namespace shell::input::key::ss3 {

InputEvent decode(DecodeContext &context) {
    char final_byte = '\0';
    if (!read_next_byte(context, final_byte)) {
        return make_ignored_event();
    }

    switch (final_byte) {
    case 'A':
        return make_special_key_event(EditorKey::ArrowUp);
    case 'B':
        return make_special_key_event(EditorKey::ArrowDown);
    case 'C':
        return make_special_key_event(EditorKey::ArrowRight);
    case 'D':
        return make_special_key_event(EditorKey::ArrowLeft);
    case 'H':
        return make_special_key_event(EditorKey::Home);
    case 'F':
        return make_special_key_event(EditorKey::End);
    default:
        return make_ignored_event();
    }
}

} // namespace shell::input::key::ss3
