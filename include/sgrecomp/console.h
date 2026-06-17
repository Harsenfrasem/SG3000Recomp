#pragma once

#include "sgrecomp/bus.h"
#include "sgrecomp/enhancements.h"
#include "sgrecomp/joypad.h"
#include "sgrecomp/psg.h"
#include "sgrecomp/vdp.h"
#include "sgrecomp/z80.h"

#include <span>

namespace sgrecomp {

class Console {
public:
    explicit Console(ConsoleModel model);
    Console(ConsoleModel model, const EnhancementConfig& enhancements);

    void load_rom(std::span<const u8> rom);
    void load_bios(std::span<const u8> bios);
    void reset();
    void press_pause();
    void run_cycles(u64 cycles);
    void set_enhancements(const EnhancementConfig& enhancements);
    const EnhancementConfig& enhancements() const { return enhancements_; }

    Z80State& cpu() { return cpu_; }
    const Z80State& cpu() const { return cpu_; }
    Bus& bus() { return bus_; }
    const Bus& bus() const { return bus_; }
    Vdp& vdp() { return vdp_; }
    const Vdp& vdp() const { return vdp_; }
    Psg& psg() { return psg_; }
    const Psg& psg() const { return psg_; }
    Joypad& joypad() { return joypad_; }
    const Joypad& joypad() const { return joypad_; }

private:
    Vdp vdp_;
    Psg psg_;
    Joypad joypad_;
    Bus bus_;
    Z80State cpu_;
    ConsoleModel model_;
    EnhancementConfig enhancements_{};
};

} // namespace sgrecomp
