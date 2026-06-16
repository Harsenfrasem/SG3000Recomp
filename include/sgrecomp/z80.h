#pragma once

#include "sgrecomp/types.h"

#include <array>
#include <iosfwd>
#include <string>

namespace sgrecomp {

class Bus;

struct Z80State {
    u8 a = 0;
    u8 f = 0;
    u8 b = 0;
    u8 c = 0;
    u8 d = 0;
    u8 e = 0;
    u8 h = 0;
    u8 l = 0;
    u8 a_alt = 0;
    u8 f_alt = 0;
    u8 b_alt = 0;
    u8 c_alt = 0;
    u8 d_alt = 0;
    u8 e_alt = 0;
    u8 h_alt = 0;
    u8 l_alt = 0;
    u8 ixh = 0;
    u8 ixl = 0;
    u8 iyh = 0;
    u8 iyl = 0;
    u16 sp = 0xDFF0;
    u16 pc = 0;
    u16 last_pc = 0;
    u8 i = 0;
    u8 r = 0;
    bool iff1 = false;
    bool iff2 = false;
    u8 interrupt_mode = 0;
    bool halted = false;
    u64 cycles = 0;

    u16 af() const { return make_u16(f, a); }
    u16 bc() const { return make_u16(c, b); }
    u16 de() const { return make_u16(e, d); }
    u16 hl() const { return make_u16(l, h); }
    void set_af(u16 value);
    void set_bc(u16 value);
    void set_de(u16 value);
    void set_hl(u16 value);
};

struct DecodedInstruction {
    u16 pc = 0;
    u8 opcode = 0;
    u8 size = 1;
    u8 cycles = 4;
    std::string mnemonic;
};

DecodedInstruction decode_z80(const std::array<u8, 0x10000>& memory, u16 pc);
void dump_z80_state(std::ostream& out, const Z80State& cpu);
bool service_maskable_interrupt(Z80State& cpu, Bus& bus);
void execute_one(Z80State& cpu, Bus& bus);

} // namespace sgrecomp
