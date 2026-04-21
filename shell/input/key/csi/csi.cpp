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

bool is_prefix_of(const std::string &text, const std::string &prefix) {
    return prefix.compare(0, text.size(), text) == 0;
}

InputEvent decode_modified_codepoint(int codepoint, unsigned modifiers) {
    switch (codepoint) {
    case 8:
    case 127:
        return make_special_key_event(EditorKey::Backspace, modifiers);
    case 9:
        return make_special_key_event(EditorKey::Tab, modifiers);
    case 13:
        return make_special_key_event(EditorKey::Enter, modifiers);
    default:
        break;
    }

    if (codepoint >= 32 && codepoint <= 126) {
        return make_character_key_event(static_cast<char>(codepoint),
                                        modifiers);
    }

    return make_ignored_event();
}

InputEvent read_bracketed_paste(DecodeContext &context) {
    const std::string terminator = "\033[201~";
    std::string payload;
    std::string pending;

    while (true) {
        char ch = '\0';
        if (!read_next_byte(context, ch)) {
            payload += pending;
            return make_paste_event(payload);
        }

        pending.push_back(ch);
        if (pending == terminator) {
            return make_paste_event(payload);
        }

        while (!pending.empty() && !is_prefix_of(pending, terminator)) {
            payload.push_back(pending.front());
            pending.erase(pending.begin());
        }
    }
}

} // namespace

InputEvent decode(DecodeContext &context) {
    std::string parameter_text;
    char final_byte = '\0';

    while (true) {
        if (!read_next_byte(context, final_byte)) {
            return make_ignored_event();
        }

        if (is_csi_final_byte(final_byte)) {
            break;
        }

        parameter_text.push_back(final_byte);
        if (parameter_text.size() > 32) {
            return make_ignored_event();
        }
    }

    std::vector<int> params;
    if (!parse_csi_parameters(parameter_text, params)) {
        return make_ignored_event();
    }

    const unsigned modifiers = params.size() >= 2
                                   ? decode_csi_modifiers(params.back())
                                   : static_cast<unsigned>(KeyModNone);

    switch (final_byte) {
    case 'A':
        return make_special_key_event(EditorKey::ArrowUp, modifiers);
    case 'B':
        return make_special_key_event(EditorKey::ArrowDown, modifiers);
    case 'C':
        return make_special_key_event(EditorKey::ArrowRight, modifiers);
    case 'D':
        return make_special_key_event(EditorKey::ArrowLeft, modifiers);
    case 'H':
        return make_special_key_event(EditorKey::Home, modifiers);
    case 'F':
        return make_special_key_event(EditorKey::End, modifiers);
    case 'Z':
        return make_special_key_event(EditorKey::Tab, modifiers | KeyModShift);
    case '~':
        if (params.empty()) {
            return make_ignored_event();
        }

        switch (params.front()) {
        case 27:
            if (params.size() < 3) {
                return make_ignored_event();
            }

            return decode_modified_codepoint(params[2],
                                             decode_csi_modifiers(params[1]));
        case 1:
        case 7:
            return make_special_key_event(EditorKey::Home, modifiers);
        case 3:
            return make_special_key_event(EditorKey::Delete, modifiers);
        case 4:
        case 8:
            return make_special_key_event(EditorKey::End, modifiers);
        case 200:
            return read_bracketed_paste(context);
        case 201:
            return make_ignored_event();
        default:
            return make_ignored_event();
        }
    case 'u':
        if (params.empty()) {
            return make_ignored_event();
        }

        return decode_modified_codepoint(params.front(), modifiers);
    default:
        return make_ignored_event();
    }
}

} // namespace shell::input::key::csi
