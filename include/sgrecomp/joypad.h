#pragma once

#include "sgrecomp/types.h"

namespace sgrecomp {

class Joypad {
public:
    enum Button : u8 {
        Up = 1 << 0,
        Down = 1 << 1,
        Left = 1 << 2,
        Right = 1 << 3,
        Button1 = 1 << 4,
        Button2 = 1 << 5,
    };

    void set_player1(u8 pressed_mask) { player1_ = pressed_mask; }
    void set_player2(u8 pressed_mask) { player2_ = pressed_mask; }
    u8 player1() const { return player1_; }
    u8 player2() const { return player2_; }
    u8 read_port_a() const;
    u8 read_port_b() const;

private:
    u8 player1_ = 0;
    u8 player2_ = 0;
};

} // namespace sgrecomp
