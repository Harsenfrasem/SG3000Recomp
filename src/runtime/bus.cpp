#include "sgrecomp/bus.h"

#include "sgrecomp/joypad.h"
#include "sgrecomp/psg.h"
#include "sgrecomp/vdp.h"

#include <algorithm>

namespace sgrecomp {

Bus::Bus(ConsoleModel model, Vdp& vdp, Psg& psg, Joypad& joypad)
    : model_(model), vdp_(vdp), psg_(psg), joypad_(joypad) {}

void Bus::load_rom(std::span<const u8> rom) {
    rom_.assign(rom.begin(), rom.end());
    memory_.fill(0xFF);
    memory_control_ = 0;
    mapper_control_ = 0;
    mapper_slots_[0] = 0;
    mapper_slots_[1] = 1;
    mapper_slots_[2] = 2;
    bios_enabled_ = !bios_.empty();
    refresh_mapper();
}

void Bus::load_bios(std::span<const u8> bios) {
    bios_.assign(bios.begin(), bios.end());
    bios_enabled_ = !bios_.empty();
    refresh_mapper();
}

void Bus::set_bios_enabled(bool enabled) {
    bios_enabled_ = enabled && !bios_.empty();
    refresh_mapper();
}

u8 Bus::read(u16 address) const {
    if (address >= 0xC000) {
        return memory_[mirrored_ram_address(address)];
    }
    return memory_[address];
}

void Bus::write(u16 address, u8 value) {
    if (model_ == ConsoleModel::MasterSystem && address >= 0xFFFC) {
        if (address == 0xFFFC) {
            mapper_control_ = value;
        } else {
            mapper_slots_[address - 0xFFFD] = value;
        }
        refresh_mapper();
    }

    if (address >= 0xC000) {
        const u16 ram = mirrored_ram_address(address);
        memory_[ram] = value;
        memory_[static_cast<u16>(ram + 0x2000)] = value;
        return;
    }
}

u8 Bus::input(u8 port) {
    if (port == 0x7E) {
        return vdp_.read_v_counter();
    }
    if (port == 0x7F) {
        return vdp_.read_h_counter();
    }
    if (port == 0xBE || port == 0x98) {
        return vdp_.read_data();
    }
    if (port == 0xBF || port == 0x99) {
        return vdp_.read_status();
    }
    if (port == 0xDC || port == 0xC0) {
        return joypad_.read_port_a();
    }
    if (port == 0xDD || port == 0xC1) {
        return joypad_.read_port_b();
    }
    return 0xFF;
}

void Bus::output(u8 port, u8 value) {
    if (model_ == ConsoleModel::MasterSystem && port == 0x3E) {
        memory_control_ = value;
        set_bios_enabled((value & 0x08) == 0);
        return;
    }
    if (port == 0xBE || port == 0x98) {
        vdp_.write_data(value);
        return;
    }
    if (port == 0xBF || port == 0x99) {
        vdp_.write_control(value);
        return;
    }
    if (port == 0x7E || port == 0x7F) {
        psg_.write(value);
    }
}

void Bus::refresh_mapper() {
    std::fill(memory_.begin(), memory_.begin() + 0xC000, 0xFF);
    if (!rom_.empty()) {
        const std::size_t banks = (rom_.size() + 0x3FFF) / 0x4000;
        for (int slot = 0; slot < 3; ++slot) {
            const std::size_t bank = mapper_slots_[slot] % std::max<std::size_t>(banks, 1);
            const std::size_t src = bank * 0x4000;
            const std::size_t dst = static_cast<std::size_t>(slot) * 0x4000;
            if (src >= rom_.size()) {
                continue;
            }
            const std::size_t len = std::min<std::size_t>(0x4000, rom_.size() - src);
            std::copy_n(rom_.begin() + static_cast<std::ptrdiff_t>(src), len, memory_.begin() + static_cast<std::ptrdiff_t>(dst));
        }

        if (rom_.size() > 0x400) {
            std::copy_n(rom_.begin(), 0x400, memory_.begin());
        }
    }

    if (bios_enabled_ && !bios_.empty()) {
        const std::size_t len = std::min<std::size_t>(bios_.size(), 0xC000);
        std::copy_n(bios_.begin(), len, memory_.begin());
    }
}

u16 Bus::mirrored_ram_address(u16 address) {
    return static_cast<u16>(0xC000 + ((address - 0xC000) & 0x1FFF));
}

} // namespace sgrecomp
