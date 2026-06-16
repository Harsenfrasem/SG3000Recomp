#pragma once

#include "sgrecomp/bus.h"
#include "sgrecomp/joypad.h"
#include "sgrecomp/psg.h"
#include "sgrecomp/vdp.h"
#include "sgrecomp/z80.h"

#include <span>

namespace sgrecomp {

class Console {
public:
    explicit Console(ConsoleModel model);

    void load_rom(std::span<const u8> rom);
    void reset();
    void run_cycles(u64 cycles);

    Z80State& cpu() { return cpu_; }
    Bus& bus() { return bus_; }
    Vdp& vdp() { return vdp_; }
    Psg& psg() { return psg_; }
    Joypad& joypad() { return joypad_; }

private:
    Vdp vdp_;
    Psg psg_;
    Joypad joypad_;
    Bus bus_;
    Z80State cpu_;
};

} // namespace sgrecomp
