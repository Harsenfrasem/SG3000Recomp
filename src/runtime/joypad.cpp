#include "sgrecomp/joypad.h"

namespace sgrecomp {

u8 Joypad::read_port_a() const {
    const u8 player1 = static_cast<u8>(~player1_ & 0x3F);
    const u8 player2_low = static_cast<u8>((~player2_ & 0x03) << 6);
    return static_cast<u8>(player1 | player2_low);
}

u8 Joypad::read_port_b() const {
    const u8 player2_high = static_cast<u8>((~player2_ >> 2) & 0x0F);
    return static_cast<u8>(0xF0 | player2_high);
}

} // namespace sgrecomp
