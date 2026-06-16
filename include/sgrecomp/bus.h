#pragma once

#include "sgrecomp/types.h"

#include <array>
#include <span>
#include <vector>

namespace sgrecomp {

class Vdp;
class Psg;
class Joypad;

enum class ConsoleModel {
    MasterSystem,
    SG3000,
};

class Bus {
public:
    Bus(ConsoleModel model, Vdp& vdp, Psg& psg, Joypad& joypad);

    void load_rom(std::span<const u8> rom);
    u8 read(u16 address) const;
    void write(u16 address, u8 value);
    u8 input(u8 port);
    void output(u8 port, u8 value);

    const std::array<u8, 0x10000>& debug_memory() const { return memory_; }

private:
    ConsoleModel model_;
    Vdp& vdp_;
    Psg& psg_;
    Joypad& joypad_;
    std::array<u8, 0x10000> memory_{};
    std::vector<u8> rom_;
    u8 mapper_control_ = 0;
    u8 mapper_slots_[3] = {0, 1, 2};

    void refresh_mapper();
};

} // namespace sgrecomp
