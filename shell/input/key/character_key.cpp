#include "character_key.hpp"

namespace shell::input::key {
namespace {

unsigned char g_backspace_byte = 127U;

bool is_backspace_byte(unsigned char ch) { return ch == 8U || ch == 127U; }

bool is_control_letter(unsigned char ch) { return ch >= 1U && ch <= 26U; }

char control_letter_from_byte(unsigned char ch) {
    return static_cast<char>('a' + (ch - 1U));
}

} // namespace

void set_backspace_byte(unsigned char backspace_byte) {
    if (is_backspace_byte(backspace_byte)) {
        g_backspace_byte = backspace_byte;
    }
}

InputEvent decode_backspace_byte(unsigned char ch) {
    if (!is_backspace_byte(ch)) {
        return make_ignored_event();
    }

    if (ch == g_backspace_byte) {
        return make_special_key_event(EditorKey::Backspace);
    }

    return make_special_key_event(EditorKey::Backspace, KeyModCtrl);
}

InputEvent decode_control_byte(unsigned char ch) {
    if (!is_control_letter(ch)) {
        return make_ignored_event();
    }

    return make_character_key_event(control_letter_from_byte(ch), KeyModCtrl);
}

InputEvent decode_alt_prefixed_byte(unsigned char ch) {
    switch (ch) {
    case '\n':
    case '\r':
        return make_special_key_event(EditorKey::Enter, KeyModAlt);
    case '\t':
        return make_special_key_event(EditorKey::Tab, KeyModAlt);
    case 8:
    case 127:
        return make_special_key_event(EditorKey::Backspace, KeyModAlt);
    case '\033':
        return make_special_key_event(EditorKey::Escape, KeyModAlt);
    default:
        break;
    }

    if (is_control_letter(ch)) {
        return make_character_key_event(control_letter_from_byte(ch),
                                        KeyModAlt | KeyModCtrl);
    }

    if (ch >= 32U) {
        return make_character_key_event(static_cast<char>(ch), KeyModAlt);
    }

    return make_ignored_event();
}

} // namespace shell::input::key
