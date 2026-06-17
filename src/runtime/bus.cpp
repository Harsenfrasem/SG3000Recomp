#include "sgrecomp/bus.h"

#include "sgrecomp/joypad.h"
#include "sgrecomp/psg.h"
#include "sgrecomp/vdp.h"
#include "sgrecomp/ym2413.h"

#include <algorithm>

namespace sgrecomp {

Bus::Bus(ConsoleModel model, Vdp& vdp, Psg& psg, Ym2413& ym2413, Joypad& joypad)
    : model_(model), vdp_(vdp), psg_(psg), ym2413_(ym2413), joypad_(joypad) {}

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
        log_memory(BusMemoryAccessKind::CartridgeRam, address, static_cast<u32>(offset), value);
        return;
    }

    if (model_ == ConsoleModel::SMS && address >= 0xFFFC) {
        if (address == 0xFFFC) {
            smapper_control_ = value;
        } else {
            smapper_slots_[address - 0xFFFD] = value;
        }
        log_memory(BusMemoryAccessKind::Mapper, address, address, value);
        refresh_smapper();
    }

    if (address >= 0xC000) {
        const u16 ram = mirrored_ram_address(address);
        memory_[ram] = value;
        memory_[static_cast<u16>(ram + 0x2000)] = value;
        log_memory(BusMemoryAccessKind::Ram, address, ram, value);
        return;
    }
}

u8 Bus::input(u8 port) {
    if (model_ == ConsoleModel::SMS && port == 0xF2) {
        const u8 value = ym2413_.read_audio_control();
        log_io(false, port, value);
        return value;
    }
    if (port == 0x7E) {
        const u8 value = vdp_.read_v_counter();
        log_io(false, port, value);
        return value;
    }
    if (port == 0x7F) {
        const u8 value = vdp_.read_h_counter();
        log_io(false, port, value);
        return value;
    }
    if (port == 0xBE || port == 0x98) {
        const u8 value = vdp_.read_data();
        log_io(false, port, value);
        return value;
    }
    if (port == 0xBF || port == 0x99) {
        const u8 value = vdp_.read_status();
        log_io(false, port, value);
        return value;
    }
    if (port == 0xDC || port == 0xC0) {
        const u8 value = joypad_.read_port_a();
        log_io(false, port, value);
        return value;
    }
    if (port == 0xDD || port == 0xC1) {
        const u8 value = joypad_.read_port_b();
        log_io(false, port, value);
        return value;
    }
    log_io(false, port, 0xFF);
    return 0xFF;
}

void Bus::output(u8 port, u8 value) {
    log_io(true, port, value);
    if (model_ == ConsoleModel::SMS && port == 0xF0) {
        ym2413_.write_address(value);
        return;
    }
    if (model_ == ConsoleModel::SMS && port == 0xF1) {
        ym2413_.write_data(value);
        return;
    }
    if (model_ == ConsoleModel::SMS && port == 0xF2) {
        ym2413_.write_audio_control(value);
        return;
    }
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

void Bus::set_fm_present(bool present) {
    ym2413_.set_present(model_ == ConsoleModel::SMS && present);
}

bool Bus::fm_present() const {
    return ym2413_.present();
}

BusState Bus::save_state() const {
    return {
        memory_,
        cartridge_ram_,
        rom_header_removed_,
        bios_enabled_,
        cartridge_ram_dirty_,
        memory_control_,
        smapper_control_,
        {smapper_slots_[0], smapper_slots_[1], smapper_slots_[2]},
    };
}

void Bus::load_state(const BusState& state) {
    memory_ = state.memory;
    cartridge_ram_ = state.cartridge_ram;
    rom_header_removed_ = state.rom_header_removed;
    bios_enabled_ = state.bios_enabled && !bios_.empty();
    cartridge_ram_dirty_ = state.cartridge_ram_dirty;
    memory_control_ = state.memory_control;
    smapper_control_ = state.smapper_control;
    smapper_slots_[0] = state.smapper_slots[0];
    smapper_slots_[1] = state.smapper_slots[1];
    smapper_slots_[2] = state.smapper_slots[2];
}

void Bus::set_io_logging_enabled(bool enabled) {
    io_logging_enabled_ = enabled;
    if (!enabled) {
        logged_io_.clear();
    }
}

void Bus::set_memory_logging_enabled(bool enabled) {
    memory_logging_enabled_ = enabled;
    if (!enabled) {
        logged_memory_.clear();
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

void Bus::log_io(bool write, u8 port, u8 value) {
    if (io_logging_enabled_) {
        logged_io_.push_back({current_cycle_, write, port, value});
    }
}

void Bus::log_memory(BusMemoryAccessKind kind, u16 address, u32 physical, u8 value) {
    if (memory_logging_enabled_) {
        logged_memory_.push_back({current_cycle_, kind, address, physical, value});
    }
}

} // namespace sgrecomp
