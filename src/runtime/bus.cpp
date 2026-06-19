#include "sgrecomp/bus.h"

#include "sgrecomp/joypad.h"
#include "sgrecomp/psg.h"
#include "sgrecomp/vdp.h"
#include "sgrecomp/ym2413.h"

#include <algorithm>

namespace sgrecomp {

const char* cartridge_mapper_name(CartridgeMapper mapper) {
    switch (mapper) {
    case CartridgeMapper::Auto: return "auto";
    case CartridgeMapper::Plain: return "plain";
    case CartridgeMapper::SMapper: return "smapper";
    case CartridgeMapper::CMapper: return "cmapper";
    case CartridgeMapper::KMapper: return "kmapper";
    case CartridgeMapper::K8KMapper: return "k8k";
    }
    return "unknown";
}

Bus::Bus(ConsoleModel model, Vdp& vdp, Psg& psg, Ym2413& ym2413, Joypad& joypad)
    : model_(model), vdp_(vdp), psg_(psg), ym2413_(ym2413), joypad_(joypad) {}

void Bus::load_rom(std::span<const u8> rom) {
    rom_header_removed_ = has_copier_header(rom);
    if (rom_header_removed_) {
        rom = rom.subspan(512);
    }
    rom_.assign(rom.begin(), rom.end());
    memory_.fill(0);
    std::fill(memory_.begin(), memory_.begin() + 0xC000, 0xFF);
    memory_control_ = 0;
    smapper_control_ = 0;
    smapper_slots_[0] = 0;
    smapper_slots_[1] = 1;
    smapper_slots_[2] = 2;
    cmapper_slots_[0] = 0;
    cmapper_slots_[1] = 1;
    cmapper_slots_[2] = 2;
    kmapper_slot2_ = 2;
    for (u8 i = 0; i < 6; ++i) {
        k8k_slots_[i] = i;
    }
    mapper_ = requested_mapper_ == CartridgeMapper::Auto
        ? (rom.size() <= 0xC000 ? CartridgeMapper::Plain : CartridgeMapper::SMapper)
        : requested_mapper_;
    cartridge_ram_dirty_ = false;
    bios_enabled_ = !bios_.empty();
    refresh_cartridge_map();
}

void Bus::load_bios(std::span<const u8> bios) {
    bios_.assign(bios.begin(), bios.end());
    bios_enabled_ = !bios_.empty();
    refresh_cartridge_map();
}

void Bus::set_mapper(CartridgeMapper mapper) {
    requested_mapper_ = mapper;
    mapper_ = mapper == CartridgeMapper::Auto
        ? (rom_.size() <= 0xC000 ? CartridgeMapper::Plain : CartridgeMapper::SMapper)
        : mapper;
    refresh_cartridge_map();
}

BusMapperSnapshot Bus::mapper_snapshot() const {
    return {
        mapper_,
        requested_mapper_,
        memory_control_,
        bios_enabled_,
        cartridge_enabled(),
        work_ram_enabled(),
        smapper_control_,
        {smapper_slots_[0], smapper_slots_[1], smapper_slots_[2]},
        {cmapper_slots_[0], cmapper_slots_[1], cmapper_slots_[2]},
        kmapper_slot2_,
        {k8k_slots_[0], k8k_slots_[1], k8k_slots_[2], k8k_slots_[3], k8k_slots_[4], k8k_slots_[5]},
        cartridge_ram_enabled(),
        cartridge_ram_bank(),
    };
}

void Bus::set_bios_enabled(bool enabled) {
    bios_enabled_ = enabled && !bios_.empty();
    refresh_cartridge_map();
}

bool Bus::cartridge_enabled() const {
    return model_ != ConsoleModel::SMS || (memory_control_ & 0x40) == 0;
}

bool Bus::work_ram_enabled() const {
    return model_ != ConsoleModel::SMS || (memory_control_ & 0x10) == 0;
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
    refresh_cartridge_map();
}

u8 Bus::read(u16 address) const {
    if (address >= 0xC000) {
        if (!work_ram_enabled()) {
            return 0xFF;
        }
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

    if (model_ == ConsoleModel::SMS && cartridge_enabled() && requested_mapper_ == CartridgeMapper::Auto) {
        if (address == 0x0000 || address == 0x4000 || address == 0x8000) {
            select_auto_mapper(CartridgeMapper::CMapper);
        } else if (address == 0xA000) {
            select_auto_mapper(CartridgeMapper::KMapper);
        } else if (address >= 0x0001 && address <= 0x0003) {
            select_auto_mapper(CartridgeMapper::K8KMapper);
        }
    }

    if (model_ == ConsoleModel::SMS && mapper_ == CartridgeMapper::CMapper
        && (address == 0x0000 || address == 0x4000 || address == 0x8000)) {
        cmapper_slots_[address / 0x4000] = value;
        log_memory(BusMemoryAccessKind::Mapper, address, address, value);
        refresh_cartridge_map();
        return;
    }

    if (model_ == ConsoleModel::SMS && mapper_ == CartridgeMapper::KMapper && address == 0xA000) {
        kmapper_slot2_ = value;
        log_memory(BusMemoryAccessKind::Mapper, address, address, value);
        refresh_cartridge_map();
        return;
    }

    if (model_ == ConsoleModel::SMS && mapper_ == CartridgeMapper::K8KMapper && address <= 0x0003) {
        k8k_slots_[address] = value;
        log_memory(BusMemoryAccessKind::Mapper, address, address, value);
        refresh_cartridge_map();
        return;
    }

    if (model_ == ConsoleModel::SMS && mapper_ == CartridgeMapper::SMapper && address >= 0xFFFC) {
        if (address == 0xFFFC) {
            smapper_control_ = value;
        } else {
            smapper_slots_[address - 0xFFFD] = value;
        }
        log_memory(BusMemoryAccessKind::Mapper, address, address, value);
        refresh_cartridge_map();
    }

    if (address >= 0xC000) {
        if (!work_ram_enabled()) {
            return;
        }
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
    if (is_v_counter_port(port)) {
        const u8 value = vdp_.read_v_counter();
        log_io(false, port, value);
        return value;
    }
    if (is_h_counter_port(port)) {
        const u8 value = vdp_.read_h_counter();
        log_io(false, port, value);
        return value;
    }
    if (is_vdp_data_port(port)) {
        const u8 value = vdp_.read_data();
        log_io(false, port, value);
        return value;
    }
    if (is_vdp_control_port(port)) {
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
        set_memory_control(value);
        return;
    }
    if (is_vdp_data_port(port)) {
        vdp_.write_data(value);
        return;
    }
    if (is_vdp_control_port(port)) {
        vdp_.write_control(value);
        return;
    }
    if (is_psg_write_port(port)) {
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
        mapper_,
        requested_mapper_,
        {cmapper_slots_[0], cmapper_slots_[1], cmapper_slots_[2]},
        kmapper_slot2_,
        {k8k_slots_[0], k8k_slots_[1], k8k_slots_[2], k8k_slots_[3], k8k_slots_[4], k8k_slots_[5]},
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
    mapper_ = state.mapper;
    requested_mapper_ = state.requested_mapper;
    cmapper_slots_[0] = state.cmapper_slots[0];
    cmapper_slots_[1] = state.cmapper_slots[1];
    cmapper_slots_[2] = state.cmapper_slots[2];
    kmapper_slot2_ = state.kmapper_slot2;
    for (std::size_t i = 0; i < 6; ++i) {
        k8k_slots_[i] = state.k8k_slots[i];
    }
    refresh_cartridge_map();
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

void Bus::refresh_cartridge_map() {
    std::fill(memory_.begin(), memory_.begin() + 0xC000, 0xFF);

    if (cartridge_enabled()) {
        switch (mapper_) {
        case CartridgeMapper::Plain:
            refresh_linear_rom();
            break;
        case CartridgeMapper::CMapper:
            refresh_cmapper();
            break;
        case CartridgeMapper::KMapper:
            refresh_kmapper();
            break;
        case CartridgeMapper::K8KMapper:
            refresh_k8k_mapper();
            break;
        case CartridgeMapper::Auto:
        case CartridgeMapper::SMapper:
            refresh_smapper();
            break;
        }
    }

    if (bios_enabled_ && !bios_.empty()) {
        const std::size_t len = std::min<std::size_t>(bios_.size(), 0xC000);
        std::copy_n(bios_.begin(), len, memory_.begin());
    }
}

void Bus::refresh_linear_rom() {
    if (rom_.empty()) {
        return;
    }
    const std::size_t len = std::min<std::size_t>(rom_.size(), 0xC000);
    std::copy_n(rom_.begin(), static_cast<std::ptrdiff_t>(len), memory_.begin());
}

void Bus::refresh_smapper() {
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
}

void Bus::refresh_cmapper() {
    for (int slot = 0; slot < 3; ++slot) {
        copy_rom_bank(cmapper_slots_[slot], static_cast<u16>(slot * 0x4000), 0x4000, 0x4000);
    }
}

void Bus::refresh_kmapper() {
    copy_rom_bank(0, 0x0000, 0x4000, 0x4000);
    copy_rom_bank(1, 0x4000, 0x4000, 0x4000);
    copy_rom_bank(kmapper_slot2_, 0x8000, 0x4000, 0x4000);
}

void Bus::refresh_k8k_mapper() {
    for (int slot = 0; slot < 6; ++slot) {
        copy_rom_bank(k8k_slots_[slot], static_cast<u16>(slot * 0x2000), 0x2000, 0x2000);
    }
}

void Bus::copy_rom_bank(std::size_t bank, u16 dst, std::size_t size, std::size_t bank_size, std::size_t bank_offset) {
    if (rom_.empty()) {
        return;
    }
    const std::size_t banks = (rom_.size() + bank_size - 1) / bank_size;
    const std::size_t normalized_bank = bank % std::max<std::size_t>(banks, 1);
    const std::size_t src = normalized_bank * bank_size + bank_offset;
    if (src >= rom_.size()) {
        return;
    }
    const std::size_t len = std::min<std::size_t>(size, rom_.size() - src);
    std::copy_n(rom_.begin() + static_cast<std::ptrdiff_t>(src), static_cast<std::ptrdiff_t>(len), memory_.begin() + dst);
}

void Bus::select_auto_mapper(CartridgeMapper mapper) {
    if (requested_mapper_ == CartridgeMapper::Auto && mapper_ != mapper) {
        mapper_ = mapper;
    }
}

void Bus::set_memory_control(u8 value) {
    memory_control_ = value;
    bios_enabled_ = (value & 0x08) == 0 && !bios_.empty();
    refresh_cartridge_map();
}

bool Bus::slot2_cartridge_ram_enabled() const {
    const bool sega_mapper = mapper_ == CartridgeMapper::SMapper || mapper_ == CartridgeMapper::Auto;
    return model_ == ConsoleModel::SMS
        && sega_mapper
        && cartridge_enabled()
        && (smapper_control_ & 0x08) != 0;
}

bool Bus::has_copier_header(std::span<const u8> rom) {
    return rom.size() > 512 && (rom.size() % 0x4000) == 512;
}

u16 Bus::mirrored_ram_address(u16 address) {
    return static_cast<u16>(0xC000 + ((address - 0xC000) & 0x1FFF));
}

bool Bus::is_vdp_data_port(u8 port) {
    return (port & 0xC0) == 0x80 && (port & 0x01) == 0;
}

bool Bus::is_vdp_control_port(u8 port) {
    return (port & 0xC0) == 0x80 && (port & 0x01) != 0;
}

bool Bus::is_v_counter_port(u8 port) {
    return (port & 0xC0) == 0x40 && (port & 0x01) == 0;
}

bool Bus::is_h_counter_port(u8 port) {
    return (port & 0xC0) == 0x40 && (port & 0x01) != 0;
}

bool Bus::is_psg_write_port(u8 port) {
    return (port & 0xC0) == 0x40;
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
