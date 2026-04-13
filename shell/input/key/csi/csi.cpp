#include "csi.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace shell::input::key::csi {
namespace {

bool is_csi_final_byte(char ch) { return ch >= '@' && ch <= '~'; }

bool parse_decimal(const std::string &text, int &value) {
    if (text.empty()) {
        return false;
    }

    value = 0;
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }

        value = value * 10 + (ch - '0');
    }

    return true;
}

bool parse_csi_parameters(const std::string &text, std::vector<int> &params) {
    params.clear();
    if (text.empty()) {
        return true;
    }

    size_t start = 0;
    if (text[start] == '?') {
        ++start;
    }

    while (start <= text.size()) {
        const size_t end = text.find(';', start);
        const std::string part = text.substr(
            start, end == std::string::npos ? std::string::npos : end - start);

        int value = 0;
        if (!parse_decimal(part, value)) {
            return false;
        }

        params.push_back(value);
        if (end == std::string::npos) {
            break;
        }

        start = end + 1;
    }

    return true;
}

unsigned decode_csi_modifiers(int modifier_param) {
    if (modifier_param <= 1) {
        return KeyModNone;
    }

    const unsigned bits = static_cast<unsigned>(modifier_param - 1);
    unsigned modifiers = KeyModNone;

    if ((bits & 1U) != 0U) {
        modifiers |= KeyModShift;
    }
    if ((bits & 2U) != 0U) {
        modifiers |= KeyModAlt;
    }
    if ((bits & 4U) != 0U) {
        modifiers |= KeyModCtrl;
    }

    return modifiers;
}

} // namespace

KeyPress decode(DecodeContext &context) {
    std::string parameter_text;
    char final_byte = '\0';

    while (true) {
        if (!read_next_byte(context, final_byte)) {
            return make_key_press(KeyKind::Ignored);
        }

        if (is_csi_final_byte(final_byte)) {
            break;
        }

        parameter_text.push_back(final_byte);
        if (parameter_text.size() > 32) {
            return make_key_press(KeyKind::Ignored);
        }
    }

    std::vector<int> params;
    if (!parse_csi_parameters(parameter_text, params)) {
        return make_key_press(KeyKind::Ignored);
    }

    const unsigned modifiers =
        params.size() >= 2 ? decode_csi_modifiers(params.back())
                           : static_cast<unsigned>(KeyModNone);

    switch (final_byte) {
    case 'A':
        return make_key_press(KeyKind::ArrowUp, modifiers);
    case 'B':
        return make_key_press(KeyKind::ArrowDown, modifiers);
    case 'C':
        return make_key_press(KeyKind::ArrowRight, modifiers);
    case 'D':
        return make_key_press(KeyKind::ArrowLeft, modifiers);
    case 'H':
        return make_key_press(KeyKind::Home, modifiers);
    case 'F':
        return make_key_press(KeyKind::End, modifiers);
    case '~':
        if (params.empty()) {
            return make_key_press(KeyKind::Ignored);
        }

        switch (params.front()) {
        case 1:
        case 7:
            return make_key_press(KeyKind::Home, modifiers);
        case 3:
            return make_key_press(KeyKind::Delete, modifiers);
        case 4:
        case 8:
            return make_key_press(KeyKind::End, modifiers);
        default:
            return make_key_press(KeyKind::Ignored);
        }
    default:
        return make_key_press(KeyKind::Ignored);
    }
}

} // namespace shell::input::key::csi
