#include "key.hpp"

#include "../../signals/signals.hpp"
#include "./character_key.hpp"
#include "./csi/csi.hpp"
#include "./ss3/ss3.hpp"

#include <array>
#include <cerrno>
#include <poll.h>
#include <unistd.h>

namespace shell::input::key {
namespace {

constexpr int escape_followup_timeout_ms = 50;

enum class TimedReadStatus {
    ByteRead,
    Timeout,
    Eof,
    Signal,
    Error,
};

bool read_stdin_byte(void *, char &ch) {
    return read(STDIN_FILENO, &ch, 1) > 0;
}

bool take_pending_signal_event(InputEvent &event) {
    if (shell::signals::g_input_interrupted != 0) {
        shell::signals::g_input_interrupted = 0;
        event = make_system_event(InputEventKind::Interrupted);
        return true;
    }

    if (shell::signals::g_resize_pending != 0) {
        shell::signals::g_resize_pending = 0;
        event = make_system_event(InputEventKind::Resized);
        return true;
    }

    return false;
}

TimedReadStatus read_stdin_byte_with_timeout(char &ch, int timeout_ms) {
    struct pollfd poll_fd{};
    poll_fd.fd = STDIN_FILENO;
    poll_fd.events = POLLIN;

    while (true) {
        const int ready = poll(&poll_fd, 1, timeout_ms);
        if (ready == 0) {
            return TimedReadStatus::Timeout;
        }

        if (ready < 0) {
            if (errno == EINTR) {
                return TimedReadStatus::Signal;
            }
            return TimedReadStatus::Error;
        }

        if ((poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return TimedReadStatus::Error;
        }

        if ((poll_fd.revents & POLLIN) == 0) {
            continue;
        }

        const ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n > 0) {
            return TimedReadStatus::ByteRead;
        }

        if (n == 0) {
            return TimedReadStatus::Eof;
        }

        if (errno == EINTR) {
            return TimedReadStatus::Signal;
        }

        return TimedReadStatus::Error;
    }
}

const std::array<EscapeDecoderSpec, 2> &escape_decoders() {
    static const std::array<EscapeDecoderSpec, 2> decoders{{
        {'[', csi::decode},
        {'O', ss3::decode},
    }};
    return decoders;
}

InputEvent decode_escape_followup(char introducer) {
    DecodeContext context{read_stdin_byte, nullptr};

    for (const EscapeDecoderSpec &decoder : escape_decoders()) {
        if (decoder.introducer == introducer && decoder.decode != nullptr) {
            return decoder.decode(context);
        }
    }

    return decode_alt_prefixed_byte(static_cast<unsigned char>(introducer));
}

InputEvent decode_escape_sequence() {
    char introducer = '\0';
    switch (
        read_stdin_byte_with_timeout(introducer, escape_followup_timeout_ms)) {
    case TimedReadStatus::ByteRead:
        return decode_escape_followup(introducer);
    case TimedReadStatus::Timeout:
    case TimedReadStatus::Eof:
        return make_special_key_event(EditorKey::Escape);
    case TimedReadStatus::Signal: {
        InputEvent signal_event{};
        if (take_pending_signal_event(signal_event)) {
            return signal_event;
        }
        return make_special_key_event(EditorKey::Escape);
    }
    case TimedReadStatus::Error:
        return make_ignored_event();
    }

    return make_ignored_event();
}

} // namespace

InputEvent read_event() {
    char ch = '\0';
    const ssize_t n = read(STDIN_FILENO, &ch, 1);

    if (n == 0) {
        return make_system_event(InputEventKind::ReadEof);
    }

    if (n < 0) {
        if (errno == EINTR) {
            InputEvent signal_event{};
            if (take_pending_signal_event(signal_event)) {
                return signal_event;
            }
            return make_system_event(InputEventKind::Interrupted);
        }
        return make_ignored_event();
    }

    switch (static_cast<unsigned char>(ch)) {
    case '\n':
    case '\r':
        return make_special_key_event(EditorKey::Enter);
    case 8:
    case 127:
        return make_special_key_event(EditorKey::Backspace);
    case 9:
        return make_special_key_event(EditorKey::Tab);
    case '\033':
        return decode_escape_sequence();
    default:
        if (static_cast<unsigned char>(ch) >= 1U &&
            static_cast<unsigned char>(ch) <= 26U) {
            return decode_control_byte(static_cast<unsigned char>(ch));
        }
        return make_text_event(std::string(1, ch));
    }
}

} // namespace shell::input::key
