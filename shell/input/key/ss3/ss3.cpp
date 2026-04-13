#include "ss3.hpp"

namespace shell::input::key::ss3 {

KeyPress decode(DecodeContext &context) {
    char final_byte = '\0';
    if (!read_next_byte(context, final_byte)) {
        return make_key_press(KeyKind::Ignored);
    }

    switch (final_byte) {
    case 'A':
        return make_key_press(KeyKind::ArrowUp);
    case 'B':
        return make_key_press(KeyKind::ArrowDown);
    case 'C':
        return make_key_press(KeyKind::ArrowRight);
    case 'D':
        return make_key_press(KeyKind::ArrowLeft);
    case 'H':
        return make_key_press(KeyKind::Home);
    case 'F':
        return make_key_press(KeyKind::End);
    default:
        return make_key_press(KeyKind::Ignored);
    }
}

} // namespace shell::input::key::ss3
