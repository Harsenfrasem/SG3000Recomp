#include "sgrecomp/bus.h"

#include "sgrecomp/joypad.h"
#include "sgrecomp/psg.h"
#include "sgrecomp/vdp.h"

#include <algorithm>

namespace sgrecomp {

Bus::Bus(ConsoleModel model, Vdp& vdp, Psg& psg, Joypad& joypad)
    : model_(model), vdp_(vdp), psg_(psg), joypad_(joypad) {}

void Bus::load_rom(std::span<const u8> rom) {
    rom_header_removed_ = has_copier_header(rom);
    if (rom_header_removed_) {
        rom = rom.subspan(512);
    }
    rom_.assign(rom.begin(), rom.end());
    memory_.fill(0xFF);
    memory_control_ = 0;
    smapper_control_ = 0;
    smapper_slots_[0] = 0;
    smapper_slots_[1] = 1;
    smapper_slots_[2] = 2;
    cartridge_ram_dirty_ = false;
    bios_enabled_ = !bios_.empty();
    refresh_smapper();
}

void Bus::load_bios(std::span<const u8> bios) {
    bios_.assign(bios.begin(), bios.end());
    bios_enabled_ = !bios_.empty();
    refresh_smapper();
}

void Bus::set_bios_enabled(bool enabled) {
    bios_enabled_ = enabled && !bios_.empty();
    refresh_smapper();
}

bool Bus::cartridge_ram_enabled() const {
    return slot2_cartridge_ram_enabled();
}

u8 Bus::cartridge_ram_bank() const {
    return static_cast<u8>((smapper_control_ & 0x04) != 0 ? 1 : 0);
}

void Bus::load_cartridge_ram(std::span<const u8> ram) {
    cartridge_ram_.fill(0);
    const std::size_t len = std::min<std::size_t>(ram.size(), cartridge_ram_.size());
    std::copy_n(ram.begin(), static_cast<std::ptrdiff_t>(len), cartridge_ram_.begin());
    cartridge_ram_dirty_ = false;
    refresh_smapper();
}

u8 Bus::read(u16 address) const {
    if (address >= 0xC000) {
        return memory_[mirrored_ram_address(address)];
    }
    return memory_[address];
}

void Bus::write(u16 address, u8 value) {
    if (model_ == ConsoleModel::SMS && slot2_cartridge_ram_enabled() && address >= 0x8000 && address < 0xC000) {
        const std::size_t offset = static_cast<std::size_t>(cartridge_ram_bank()) * 0x4000 + (address - 0x8000);
        cartridge_ram_[offset] = value;
        cartridge_ram_dirty_ = true;
        memory_[address] = value;
        return;
    }

    if (model_ == ConsoleModel::SMS && address >= 0xFFFC) {
        if (address == 0xFFFC) {
            smapper_control_ = value;
        } else {
            smapper_slots_[address - 0xFFFD] = value;
        }
        refresh_smapper();
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
    if (model_ == ConsoleModel::SMS && port == 0x3E) {
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

void Bus::refresh_smapper() {
    std::fill(memory_.begin(), memory_.begin() + 0xC000, 0xFF);
    if (!rom_.empty()) {
        const std::size_t banks = (rom_.size() + 0x3FFF) / 0x4000;
        for (int slot = 0; slot < 3; ++slot) {
            const std::size_t bank = smapper_slots_[slot] % std::max<std::size_t>(banks, 1);
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

    if (model_ == ConsoleModel::SMS && slot2_cartridge_ram_enabled()) {
        const std::size_t offset = static_cast<std::size_t>(cartridge_ram_bank()) * 0x4000;
        std::copy_n(cartridge_ram_.begin() + static_cast<std::ptrdiff_t>(offset), 0x4000, memory_.begin() + 0x8000);
    }

    if (bios_enabled_ && !bios_.empty()) {
        const std::size_t len = std::min<std::size_t>(bios_.size(), 0xC000);
        std::copy_n(bios_.begin(), len, memory_.begin());
    }
}

bool Bus::slot2_cartridge_ram_enabled() const {
    return (smapper_control_ & 0x08) != 0;
}

bool Bus::has_copier_header(std::span<const u8> rom) {
    return rom.size() > 512 && (rom.size() % 0x4000) == 512;
}

u16 Bus::mirrored_ram_address(u16 address) {
    return static_cast<u16>(0xC000 + ((address - 0xC000) & 0x1FFF));
}

} // namespace sgrecomp
