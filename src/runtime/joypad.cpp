#include "sgrecomp/joypad.h"

namespace sgrecomp {

u8 Joypad::read_port_a() const {
    return static_cast<u8>(~player1_ & 0x3F);
}

u8 Joypad::read_port_b() const {
    return 0xFF;
}

} // namespace sgrecomp
