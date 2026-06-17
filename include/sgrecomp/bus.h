#pragma once

#include "sgrecomp/types.h"

#include <array>
#include <span>
#include <vector>

namespace sgrecomp {

class Vdp;
class Psg;
class Ym2413;
class Joypad;

struct BusIoAccess {
    u64 cycle = 0;
    bool write = false;
    u8 port = 0;
    u8 value = 0;
};

enum class BusMemoryAccessKind {
    Ram,
    CartridgeRam,
    Mapper,
};

enum class CartridgeMapper {
    Auto,
    Plain,
    SMapper,
    CMapper,
    KMapper,
    K8KMapper,
};

struct BusMemoryAccess {
    u64 cycle = 0;
    BusMemoryAccessKind kind = BusMemoryAccessKind::Ram;
    u16 address = 0;
    u32 physical = 0;
    u8 value = 0;
};

struct BusState {
    std::array<u8, 0x10000> memory{};
    std::array<u8, 0x8000> cartridge_ram{};
    bool rom_header_removed = false;
    bool bios_enabled = false;
    bool cartridge_ram_dirty = false;
    u8 memory_control = 0;
    u8 smapper_control = 0;
    std::array<u8, 3> smapper_slots{{0, 1, 2}};
    CartridgeMapper mapper = CartridgeMapper::Auto;
    CartridgeMapper requested_mapper = CartridgeMapper::Auto;
    std::array<u8, 3> cmapper_slots{{0, 1, 2}};
    u8 kmapper_slot2 = 2;
    std::array<u8, 6> k8k_slots{{0, 1, 2, 3, 4, 5}};
};

enum class ConsoleModel {
    SMS,
    SG3000,
};

class Bus {
public:
    Bus(ConsoleModel model, Vdp& vdp, Psg& psg, Ym2413& ym2413, Joypad& joypad);

    void load_rom(std::span<const u8> rom);
    void load_bios(std::span<const u8> bios);
    void set_mapper(CartridgeMapper mapper);
    CartridgeMapper mapper() const { return mapper_; }
    CartridgeMapper requested_mapper() const { return requested_mapper_; }
    bool rom_header_removed() const { return rom_header_removed_; }
    void set_bios_enabled(bool enabled);
    bool bios_enabled() const { return bios_enabled_; }
    bool cartridge_ram_enabled() const;
    u8 cartridge_ram_bank() const;
    void load_cartridge_ram(std::span<const u8> ram);
    bool cartridge_ram_dirty() const { return cartridge_ram_dirty_; }
    u8 read(u16 address) const;
    void write(u16 address, u8 value);
    u8 input(u8 port);
    void output(u8 port, u8 value);
    void set_fm_present(bool present);
    bool fm_present() const;
    void set_cycle(u64 cycle) { current_cycle_ = cycle; }
    void set_io_logging_enabled(bool enabled);
    const std::vector<BusIoAccess>& logged_io() const { return logged_io_; }
    void set_memory_logging_enabled(bool enabled);
    const std::vector<BusMemoryAccess>& logged_memory() const { return logged_memory_; }
    BusState save_state() const;
    void load_state(const BusState& state);

    const std::array<u8, 0x10000>& debug_memory() const { return memory_; }
    const std::array<u8, 0x8000>& debug_cartridge_ram() const { return cartridge_ram_; }

private:
    ConsoleModel model_;
    Vdp& vdp_;
    Psg& psg_;
    Ym2413& ym2413_;
    Joypad& joypad_;
    std::array<u8, 0x10000> memory_{};
    std::array<u8, 0x8000> cartridge_ram_{};
    std::vector<u8> rom_;
    std::vector<u8> bios_;
    bool rom_header_removed_ = false;
    bool bios_enabled_ = false;
    bool cartridge_ram_dirty_ = false;
    u8 memory_control_ = 0;
    u8 smapper_control_ = 0;
    u8 smapper_slots_[3] = {0, 1, 2};
    CartridgeMapper requested_mapper_ = CartridgeMapper::Auto;
    CartridgeMapper mapper_ = CartridgeMapper::Auto;
    u8 cmapper_slots_[3] = {0, 1, 2};
    u8 kmapper_slot2_ = 2;
    u8 k8k_slots_[6] = {0, 1, 2, 3, 4, 5};
    u64 current_cycle_ = 0;
    bool io_logging_enabled_ = false;
    std::vector<BusIoAccess> logged_io_;
    bool memory_logging_enabled_ = false;
    std::vector<BusMemoryAccess> logged_memory_;

    void refresh_cartridge_map();
    void refresh_linear_rom();
    void refresh_smapper();
    void refresh_cmapper();
    void refresh_kmapper();
    void refresh_k8k_mapper();
    void copy_rom_bank(std::size_t bank, u16 dst, std::size_t size, std::size_t bank_size, std::size_t bank_offset = 0);
    void select_auto_mapper(CartridgeMapper mapper);
    void log_io(bool write, u8 port, u8 value);
    void log_memory(BusMemoryAccessKind kind, u16 address, u32 physical, u8 value);
    bool slot2_cartridge_ram_enabled() const;
    static bool has_copier_header(std::span<const u8> rom);
    static u16 mirrored_ram_address(u16 address);
    static bool is_vdp_data_port(u8 port);
    static bool is_vdp_control_port(u8 port);
    static bool is_v_counter_port(u8 port);
    static bool is_h_counter_port(u8 port);
    static bool is_psg_write_port(u8 port);
};

} // namespace sgrecomp
