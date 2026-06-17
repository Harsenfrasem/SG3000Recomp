#include "sgrecomp/save_state.h"

#include <cstring>
#include <stdexcept>

namespace sgrecomp {
namespace {

constexpr u32 kMagic = 0x53534753; // SGSS
constexpr u16 kVersion = 1;

class Writer {
public:
    void u8v(u8 value) { bytes_.push_back(value); }

    void boolv(bool value) { u8v(value ? 1 : 0); }

    void u16v(u16 value) {
        u8v(static_cast<u8>(value & 0xFF));
        u8v(static_cast<u8>((value >> 8) & 0xFF));
    }

    void u32v(u32 value) {
        u16v(static_cast<u16>(value & 0xFFFF));
        u16v(static_cast<u16>((value >> 16) & 0xFFFF));
    }

    void u64v(u64 value) {
        u32v(static_cast<u32>(value & 0xFFFFFFFFULL));
        u32v(static_cast<u32>((value >> 32) & 0xFFFFFFFFULL));
    }

    void i32v(int value) { u32v(static_cast<u32>(value)); }

    void doublev(double value) {
        u64 raw = 0;
        std::memcpy(&raw, &value, sizeof(raw));
        u64v(raw);
    }

    template <typename T, std::size_t N>
    void array_bytes(const std::array<T, N>& values) {
        for (const auto& value : values) {
            if constexpr (sizeof(T) == 1) {
                u8v(static_cast<u8>(value));
            } else if constexpr (sizeof(T) == 2) {
                u16v(static_cast<u16>(value));
            } else if constexpr (sizeof(T) == 4) {
                u32v(static_cast<u32>(value));
            } else if constexpr (sizeof(T) == 8) {
                doublev(static_cast<double>(value));
            }
        }
    }

    std::vector<u8> finish() { return std::move(bytes_); }

private:
    std::vector<u8> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const u8> bytes) : bytes_(bytes) {}

    u8 u8v() {
        require(1);
        return bytes_[offset_++];
    }

    bool boolv() { return u8v() != 0; }

    u16 u16v() {
        const u16 lo = u8v();
        const u16 hi = u8v();
        return static_cast<u16>(lo | (hi << 8));
    }

    u32 u32v() {
        const u32 lo = u16v();
        const u32 hi = u16v();
        return lo | (hi << 16);
    }

    u64 u64v() {
        const u64 lo = u32v();
        const u64 hi = u32v();
        return lo | (hi << 32);
    }

    int i32v() { return static_cast<int>(u32v()); }

    double doublev() {
        const u64 raw = u64v();
        double value = 0.0;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    template <typename T, std::size_t N>
    void array_bytes(std::array<T, N>& values) {
        for (auto& value : values) {
            if constexpr (sizeof(T) == 1) {
                value = static_cast<T>(u8v());
            } else if constexpr (sizeof(T) == 2) {
                value = static_cast<T>(u16v());
            } else if constexpr (sizeof(T) == 4) {
                value = static_cast<T>(u32v());
            } else if constexpr (sizeof(T) == 8) {
                value = static_cast<T>(doublev());
            }
        }
    }

    void finish() const {
        if (offset_ != bytes_.size()) {
            throw std::runtime_error("save state has trailing bytes");
        }
    }

private:
    std::span<const u8> bytes_;
    std::size_t offset_ = 0;

    void require(std::size_t count) const {
        if (offset_ + count > bytes_.size()) {
            throw std::runtime_error("truncated save state");
        }
    }
};

void write_cpu(Writer& out, const Z80State& cpu) {
    out.u8v(cpu.a); out.u8v(cpu.f); out.u8v(cpu.b); out.u8v(cpu.c);
    out.u8v(cpu.d); out.u8v(cpu.e); out.u8v(cpu.h); out.u8v(cpu.l);
    out.u8v(cpu.a_alt); out.u8v(cpu.f_alt); out.u8v(cpu.b_alt); out.u8v(cpu.c_alt);
    out.u8v(cpu.d_alt); out.u8v(cpu.e_alt); out.u8v(cpu.h_alt); out.u8v(cpu.l_alt);
    out.u8v(cpu.ixh); out.u8v(cpu.ixl); out.u8v(cpu.iyh); out.u8v(cpu.iyl);
    out.u16v(cpu.sp); out.u16v(cpu.pc); out.u16v(cpu.last_pc);
    out.u8v(cpu.i); out.u8v(cpu.r);
    out.boolv(cpu.iff1); out.boolv(cpu.iff2); out.boolv(cpu.ei_pending);
    out.u8v(cpu.interrupt_mode); out.boolv(cpu.halted); out.u64v(cpu.cycles);
}

Z80State read_cpu(Reader& in) {
    Z80State cpu;
    cpu.a = in.u8v(); cpu.f = in.u8v(); cpu.b = in.u8v(); cpu.c = in.u8v();
    cpu.d = in.u8v(); cpu.e = in.u8v(); cpu.h = in.u8v(); cpu.l = in.u8v();
    cpu.a_alt = in.u8v(); cpu.f_alt = in.u8v(); cpu.b_alt = in.u8v(); cpu.c_alt = in.u8v();
    cpu.d_alt = in.u8v(); cpu.e_alt = in.u8v(); cpu.h_alt = in.u8v(); cpu.l_alt = in.u8v();
    cpu.ixh = in.u8v(); cpu.ixl = in.u8v(); cpu.iyh = in.u8v(); cpu.iyl = in.u8v();
    cpu.sp = in.u16v(); cpu.pc = in.u16v(); cpu.last_pc = in.u16v();
    cpu.i = in.u8v(); cpu.r = in.u8v();
    cpu.iff1 = in.boolv(); cpu.iff2 = in.boolv(); cpu.ei_pending = in.boolv();
    cpu.interrupt_mode = in.u8v(); cpu.halted = in.boolv(); cpu.cycles = in.u64v();
    return cpu;
}

void write_state(Writer& out, const ConsoleState& state) {
    out.u32v(kMagic);
    out.u16v(kVersion);
    write_cpu(out, state.cpu);
    out.array_bytes(state.bus.memory);
    out.array_bytes(state.bus.cartridge_ram);
    out.boolv(state.bus.rom_header_removed);
    out.boolv(state.bus.bios_enabled);
    out.boolv(state.bus.cartridge_ram_dirty);
    out.u8v(state.bus.memory_control);
    out.u8v(state.bus.smapper_control);
    out.array_bytes(state.bus.smapper_slots);
    out.array_bytes(state.vdp.vram);
    out.array_bytes(state.vdp.cram);
    out.array_bytes(state.vdp.registers);
    out.array_bytes(state.vdp.framebuffer);
    out.array_bytes(state.vdp.scanline_bg_priority);
    out.u16v(state.vdp.address);
    out.u8v(state.vdp.latch);
    out.u8v(state.vdp.code);
    out.boolv(state.vdp.pending_control);
    out.u8v(state.vdp.status);
    out.i32v(state.vdp.scanline_cycles);
    out.i32v(state.vdp.scanline);
    out.i32v(state.vdp.line_counter);
    out.boolv(state.vdp.first_line);
    out.array_bytes(state.psg.tone);
    out.array_bytes(state.psg.volume);
    out.array_bytes(state.psg.counters);
    out.array_bytes(state.psg.output);
    out.u16v(state.psg.noise_lfsr);
    out.u8v(state.psg.latched_channel);
    out.boolv(state.psg.latched_volume);
    out.boolv(state.ym2413.present);
    out.u8v(state.ym2413.selected_register);
    out.u8v(state.ym2413.audio_control);
    out.array_bytes(state.ym2413.registers);
    out.array_bytes(state.ym2413.phase);
    out.u8v(state.joypad_player1);
    out.u8v(state.joypad_player2);
}

ConsoleState read_state(Reader& in) {
    if (in.u32v() != kMagic) {
        throw std::runtime_error("not an SG3000Recomp save state");
    }
    if (in.u16v() != kVersion) {
        throw std::runtime_error("unsupported save state version");
    }

    ConsoleState state;
    state.cpu = read_cpu(in);
    in.array_bytes(state.bus.memory);
    in.array_bytes(state.bus.cartridge_ram);
    state.bus.rom_header_removed = in.boolv();
    state.bus.bios_enabled = in.boolv();
    state.bus.cartridge_ram_dirty = in.boolv();
    state.bus.memory_control = in.u8v();
    state.bus.smapper_control = in.u8v();
    in.array_bytes(state.bus.smapper_slots);
    in.array_bytes(state.vdp.vram);
    in.array_bytes(state.vdp.cram);
    in.array_bytes(state.vdp.registers);
    in.array_bytes(state.vdp.framebuffer);
    in.array_bytes(state.vdp.scanline_bg_priority);
    state.vdp.address = in.u16v();
    state.vdp.latch = in.u8v();
    state.vdp.code = in.u8v();
    state.vdp.pending_control = in.boolv();
    state.vdp.status = in.u8v();
    state.vdp.scanline_cycles = in.i32v();
    state.vdp.scanline = in.i32v();
    state.vdp.line_counter = in.i32v();
    state.vdp.first_line = in.boolv();
    in.array_bytes(state.psg.tone);
    in.array_bytes(state.psg.volume);
    in.array_bytes(state.psg.counters);
    in.array_bytes(state.psg.output);
    state.psg.noise_lfsr = in.u16v();
    state.psg.latched_channel = in.u8v();
    state.psg.latched_volume = in.boolv();
    state.ym2413.present = in.boolv();
    state.ym2413.selected_register = in.u8v();
    state.ym2413.audio_control = in.u8v();
    in.array_bytes(state.ym2413.registers);
    in.array_bytes(state.ym2413.phase);
    state.joypad_player1 = in.u8v();
    state.joypad_player2 = in.u8v();
    in.finish();
    return state;
}

} // namespace

std::vector<u8> serialize_console_state(const ConsoleState& state) {
    Writer writer;
    write_state(writer, state);
    return writer.finish();
}

ConsoleState deserialize_console_state(std::span<const u8> bytes) {
    Reader reader(bytes);
    return read_state(reader);
}

std::vector<u8> save_console_state(const Console& console) {
    return serialize_console_state(console.save_state());
}

void load_console_state(Console& console, std::span<const u8> bytes) {
    console.load_state(deserialize_console_state(bytes));
}

} // namespace sgrecomp
