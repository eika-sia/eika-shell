#include "key.hpp"

#include "../../signals/signals.hpp"
#include "./csi/csi.hpp"
#include "./ss3/ss3.hpp"

#include <array>
#include <cerrno>
#include <unistd.h>

namespace shell::input::key {
namespace {

bool read_stdin_byte(void *, char &ch) {
    return read(STDIN_FILENO, &ch, 1) > 0;
}

const std::array<EscapeDecoderSpec, 2> &escape_decoders() {
    static const std::array<EscapeDecoderSpec, 2> decoders{{
        {'[', csi::decode},
        {'O', ss3::decode},
    }};
    return decoders;
}

KeyPress decode_escape_sequence() {
    DecodeContext context{read_stdin_byte, nullptr};

    char introducer = '\0';
    if (!read_next_byte(context, introducer)) {
        return make_key_press(KeyKind::Ignored);
    }

    for (const EscapeDecoderSpec &decoder : escape_decoders()) {
        if (decoder.introducer == introducer && decoder.decode != nullptr) {
            return decoder.decode(context);
        }
    }

    return make_key_press(KeyKind::Ignored);
}

} // namespace

KeyPress read_key() {
    char ch = '\0';
    const ssize_t n = read(STDIN_FILENO, &ch, 1);

    if (n == 0) {
        return make_key_press(KeyKind::ReadEof);
    }

    if (n < 0) {
        if (errno == EINTR) {
            if (shell::signals::g_input_interrupted != 0) {
                shell::signals::g_input_interrupted = 0;
                return make_key_press(KeyKind::Interrupted);
            }
            if (shell::signals::g_resize_pending != 0) {
                shell::signals::g_resize_pending = 0;
                return make_key_press(KeyKind::Resized);
            }
            return make_key_press(KeyKind::Interrupted);
        }
        return make_key_press(KeyKind::Ignored);
    }

    switch (ch) {
    case '\n':
    case '\r':
        return make_key_press(KeyKind::Enter);
    case 1:
        return make_key_press(KeyKind::CtrlA);
    case 4:
        return make_key_press(KeyKind::CtrlD);
    case 5:
        return make_key_press(KeyKind::CtrlE);
    case 8:
    case 127:
        return make_key_press(KeyKind::Backspace);
    case 9:
        return make_key_press(KeyKind::Tab);
    case 12:
        return make_key_press(KeyKind::CtrlL);
    case '\033':
        return decode_escape_sequence();
    default:
        return make_key_press(KeyKind::Character, KeyModNone, ch);
    }
}

} // namespace shell::input::key
