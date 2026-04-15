#pragma once

#include "key.hpp"

namespace shell::input::key {

InputEvent decode_control_byte(unsigned char ch);
InputEvent decode_alt_prefixed_byte(unsigned char ch);

} // namespace shell::input::key
