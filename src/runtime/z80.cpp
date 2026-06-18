#include "sgrecomp/z80.h"

#include "sgrecomp/bus.h"

#include <cstdio>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <sstream>
#include <utility>

namespace sgrecomp {
namespace {

constexpr u8 flag_s = 0x80;
constexpr u8 flag_z = 0x40;
constexpr u8 flag_y = 0x20;
constexpr u8 flag_h = 0x10;
constexpr u8 flag_x = 0x08;
constexpr u8 flag_pv = 0x04;
constexpr u8 flag_n = 0x02;
constexpr u8 flag_c = 0x01;

u8 parity(u8 value) {
    value ^= static_cast<u8>(value >> 4);
    value &= 0x0F;
    return static_cast<u8>((0x6996 >> value) & 1) ? 0 : flag_pv;
}

void set_szp(Z80State& cpu, u8 value) {
    cpu.f = static_cast<u8>((value & (flag_s | flag_y | flag_x)) | (value == 0 ? flag_z : 0) | parity(value));
}

u8 add8(Z80State& cpu, u8 lhs, u8 rhs) {
    const u16 sum = static_cast<u16>(lhs + rhs);
    const u8 result = static_cast<u8>(sum);
    cpu.f = static_cast<u8>((result & (flag_s | flag_y | flag_x)) |
        (result == 0 ? flag_z : 0) |
        (((lhs ^ rhs ^ result) & 0x10) ? flag_h : 0) |
        ((~(lhs ^ rhs) & (lhs ^ result) & 0x80) ? flag_pv : 0) |
        (sum > 0xFF ? flag_c : 0));
    return result;
}

u8 sub8(Z80State& cpu, u8 lhs, u8 rhs) {
    const u16 diff = static_cast<u16>(lhs - rhs);
    const u8 result = static_cast<u8>(diff);
    cpu.f = static_cast<u8>(flag_n |
        (result & (flag_s | flag_y | flag_x)) |
        (result == 0 ? flag_z : 0) |
        (((lhs ^ rhs ^ result) & 0x10) ? flag_h : 0) |
        (((lhs ^ rhs) & (lhs ^ result) & 0x80) ? flag_pv : 0) |
        (lhs < rhs ? flag_c : 0));
    return result;
}

void daa(Z80State& cpu) {
    const u8 old_a = cpu.a;
    u8 correction = 0;
    bool carry = (cpu.f & flag_c) != 0;

    if ((cpu.f & flag_h) != 0 || ((cpu.f & flag_n) == 0 && (cpu.a & 0x0F) > 9)) {
        correction = static_cast<u8>(correction | 0x06);
    }
    if (carry || ((cpu.f & flag_n) == 0 && cpu.a > 0x99)) {
        correction = static_cast<u8>(correction | 0x60);
        carry = true;
    }

    cpu.a = (cpu.f & flag_n) != 0
        ? static_cast<u8>(cpu.a - correction)
        : static_cast<u8>(cpu.a + correction);
    cpu.f = static_cast<u8>((cpu.f & flag_n) |
        (cpu.a & (flag_s | flag_y | flag_x)) |
        (cpu.a == 0 ? flag_z : 0) |
        (((old_a ^ cpu.a) & 0x10) ? flag_h : 0) |
        parity(cpu.a) |
        (carry ? flag_c : 0));
}

u8 adc8(Z80State& cpu, u8 lhs, u8 rhs) {
    const u8 carry = static_cast<u8>(cpu.f & flag_c);
    const u16 sum = static_cast<u16>(lhs + rhs + carry);
    const u8 result = static_cast<u8>(sum);
    cpu.f = static_cast<u8>((result & (flag_s | flag_y | flag_x)) |
        (result == 0 ? flag_z : 0) |
        (((lhs ^ rhs ^ result) & 0x10) ? flag_h : 0) |
        ((~(lhs ^ rhs) & (lhs ^ result) & 0x80) ? flag_pv : 0) |
        (sum > 0xFF ? flag_c : 0));
    return result;
}

u8 sbc8(Z80State& cpu, u8 lhs, u8 rhs) {
    const u8 carry = static_cast<u8>(cpu.f & flag_c);
    const u16 diff = static_cast<u16>(lhs - rhs - carry);
    const u8 result = static_cast<u8>(diff);
    cpu.f = static_cast<u8>(flag_n |
        (result & (flag_s | flag_y | flag_x)) |
        (result == 0 ? flag_z : 0) |
        (((lhs ^ rhs ^ result) & 0x10) ? flag_h : 0) |
        (((lhs ^ rhs) & (lhs ^ result) & 0x80) ? flag_pv : 0) |
        (static_cast<u16>(rhs + carry) > lhs ? flag_c : 0));
    return result;
}

u8 and8(Z80State& cpu, u8 lhs, u8 rhs) {
    const u8 result = static_cast<u8>(lhs & rhs);
    cpu.f = static_cast<u8>(flag_h | (result & (flag_s | flag_y | flag_x)) | (result == 0 ? flag_z : 0) | parity(result));
    return result;
}

u8 xor8(Z80State& cpu, u8 lhs, u8 rhs) {
    const u8 result = static_cast<u8>(lhs ^ rhs);
    set_szp(cpu, result);
    return result;
}

u8 or8(Z80State& cpu, u8 lhs, u8 rhs) {
    const u8 result = static_cast<u8>(lhs | rhs);
    set_szp(cpu, result);
    return result;
}

u8 inc8(Z80State& cpu, u8 value) {
    const u8 old_carry = static_cast<u8>(cpu.f & flag_c);
    const u8 result = static_cast<u8>(value + 1);
    cpu.f = static_cast<u8>(old_carry |
        (result & (flag_s | flag_y | flag_x)) |
        (result == 0 ? flag_z : 0) |
        (((value ^ result) & 0x10) ? flag_h : 0) |
        (value == 0x7F ? flag_pv : 0));
    return result;
}

u8 dec8(Z80State& cpu, u8 value) {
    const u8 old_carry = static_cast<u8>(cpu.f & flag_c);
    const u8 result = static_cast<u8>(value - 1);
    cpu.f = static_cast<u8>(old_carry | flag_n |
        (result & (flag_s | flag_y | flag_x)) |
        (result == 0 ? flag_z : 0) |
        (((value ^ result) & 0x10) ? flag_h : 0) |
        (value == 0x80 ? flag_pv : 0));
    return result;
}

u8 fetch8(Z80State& cpu, Bus& bus) {
    const u8 value = bus.read(cpu.pc);
    cpu.pc = static_cast<u16>(cpu.pc + 1);
    return value;
}

void increment_refresh(Z80State& cpu) {
    cpu.r = static_cast<u8>((cpu.r & 0x80) | ((cpu.r + 1) & 0x7F));
}

void decrement_refresh(Z80State& cpu) {
    cpu.r = static_cast<u8>((cpu.r & 0x80) | ((cpu.r - 1) & 0x7F));
}

u16 fetch16(Z80State& cpu, Bus& bus) {
    const u8 lo = fetch8(cpu, bus);
    const u8 hi = fetch8(cpu, bus);
    return make_u16(lo, hi);
}

u16 read16(Bus& bus, u16 address) {
    const u8 lo = bus.read(address);
    const u8 hi = bus.read(static_cast<u16>(address + 1));
    return make_u16(lo, hi);
}

void write16(Bus& bus, u16 address, u16 value) {
    bus.write(address, static_cast<u8>(value));
    bus.write(static_cast<u16>(address + 1), static_cast<u8>(value >> 8));
}

void push16(Z80State& cpu, Bus& bus, u16 value) {
    cpu.sp = static_cast<u16>(cpu.sp - 1);
    bus.write(cpu.sp, static_cast<u8>(value >> 8));
    cpu.sp = static_cast<u16>(cpu.sp - 1);
    bus.write(cpu.sp, static_cast<u8>(value));
}

u16 pop16(Z80State& cpu, Bus& bus) {
    const u8 lo = bus.read(cpu.sp);
    cpu.sp = static_cast<u16>(cpu.sp + 1);
    const u8 hi = bus.read(cpu.sp);
    cpu.sp = static_cast<u16>(cpu.sp + 1);
    return make_u16(lo, hi);
}

void relative_jump(Z80State& cpu, s8 displacement) {
    cpu.pc = static_cast<u16>(cpu.pc + displacement);
}

u16 read_rp(const Z80State& cpu, u8 index) {
    switch (index & 0x03) {
    case 0: return cpu.bc();
    case 1: return cpu.de();
    case 2: return cpu.hl();
    default: return cpu.sp;
    }
}

void write_rp(Z80State& cpu, u8 index, u16 value) {
    switch (index & 0x03) {
    case 0: cpu.set_bc(value); break;
    case 1: cpu.set_de(value); break;
    case 2: cpu.set_hl(value); break;
    default: cpu.sp = value; break;
    }
}

u16 read_qq(const Z80State& cpu, u8 index) {
    switch (index & 0x03) {
    case 0: return cpu.bc();
    case 1: return cpu.de();
    case 2: return cpu.hl();
    default: return cpu.af();
    }
}

u16 read_ix(const Z80State& cpu) {
    return make_u16(cpu.ixl, cpu.ixh);
}

u16 read_iy(const Z80State& cpu) {
    return make_u16(cpu.iyl, cpu.iyh);
}

void write_ix(Z80State& cpu, u16 value) {
    cpu.ixl = static_cast<u8>(value);
    cpu.ixh = static_cast<u8>(value >> 8);
}

void write_iy(Z80State& cpu, u16 value) {
    cpu.iyl = static_cast<u8>(value);
    cpu.iyh = static_cast<u8>(value >> 8);
}

u16 read_index(const Z80State& cpu, bool iy) {
    return iy ? read_iy(cpu) : read_ix(cpu);
}

void write_index(Z80State& cpu, bool iy, u16 value) {
    if (iy) {
        write_iy(cpu, value);
    } else {
        write_ix(cpu, value);
    }
}

u8 read_index_reg(const Z80State& cpu, bool iy, u8 index) {
    switch (index & 0x07) {
    case 0: return cpu.b;
    case 1: return cpu.c;
    case 2: return cpu.d;
    case 3: return cpu.e;
    case 4: return iy ? cpu.iyh : cpu.ixh;
    case 5: return iy ? cpu.iyl : cpu.ixl;
    default: return cpu.a;
    }
}

void write_index_reg(Z80State& cpu, bool iy, u8 index, u8 value) {
    switch (index & 0x07) {
    case 0: cpu.b = value; break;
    case 1: cpu.c = value; break;
    case 2: cpu.d = value; break;
    case 3: cpu.e = value; break;
    case 4:
        if (iy) {
            cpu.iyh = value;
        } else {
            cpu.ixh = value;
        }
        break;
    case 5:
        if (iy) {
            cpu.iyl = value;
        } else {
            cpu.ixl = value;
        }
        break;
    default: cpu.a = value; break;
    }
}

void write_qq(Z80State& cpu, u8 index, u16 value) {
    switch (index & 0x03) {
    case 0: cpu.set_bc(value); break;
    case 1: cpu.set_de(value); break;
    case 2: cpu.set_hl(value); break;
    default: cpu.set_af(value); break;
    }
}

void add_hl(Z80State& cpu, u16 rhs) {
    const u16 lhs = cpu.hl();
    const u32 sum = static_cast<u32>(lhs + rhs);
    const u16 result = static_cast<u16>(sum);
    const u8 preserved = static_cast<u8>(cpu.f & (flag_s | flag_z | flag_pv));
    cpu.f = static_cast<u8>(preserved |
        ((result >> 8) & (flag_y | flag_x)) |
        (((lhs ^ rhs ^ result) & 0x1000) ? flag_h : 0) |
        (sum > 0xFFFF ? flag_c : 0));
    cpu.set_hl(result);
}

void adc_hl(Z80State& cpu, u16 rhs) {
    const u16 lhs = cpu.hl();
    const u32 sum = static_cast<u32>(lhs + rhs + (cpu.f & flag_c));
    const u16 result = static_cast<u16>(sum);
    cpu.f = static_cast<u8>((result & 0x8000 ? flag_s : 0) |
        ((result >> 8) & (flag_y | flag_x)) |
        (result == 0 ? flag_z : 0) |
        (((lhs ^ rhs ^ result) & 0x1000) ? flag_h : 0) |
        ((~(lhs ^ rhs) & (lhs ^ result) & 0x8000) ? flag_pv : 0) |
        (sum > 0xFFFF ? flag_c : 0));
    cpu.set_hl(result);
}

void sbc_hl(Z80State& cpu, u16 rhs) {
    const u16 lhs = cpu.hl();
    const u32 carry = cpu.f & flag_c;
    const u32 diff = static_cast<u32>(lhs - rhs - carry);
    const u16 result = static_cast<u16>(diff);
    cpu.f = static_cast<u8>(flag_n |
        ((result >> 8) & (flag_y | flag_x)) |
        (result & 0x8000 ? flag_s : 0) |
        (result == 0 ? flag_z : 0) |
        (((lhs ^ rhs ^ result) & 0x1000) ? flag_h : 0) |
        (((lhs ^ rhs) & (lhs ^ result) & 0x8000) ? flag_pv : 0) |
        (static_cast<u32>(rhs + carry) > lhs ? flag_c : 0));
    cpu.set_hl(result);
}

u16 add16_index_flags(Z80State& cpu, u16 lhs, u16 rhs) {
    const u32 sum = static_cast<u32>(lhs + rhs);
    const u16 result = static_cast<u16>(sum);
    const u8 preserved = static_cast<u8>(cpu.f & (flag_s | flag_z | flag_pv));
    cpu.f = static_cast<u8>(preserved |
        ((result >> 8) & (flag_y | flag_x)) |
        (((lhs ^ rhs ^ result) & 0x1000) ? flag_h : 0) |
        (sum > 0xFFFF ? flag_c : 0));
    return result;
}

bool condition(const Z80State& cpu, u8 index) {
    switch (index & 0x07) {
    case 0: return (cpu.f & flag_z) == 0;
    case 1: return (cpu.f & flag_z) != 0;
    case 2: return (cpu.f & flag_c) == 0;
    case 3: return (cpu.f & flag_c) != 0;
    case 4: return (cpu.f & flag_pv) == 0;
    case 5: return (cpu.f & flag_pv) != 0;
    case 6: return (cpu.f & flag_s) == 0;
    default: return (cpu.f & flag_s) != 0;
    }
}

u8 read_reg(Z80State& cpu, Bus& bus, u8 index) {
    switch (index & 0x07) {
    case 0: return cpu.b;
    case 1: return cpu.c;
    case 2: return cpu.d;
    case 3: return cpu.e;
    case 4: return cpu.h;
    case 5: return cpu.l;
    case 6: return bus.read(cpu.hl());
    default: return cpu.a;
    }
}

void write_reg(Z80State& cpu, Bus& bus, u8 index, u8 value) {
    switch (index & 0x07) {
    case 0: cpu.b = value; break;
    case 1: cpu.c = value; break;
    case 2: cpu.d = value; break;
    case 3: cpu.e = value; break;
    case 4: cpu.h = value; break;
    case 5: cpu.l = value; break;
    case 6: bus.write(cpu.hl(), value); break;
    default: cpu.a = value; break;
    }
}

const char* reg_name(u8 index) {
    static constexpr const char* names[] = {"b", "c", "d", "e", "h", "l", "(hl)", "a"};
    return names[index & 0x07];
}

const char* rp_name(u8 index) {
    static constexpr const char* names[] = {"bc", "de", "hl", "sp"};
    return names[index & 0x03];
}

const char* qq_name(u8 index) {
    static constexpr const char* names[] = {"bc", "de", "hl", "af"};
    return names[index & 0x03];
}

const char* condition_name(u8 index) {
    static constexpr const char* names[] = {"nz", "z", "nc", "c", "po", "pe", "p", "m"};
    return names[index & 0x07];
}

void set_rotate_flags(Z80State& cpu, u8 result, bool carry) {
    cpu.f = static_cast<u8>((result & (flag_s | flag_y | flag_x)) |
        (result == 0 ? flag_z : 0) |
        parity(result) |
        (carry ? flag_c : 0));
}

u8 rotate_shift_cb(Z80State& cpu, u8 op, u8 value) {
    switch ((op >> 3) & 0x07) {
    case 0: {
        const bool carry = (value & 0x80) != 0;
        const u8 result = static_cast<u8>((value << 1) | (carry ? 1 : 0));
        set_rotate_flags(cpu, result, carry);
        return result;
    }
    case 1: {
        const bool carry = (value & 0x01) != 0;
        const u8 result = static_cast<u8>((value >> 1) | (carry ? 0x80 : 0));
        set_rotate_flags(cpu, result, carry);
        return result;
    }
    case 2: {
        const bool carry = (value & 0x80) != 0;
        const u8 result = static_cast<u8>((value << 1) | (cpu.f & flag_c));
        set_rotate_flags(cpu, result, carry);
        return result;
    }
    case 3: {
        const bool carry = (value & 0x01) != 0;
        const u8 result = static_cast<u8>((value >> 1) | ((cpu.f & flag_c) ? 0x80 : 0));
        set_rotate_flags(cpu, result, carry);
        return result;
    }
    case 4: {
        const bool carry = (value & 0x80) != 0;
        const u8 result = static_cast<u8>(value << 1);
        set_rotate_flags(cpu, result, carry);
        return result;
    }
    case 5: {
        const bool carry = (value & 0x01) != 0;
        const u8 result = static_cast<u8>((value >> 1) | (value & 0x80));
        set_rotate_flags(cpu, result, carry);
        return result;
    }
    case 6: {
        const bool carry = (value & 0x80) != 0;
        const u8 result = static_cast<u8>((value << 1) | 0x01);
        set_rotate_flags(cpu, result, carry);
        return result;
    }
    default: {
        const bool carry = (value & 0x01) != 0;
        const u8 result = static_cast<u8>(value >> 1);
        set_rotate_flags(cpu, result, carry);
        return result;
    }
    }
}

void execute_cb(Z80State& cpu, Bus& bus, u8 op) {
    const u8 reg = static_cast<u8>(op & 0x07);
    const u8 group = static_cast<u8>(op >> 6);
    const u8 bit = static_cast<u8>((op >> 3) & 0x07);

    if (group == 0) {
        const u8 result = rotate_shift_cb(cpu, op, read_reg(cpu, bus, reg));
        write_reg(cpu, bus, reg, result);
        cpu.cycles += reg == 6 ? 15 : 8;
        return;
    }

    if (group == 1) {
        const u8 value = read_reg(cpu, bus, reg);
        const bool zero = (value & (1u << bit)) == 0;
        const u8 carry = static_cast<u8>(cpu.f & flag_c);
        cpu.f = static_cast<u8>(carry | flag_h | (value & (flag_y | flag_x)) | (zero ? (flag_z | flag_pv) : 0) |
            (!zero && bit == 7 ? flag_s : 0));
        cpu.cycles += reg == 6 ? 12 : 8;
        return;
    }

    const u8 mask = static_cast<u8>(1u << bit);
    u8 value = read_reg(cpu, bus, reg);
    if (group == 2) {
        value = static_cast<u8>(value & ~mask);
    } else {
        value = static_cast<u8>(value | mask);
    }
    write_reg(cpu, bus, reg, value);
    cpu.cycles += reg == 6 ? 15 : 8;
}

void execute_index_cb(Z80State& cpu, Bus& bus, bool iy, s8 displacement, u8 op) {
    const u8 reg = static_cast<u8>(op & 0x07);
    const u8 group = static_cast<u8>(op >> 6);
    const u8 bit = static_cast<u8>((op >> 3) & 0x07);
    const u16 address = static_cast<u16>(read_index(cpu, iy) + displacement);

    if (group == 0) {
        const u8 result = rotate_shift_cb(cpu, op, bus.read(address));
        bus.write(address, result);
        if (reg != 6) {
            write_reg(cpu, bus, reg, result);
        }
        cpu.cycles += 23;
        return;
    }

    if (group == 1) {
        const u8 value = bus.read(address);
        const bool zero = (value & (1u << bit)) == 0;
        const u8 carry = static_cast<u8>(cpu.f & flag_c);
        cpu.f = static_cast<u8>(carry | flag_h | ((address >> 8) & (flag_y | flag_x)) | (zero ? (flag_z | flag_pv) : 0) |
            (!zero && bit == 7 ? flag_s : 0));
        cpu.cycles += 20;
        return;
    }

    const u8 mask = static_cast<u8>(1u << bit);
    u8 value = bus.read(address);
    if (group == 2) {
        value = static_cast<u8>(value & ~mask);
    } else {
        value = static_cast<u8>(value | mask);
    }
    bus.write(address, value);
    if (reg != 6) {
        write_reg(cpu, bus, reg, value);
    }
    cpu.cycles += 23;
}

void set_ld_a_ir_flags(Z80State& cpu, u8 value) {
    const u8 carry = static_cast<u8>(cpu.f & flag_c);
    cpu.f = static_cast<u8>(carry |
        (value & (flag_s | flag_y | flag_x)) |
        (value == 0 ? flag_z : 0) |
        (cpu.iff2 ? flag_pv : 0));
}

void block_transfer(Z80State& cpu, Bus& bus, s16 step) {
    const u8 value = bus.read(cpu.hl());
    bus.write(cpu.de(), value);
    cpu.set_hl(static_cast<u16>(cpu.hl() + step));
    cpu.set_de(static_cast<u16>(cpu.de() + step));
    cpu.set_bc(static_cast<u16>(cpu.bc() - 1));
    const u8 sum = static_cast<u8>(cpu.a + value);
    cpu.f = static_cast<u8>((cpu.f & (flag_s | flag_z | flag_c)) |
        (sum & flag_x) | ((sum & 0x02) ? flag_y : 0) | (cpu.bc() != 0 ? flag_pv : 0));
}

void block_compare(Z80State& cpu, Bus& bus, s16 step) {
    const u8 value = bus.read(cpu.hl());
    const u8 carry = static_cast<u8>(cpu.f & flag_c);
    const u8 result = static_cast<u8>(cpu.a - value);
    cpu.set_hl(static_cast<u16>(cpu.hl() + step));
    cpu.set_bc(static_cast<u16>(cpu.bc() - 1));
    const u8 adjusted = static_cast<u8>(result - (((cpu.a ^ value ^ result) & 0x10) ? 1 : 0));
    cpu.f = static_cast<u8>(carry | flag_n |
        (result & flag_s) | (adjusted & flag_x) | ((adjusted & 0x02) ? flag_y : 0) |
        (result == 0 ? flag_z : 0) |
        (((cpu.a ^ value ^ result) & 0x10) ? flag_h : 0) |
        (cpu.bc() != 0 ? flag_pv : 0));
}

void set_block_io_flags(Z80State& cpu, u8 value, u8 addend) {
    const u16 sum = static_cast<u16>(value + addend);
    cpu.f = static_cast<u8>((cpu.b & (flag_s | flag_y | flag_x)) |
        (cpu.b == 0 ? flag_z : 0) |
        ((value & 0x80) != 0 ? flag_n : 0) |
        (sum > 0xFF ? (flag_h | flag_c) : 0) |
        parity(static_cast<u8>((sum & 0x07) ^ cpu.b)));
}

void ini(Z80State& cpu, Bus& bus, s16 step) {
    const u8 value = bus.input(cpu.c);
    bus.write(cpu.hl(), value);
    cpu.set_hl(static_cast<u16>(cpu.hl() + step));
    cpu.b = static_cast<u8>(cpu.b - 1);
    set_block_io_flags(cpu, value, static_cast<u8>(cpu.c + step));
}

void outi(Z80State& cpu, Bus& bus, s16 step) {
    const u8 value = bus.read(cpu.hl());
    bus.output(cpu.c, value);
    cpu.set_hl(static_cast<u16>(cpu.hl() + step));
    cpu.b = static_cast<u8>(cpu.b - 1);
    set_block_io_flags(cpu, value, cpu.l);
}

void set_in_flags(Z80State& cpu, u8 value) {
    const u8 carry = static_cast<u8>(cpu.f & flag_c);
    cpu.f = static_cast<u8>(carry |
        (value & (flag_s | flag_y | flag_x)) |
        (value == 0 ? flag_z : 0) |
        parity(value));
}

void set_accumulator_flags(Z80State& cpu) {
    const u8 carry = static_cast<u8>(cpu.f & flag_c);
    cpu.f = static_cast<u8>(carry |
        (cpu.a & (flag_s | flag_y | flag_x)) |
        (cpu.a == 0 ? flag_z : 0) |
        parity(cpu.a));
}

void execute_ed(Z80State& cpu, Bus& bus, u8 op) {
    if ((op & 0xC7) == 0x40) {
        const u8 reg = static_cast<u8>((op >> 3) & 0x07);
        const u8 value = bus.input(cpu.c);
        if (reg != 6) {
            write_reg(cpu, bus, reg, value);
        }
        set_in_flags(cpu, value);
        cpu.cycles += 12;
        return;
    }
    if ((op & 0xC7) == 0x41) {
        const u8 reg = static_cast<u8>((op >> 3) & 0x07);
        bus.output(cpu.c, reg == 6 ? 0 : read_reg(cpu, bus, reg));
        cpu.cycles += 12;
        return;
    }

    switch (op) {
    case 0x42: case 0x52: case 0x62: case 0x72:
        sbc_hl(cpu, read_rp(cpu, static_cast<u8>((op >> 4) & 0x03)));
        cpu.cycles += 15;
        return;
    case 0x4A: case 0x5A: case 0x6A: case 0x7A:
        adc_hl(cpu, read_rp(cpu, static_cast<u8>((op >> 4) & 0x03)));
        cpu.cycles += 15;
        return;
    case 0x44: case 0x4C: case 0x54: case 0x5C:
    case 0x64: case 0x6C: case 0x74: case 0x7C:
        cpu.a = sub8(cpu, 0, cpu.a);
        cpu.cycles += 8;
        return;
    case 0x45: case 0x55: case 0x5D: case 0x65:
    case 0x6D: case 0x75: case 0x7D:
        cpu.pc = pop16(cpu, bus);
        cpu.iff1 = cpu.iff2;
        cpu.cycles += 14;
        return;
    case 0x4D:
        cpu.pc = pop16(cpu, bus);
        cpu.cycles += 14;
        return;
    case 0x46: case 0x66:
        cpu.interrupt_mode = 0;
        cpu.cycles += 8;
        return;
    case 0x56: case 0x76:
        cpu.interrupt_mode = 1;
        cpu.cycles += 8;
        return;
    case 0x5E: case 0x7E:
        cpu.interrupt_mode = 2;
        cpu.cycles += 8;
        return;
    case 0x47:
        cpu.i = cpu.a;
        cpu.cycles += 9;
        return;
    case 0x4F:
        cpu.r = cpu.a;
        cpu.cycles += 9;
        return;
    case 0x57:
        cpu.a = cpu.i;
        set_ld_a_ir_flags(cpu, cpu.a);
        cpu.cycles += 9;
        return;
    case 0x5F:
        cpu.a = cpu.r;
        set_ld_a_ir_flags(cpu, cpu.a);
        cpu.cycles += 9;
        return;
    case 0x67: {
        const u8 value = bus.read(cpu.hl());
        bus.write(cpu.hl(), static_cast<u8>((cpu.a << 4) | (value >> 4)));
        cpu.a = static_cast<u8>((cpu.a & 0xF0) | (value & 0x0F));
        set_accumulator_flags(cpu);
        cpu.cycles += 18;
        return;
    }
    case 0x6F: {
        const u8 value = bus.read(cpu.hl());
        bus.write(cpu.hl(), static_cast<u8>((value << 4) | (cpu.a & 0x0F)));
        cpu.a = static_cast<u8>((cpu.a & 0xF0) | (value >> 4));
        set_accumulator_flags(cpu);
        cpu.cycles += 18;
        return;
    }
    case 0x43: case 0x53: case 0x63: case 0x73: {
        const u16 address = fetch16(cpu, bus);
        write16(bus, address, read_rp(cpu, static_cast<u8>((op >> 4) & 0x03)));
        cpu.cycles += 20;
        return;
    }
    case 0x4B: case 0x5B: case 0x6B: case 0x7B: {
        const u16 address = fetch16(cpu, bus);
        write_rp(cpu, static_cast<u8>((op >> 4) & 0x03), read16(bus, address));
        cpu.cycles += 20;
        return;
    }
    case 0xA0:
        block_transfer(cpu, bus, 1);
        cpu.cycles += 16;
        return;
    case 0xA8:
        block_transfer(cpu, bus, -1);
        cpu.cycles += 16;
        return;
    case 0xB0:
        block_transfer(cpu, bus, 1);
        if (cpu.bc() != 0) {
            cpu.pc = static_cast<u16>(cpu.pc - 2);
            cpu.cycles += 21;
        } else {
            cpu.cycles += 16;
        }
        return;
    case 0xB8:
        block_transfer(cpu, bus, -1);
        if (cpu.bc() != 0) {
            cpu.pc = static_cast<u16>(cpu.pc - 2);
            cpu.cycles += 21;
        } else {
            cpu.cycles += 16;
        }
        return;
    case 0xA1:
        block_compare(cpu, bus, 1);
        cpu.cycles += 16;
        return;
    case 0xA9:
        block_compare(cpu, bus, -1);
        cpu.cycles += 16;
        return;
    case 0xA2:
        ini(cpu, bus, 1);
        cpu.cycles += 16;
        return;
    case 0xA3:
        outi(cpu, bus, 1);
        cpu.cycles += 16;
        return;
    case 0xAA:
        ini(cpu, bus, -1);
        cpu.cycles += 16;
        return;
    case 0xAB:
        outi(cpu, bus, -1);
        cpu.cycles += 16;
        return;
    case 0xB1:
        block_compare(cpu, bus, 1);
        if (cpu.bc() != 0 && (cpu.f & flag_z) == 0) {
            cpu.pc = static_cast<u16>(cpu.pc - 2);
            cpu.cycles += 21;
        } else {
            cpu.cycles += 16;
        }
        return;
    case 0xB9:
        block_compare(cpu, bus, -1);
        if (cpu.bc() != 0 && (cpu.f & flag_z) == 0) {
            cpu.pc = static_cast<u16>(cpu.pc - 2);
            cpu.cycles += 21;
        } else {
            cpu.cycles += 16;
        }
        return;
    case 0xB2:
        ini(cpu, bus, 1);
        if (cpu.b != 0) {
            cpu.pc = static_cast<u16>(cpu.pc - 2);
            cpu.cycles += 21;
        } else {
            cpu.cycles += 16;
        }
        return;
    case 0xB3:
        outi(cpu, bus, 1);
        if (cpu.b != 0) {
            cpu.pc = static_cast<u16>(cpu.pc - 2);
            cpu.cycles += 21;
        } else {
            cpu.cycles += 16;
        }
        return;
    case 0xBA:
        ini(cpu, bus, -1);
        if (cpu.b != 0) {
            cpu.pc = static_cast<u16>(cpu.pc - 2);
            cpu.cycles += 21;
        } else {
            cpu.cycles += 16;
        }
        return;
    case 0xBB:
        outi(cpu, bus, -1);
        if (cpu.b != 0) {
            cpu.pc = static_cast<u16>(cpu.pc - 2);
            cpu.cycles += 21;
        } else {
            cpu.cycles += 16;
        }
        return;
    default:
        // Undefined ED encodings execute as two-byte NOPs on a Z80.
        cpu.cycles += 8;
        return;
    }
}

void execute_index(Z80State& cpu, Bus& bus, bool iy, u8 op) {
    switch (op) {
    case 0xCB: {
        const auto displacement = static_cast<s8>(fetch8(cpu, bus));
        execute_index_cb(cpu, bus, iy, displacement, fetch8(cpu, bus));
        return;
    }
    case 0x09: case 0x19: case 0x29: case 0x39: {
        const u8 rp = static_cast<u8>((op >> 4) & 0x03);
        const u16 rhs = rp == 2 ? read_index(cpu, iy) : read_rp(cpu, rp);
        write_index(cpu, iy, add16_index_flags(cpu, read_index(cpu, iy), rhs));
        cpu.cycles += 15;
        return;
    }
    case 0x21:
        write_index(cpu, iy, fetch16(cpu, bus));
        cpu.cycles += 14;
        return;
    case 0x22: {
        const u16 address = fetch16(cpu, bus);
        write16(bus, address, read_index(cpu, iy));
        cpu.cycles += 20;
        return;
    }
    case 0x23:
        write_index(cpu, iy, static_cast<u16>(read_index(cpu, iy) + 1));
        cpu.cycles += 10;
        return;
    case 0x2A: {
        const u16 address = fetch16(cpu, bus);
        write_index(cpu, iy, read16(bus, address));
        cpu.cycles += 20;
        return;
    }
    case 0x2B:
        write_index(cpu, iy, static_cast<u16>(read_index(cpu, iy) - 1));
        cpu.cycles += 10;
        return;
    case 0x24:
    case 0x25:
    case 0x2C:
    case 0x2D: {
        const u8 reg = static_cast<u8>((op >> 3) & 0x07);
        const u8 value = read_index_reg(cpu, iy, reg);
        write_index_reg(cpu, iy, reg, (op & 0x01) != 0 ? dec8(cpu, value) : inc8(cpu, value));
        cpu.cycles += 8;
        return;
    }
    case 0x26:
    case 0x2E:
        write_index_reg(cpu, iy, static_cast<u8>((op >> 3) & 0x07), fetch8(cpu, bus));
        cpu.cycles += 11;
        return;
    case 0x34: {
        const auto displacement = static_cast<s8>(fetch8(cpu, bus));
        const u16 address = static_cast<u16>(read_index(cpu, iy) + displacement);
        bus.write(address, inc8(cpu, bus.read(address)));
        cpu.cycles += 23;
        return;
    }
    case 0x35: {
        const auto displacement = static_cast<s8>(fetch8(cpu, bus));
        const u16 address = static_cast<u16>(read_index(cpu, iy) + displacement);
        bus.write(address, dec8(cpu, bus.read(address)));
        cpu.cycles += 23;
        return;
    }
    case 0x36: {
        const auto displacement = static_cast<s8>(fetch8(cpu, bus));
        bus.write(static_cast<u16>(read_index(cpu, iy) + displacement), fetch8(cpu, bus));
        cpu.cycles += 19;
        return;
    }
    case 0x76:
        cpu.halted = true;
        cpu.cycles += 8;
        return;
    case 0xE1:
        write_index(cpu, iy, pop16(cpu, bus));
        cpu.cycles += 14;
        return;
    case 0xE3: {
        const u16 stack_value = read16(bus, cpu.sp);
        write16(bus, cpu.sp, read_index(cpu, iy));
        write_index(cpu, iy, stack_value);
        cpu.cycles += 23;
        return;
    }
    case 0xE5:
        push16(cpu, bus, read_index(cpu, iy));
        cpu.cycles += 15;
        return;
    case 0xE9:
        cpu.pc = read_index(cpu, iy);
        cpu.cycles += 8;
        return;
    case 0xF9:
        cpu.sp = read_index(cpu, iy);
        cpu.cycles += 10;
        return;
    default: {
        if ((op & 0xC7) == 0x46) {
            const u8 reg = static_cast<u8>((op >> 3) & 0x07);
            const auto displacement = static_cast<s8>(fetch8(cpu, bus));
            write_reg(cpu, bus, reg, bus.read(static_cast<u16>(read_index(cpu, iy) + displacement)));
            cpu.cycles += 19;
            return;
        }
        if ((op & 0xF8) == 0x70) {
            const u8 reg = static_cast<u8>(op & 0x07);
            const auto displacement = static_cast<s8>(fetch8(cpu, bus));
            const u8 value = reg == 6 ? bus.read(static_cast<u16>(read_index(cpu, iy) + displacement)) : read_index_reg(cpu, iy, reg);
            bus.write(static_cast<u16>(read_index(cpu, iy) + displacement), value);
            cpu.cycles += 19;
            return;
        }
        if ((op & 0xC7) == 0x86) {
            const auto displacement = static_cast<s8>(fetch8(cpu, bus));
            const u8 rhs = bus.read(static_cast<u16>(read_index(cpu, iy) + displacement));
            switch ((op >> 3) & 0x07) {
            case 0: cpu.a = add8(cpu, cpu.a, rhs); break;
            case 1: cpu.a = adc8(cpu, cpu.a, rhs); break;
            case 2: cpu.a = sub8(cpu, cpu.a, rhs); break;
            case 3: cpu.a = sbc8(cpu, cpu.a, rhs); break;
            case 4: cpu.a = and8(cpu, cpu.a, rhs); break;
            case 5: cpu.a = xor8(cpu, cpu.a, rhs); break;
            case 6: cpu.a = or8(cpu, cpu.a, rhs); break;
            case 7: (void)sub8(cpu, cpu.a, rhs); break;
            }
            cpu.cycles += 19;
            return;
        }
        if ((op & 0xC0) == 0x40 && op != 0x76) {
            const u8 dst = static_cast<u8>((op >> 3) & 0x07);
            const u8 src = static_cast<u8>(op & 0x07);
            if (dst != 6 && src != 6 && (dst == 4 || dst == 5 || src == 4 || src == 5)) {
                write_index_reg(cpu, iy, dst, read_index_reg(cpu, iy, src));
                cpu.cycles += 8;
                return;
            }
            if (dst != 6 && src != 6) {
                write_reg(cpu, bus, dst, read_reg(cpu, bus, src));
                cpu.cycles += 8;
                return;
            }
        }
        if ((op & 0xC0) == 0x80 && ((op & 0x07) == 4 || (op & 0x07) == 5)) {
            const u8 rhs = read_index_reg(cpu, iy, op & 0x07);
            switch ((op >> 3) & 0x07) {
            case 0: cpu.a = add8(cpu, cpu.a, rhs); break;
            case 1: cpu.a = adc8(cpu, cpu.a, rhs); break;
            case 2: cpu.a = sub8(cpu, cpu.a, rhs); break;
            case 3: cpu.a = sbc8(cpu, cpu.a, rhs); break;
            case 4: cpu.a = and8(cpu, cpu.a, rhs); break;
            case 5: cpu.a = xor8(cpu, cpu.a, rhs); break;
            case 6: cpu.a = or8(cpu, cpu.a, rhs); break;
            case 7: (void)sub8(cpu, cpu.a, rhs); break;
            }
            cpu.cycles += 8;
            return;
        }
        if ((op & 0xC0) == 0x80 && (op & 0x07) != 6) {
            const u8 rhs = read_reg(cpu, bus, op & 0x07);
            switch ((op >> 3) & 0x07) {
            case 0: cpu.a = add8(cpu, cpu.a, rhs); break;
            case 1: cpu.a = adc8(cpu, cpu.a, rhs); break;
            case 2: cpu.a = sub8(cpu, cpu.a, rhs); break;
            case 3: cpu.a = sbc8(cpu, cpu.a, rhs); break;
            case 4: cpu.a = and8(cpu, cpu.a, rhs); break;
            case 5: cpu.a = xor8(cpu, cpu.a, rhs); break;
            case 6: cpu.a = or8(cpu, cpu.a, rhs); break;
            case 7: (void)sub8(cpu, cpu.a, rhs); break;
            }
            cpu.cycles += 8;
            return;
        }
        // DD/FD prefixes are ignored for opcodes that do not reference HL/H/L/(HL).
        // Replaying the already fetched opcode preserves normal operand decoding;
        // execute_one also accounts for its opcode fetch/R increment.
        cpu.pc = static_cast<u16>(cpu.pc - 1);
        decrement_refresh(cpu);
        cpu.cycles += 4;
        execute_one(cpu, bus);
        return;
    }
    }
}

std::string imm8(const std::array<u8, 0x10000>& memory, u16 pc, const char* op) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%s $%02X", op, memory[static_cast<u16>(pc + 1)]);
    return buffer;
}

std::string imm16(const std::array<u8, 0x10000>& memory, u16 pc, const char* op) {
    char buffer[40];
    const u16 value = make_u16(memory[static_cast<u16>(pc + 1)], memory[static_cast<u16>(pc + 2)]);
    std::snprintf(buffer, sizeof(buffer), "%s $%04X", op, value);
    return buffer;
}

std::string cb_mnemonic(u8 op) {
    char buffer[32];
    const u8 group = static_cast<u8>(op >> 6);
    const u8 bit = static_cast<u8>((op >> 3) & 0x07);
    if (group == 0) {
        static constexpr const char* ops[] = {"rlc", "rrc", "rl", "rr", "sla", "sra", "sll", "srl"};
        std::snprintf(buffer, sizeof(buffer), "%s %s", ops[bit], reg_name(op & 0x07));
    } else if (group == 1) {
        std::snprintf(buffer, sizeof(buffer), "bit %u,%s", bit, reg_name(op & 0x07));
    } else if (group == 2) {
        std::snprintf(buffer, sizeof(buffer), "res %u,%s", bit, reg_name(op & 0x07));
    } else {
        std::snprintf(buffer, sizeof(buffer), "set %u,%s", bit, reg_name(op & 0x07));
    }
    return buffer;
}

std::string ed_mnemonic(const std::array<u8, 0x10000>& memory, u16 pc, u8 op) {
    char buffer[40];
    if ((op & 0xC7) == 0x40) {
        const u8 reg = static_cast<u8>((op >> 3) & 0x07);
        if (reg == 6) {
            return "in (c)";
        }
        std::snprintf(buffer, sizeof(buffer), "in %s,(c)", reg_name(reg));
        return buffer;
    }
    if ((op & 0xC7) == 0x41) {
        const u8 reg = static_cast<u8>((op >> 3) & 0x07);
        if (reg == 6) {
            return "out (c),0";
        }
        std::snprintf(buffer, sizeof(buffer), "out (c),%s", reg_name(reg));
        return buffer;
    }
    switch (op) {
    case 0x42: case 0x52: case 0x62: case 0x72:
        std::snprintf(buffer, sizeof(buffer), "sbc hl,%s", rp_name((op >> 4) & 0x03));
        return buffer;
    case 0x4A: case 0x5A: case 0x6A: case 0x7A:
        std::snprintf(buffer, sizeof(buffer), "adc hl,%s", rp_name((op >> 4) & 0x03));
        return buffer;
    case 0x44: case 0x4C: case 0x54: case 0x5C:
    case 0x64: case 0x6C: case 0x74: case 0x7C:
        return "neg";
    case 0x45: case 0x55: case 0x5D: case 0x65:
    case 0x6D: case 0x75: case 0x7D:
        return "retn";
    case 0x4D: return "reti";
    case 0x46: case 0x66: return "im 0";
    case 0x56: case 0x76: return "im 1";
    case 0x5E: case 0x7E: return "im 2";
    case 0x47: return "ld i,a";
    case 0x4F: return "ld r,a";
    case 0x57: return "ld a,i";
    case 0x5F: return "ld a,r";
    case 0x67: return "rrd";
    case 0x6F: return "rld";
    case 0x43: case 0x53: case 0x63: case 0x73:
        std::snprintf(buffer, sizeof(buffer), "ld ($%04X),%s",
            make_u16(memory[static_cast<u16>(pc + 2)], memory[static_cast<u16>(pc + 3)]),
            rp_name((op >> 4) & 0x03));
        return buffer;
    case 0x4B: case 0x5B: case 0x6B: case 0x7B:
        std::snprintf(buffer, sizeof(buffer), "ld %s,($%04X)",
            rp_name((op >> 4) & 0x03),
            make_u16(memory[static_cast<u16>(pc + 2)], memory[static_cast<u16>(pc + 3)]));
        return buffer;
    case 0xA0: return "ldi";
    case 0xB0: return "ldir";
    case 0xA1: return "cpi";
    case 0xA2: return "ini";
    case 0xA3: return "outi";
    case 0xAA: return "ind";
    case 0xAB: return "outd";
    case 0xB1: return "cpir";
    case 0xB2: return "inir";
    case 0xB3: return "otir";
    case 0xBA: return "indr";
    case 0xBB: return "otdr";
    default: return "ed db";
    }
}

std::string index_mnemonic(const std::array<u8, 0x10000>& memory, u16 pc, bool iy, u8 op) {
    const char* name = iy ? "iy" : "ix";
    char buffer[48];
    switch (op) {
    case 0xCB: {
        const auto displacement = static_cast<s8>(memory[static_cast<u16>(pc + 2)]);
        const u8 cb = memory[static_cast<u16>(pc + 3)];
        const std::string cb_text = cb_mnemonic(cb);
        std::snprintf(buffer, sizeof(buffer), "%s (%s%+d)", cb_text.c_str(), name, displacement);
        return buffer;
    }
    case 0x09: case 0x19: case 0x29: case 0x39: {
        const u8 rp = static_cast<u8>((op >> 4) & 0x03);
        std::snprintf(buffer, sizeof(buffer), "add %s,%s", name, rp == 2 ? name : rp_name(rp));
        return buffer;
    }
    case 0x21:
        std::snprintf(buffer, sizeof(buffer), "ld %s,$%04X", name,
            make_u16(memory[static_cast<u16>(pc + 2)], memory[static_cast<u16>(pc + 3)]));
        return buffer;
    case 0x22:
        std::snprintf(buffer, sizeof(buffer), "ld ($%04X),%s",
            make_u16(memory[static_cast<u16>(pc + 2)], memory[static_cast<u16>(pc + 3)]), name);
        return buffer;
    case 0x23:
        std::snprintf(buffer, sizeof(buffer), "inc %s", name);
        return buffer;
    case 0x2A:
        std::snprintf(buffer, sizeof(buffer), "ld %s,($%04X)", name,
            make_u16(memory[static_cast<u16>(pc + 2)], memory[static_cast<u16>(pc + 3)]));
        return buffer;
    case 0x2B:
        std::snprintf(buffer, sizeof(buffer), "dec %s", name);
        return buffer;
    case 0x34:
        std::snprintf(buffer, sizeof(buffer), "inc (%s%+d)", name, static_cast<s8>(memory[static_cast<u16>(pc + 2)]));
        return buffer;
    case 0x35:
        std::snprintf(buffer, sizeof(buffer), "dec (%s%+d)", name, static_cast<s8>(memory[static_cast<u16>(pc + 2)]));
        return buffer;
    case 0x36:
        std::snprintf(buffer, sizeof(buffer), "ld (%s%+d),$%02X", name,
            static_cast<s8>(memory[static_cast<u16>(pc + 2)]),
            memory[static_cast<u16>(pc + 3)]);
        return buffer;
    case 0xE1:
        std::snprintf(buffer, sizeof(buffer), "pop %s", name);
        return buffer;
    case 0xE3:
        std::snprintf(buffer, sizeof(buffer), "ex (sp),%s", name);
        return buffer;
    case 0xE5:
        std::snprintf(buffer, sizeof(buffer), "push %s", name);
        return buffer;
    case 0xE9:
        std::snprintf(buffer, sizeof(buffer), "jp (%s)", name);
        return buffer;
    case 0xF9:
        std::snprintf(buffer, sizeof(buffer), "ld sp,%s", name);
        return buffer;
    default:
        if ((op & 0xC7) == 0x46) {
            std::snprintf(buffer, sizeof(buffer), "ld %s,(%s%+d)",
                reg_name((op >> 3) & 0x07), name, static_cast<s8>(memory[static_cast<u16>(pc + 2)]));
            return buffer;
        }
        if ((op & 0xF8) == 0x70) {
            std::snprintf(buffer, sizeof(buffer), "ld (%s%+d),%s",
                name, static_cast<s8>(memory[static_cast<u16>(pc + 2)]), reg_name(op & 0x07));
            return buffer;
        }
        if ((op & 0xC7) == 0x86) {
            static constexpr const char* ops[] = {"add a", "adc a", "sub", "sbc a", "and", "xor", "or", "cp"};
            std::snprintf(buffer, sizeof(buffer), "%s,(%s%+d)",
                ops[(op >> 3) & 0x07], name, static_cast<s8>(memory[static_cast<u16>(pc + 2)]));
            return buffer;
        }
        std::snprintf(buffer, sizeof(buffer), "%s db", iy ? "fd" : "dd");
        return buffer;
    }
}

} // namespace

void Z80State::set_af(u16 value) {
    f = static_cast<u8>(value);
    a = static_cast<u8>(value >> 8);
}

void Z80State::set_bc(u16 value) {
    c = static_cast<u8>(value);
    b = static_cast<u8>(value >> 8);
}

void Z80State::set_de(u16 value) {
    e = static_cast<u8>(value);
    d = static_cast<u8>(value >> 8);
}

void Z80State::set_hl(u16 value) {
    l = static_cast<u8>(value);
    h = static_cast<u8>(value >> 8);
}

DecodedInstruction decode_z80(const std::array<u8, 0x10000>& memory, u16 pc) {
    const u8 opcode = memory[pc];
    DecodedInstruction out{pc, opcode, 1, 4, "db"};
    switch (opcode) {
    case 0x00: out.mnemonic = "nop"; break;
    case 0x01: out.size = 3; out.cycles = 10; out.mnemonic = imm16(memory, pc, "ld bc,"); break;
    case 0x02: out.cycles = 7; out.mnemonic = "ld (bc),a"; break;
    case 0x04: out.cycles = 4; out.mnemonic = "inc b"; break;
    case 0x05: out.cycles = 4; out.mnemonic = "dec b"; break;
    case 0x06: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "ld b,"); break;
    case 0x07: out.cycles = 4; out.mnemonic = "rlca"; break;
    case 0x0C: out.cycles = 4; out.mnemonic = "inc c"; break;
    case 0x0D: out.cycles = 4; out.mnemonic = "dec c"; break;
    case 0x0E: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "ld c,"); break;
    case 0x0F: out.cycles = 4; out.mnemonic = "rrca"; break;
    case 0x08: out.cycles = 4; out.mnemonic = "ex af,af'"; break;
    case 0x0A: out.cycles = 7; out.mnemonic = "ld a,(bc)"; break;
    case 0x10: out.size = 2; out.cycles = 13; out.mnemonic = imm8(memory, pc, "djnz"); break;
    case 0x11: out.size = 3; out.cycles = 10; out.mnemonic = imm16(memory, pc, "ld de,"); break;
    case 0x12: out.cycles = 7; out.mnemonic = "ld (de),a"; break;
    case 0x18: out.size = 2; out.cycles = 12; out.mnemonic = imm8(memory, pc, "jr"); break;
    case 0x20: out.size = 2; out.cycles = 12; out.mnemonic = imm8(memory, pc, "jr nz,"); break;
    case 0x16: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "ld d,"); break;
    case 0x17: out.cycles = 4; out.mnemonic = "rla"; break;
    case 0x1E: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "ld e,"); break;
    case 0x1A: out.cycles = 7; out.mnemonic = "ld a,(de)"; break;
    case 0x1F: out.cycles = 4; out.mnemonic = "rra"; break;
    case 0x21: out.size = 3; out.cycles = 10; out.mnemonic = imm16(memory, pc, "ld hl,"); break;
    case 0x22: out.size = 3; out.cycles = 16; out.mnemonic = imm16(memory, pc, "ld (),hl"); break;
    case 0x27: out.cycles = 4; out.mnemonic = "daa"; break;
    case 0x28: out.size = 2; out.cycles = 12; out.mnemonic = imm8(memory, pc, "jr z,"); break;
    case 0x2F: out.cycles = 4; out.mnemonic = "cpl"; break;
    case 0x30: out.size = 2; out.cycles = 12; out.mnemonic = imm8(memory, pc, "jr nc,"); break;
    case 0x26: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "ld h,"); break;
    case 0x2E: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "ld l,"); break;
    case 0x2A: out.size = 3; out.cycles = 16; out.mnemonic = imm16(memory, pc, "ld hl,()"); break;
    case 0x31: out.size = 3; out.cycles = 10; out.mnemonic = imm16(memory, pc, "ld sp,"); break;
    case 0x32: out.size = 3; out.cycles = 13; out.mnemonic = imm16(memory, pc, "ld (),a"); break;
    case 0x36: out.size = 2; out.cycles = 10; out.mnemonic = imm8(memory, pc, "ld (hl),"); break;
    case 0x3A: out.size = 3; out.cycles = 13; out.mnemonic = imm16(memory, pc, "ld a,()"); break;
    case 0x38: out.size = 2; out.cycles = 12; out.mnemonic = imm8(memory, pc, "jr c,"); break;
    case 0x3C: out.cycles = 4; out.mnemonic = "inc a"; break;
    case 0x3D: out.cycles = 4; out.mnemonic = "dec a"; break;
    case 0x3E: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "ld a,"); break;
    case 0xCB:
        out.size = 2;
        out.cycles = (memory[static_cast<u16>(pc + 1)] & 0x07) == 6
            ? (((memory[static_cast<u16>(pc + 1)] >> 6) & 0x03) == 1 ? 12 : 15)
            : 8;
        out.mnemonic = cb_mnemonic(memory[static_cast<u16>(pc + 1)]);
        break;
    case 0x76: out.cycles = 4; out.mnemonic = "halt"; break;
    case 0x80: out.mnemonic = "add a,b"; break;
    case 0x81: out.mnemonic = "add a,c"; break;
    case 0x87: out.mnemonic = "add a,a"; break;
    case 0x90: out.mnemonic = "sub b"; break;
    case 0x91: out.mnemonic = "sub c"; break;
    case 0xAF: out.mnemonic = "xor a"; break;
    case 0xC3: out.size = 3; out.cycles = 10; out.mnemonic = imm16(memory, pc, "jp"); break;
    case 0xC6: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "add a,"); break;
    case 0xC9: out.cycles = 10; out.mnemonic = "ret"; break;
    case 0xCE: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "adc a,"); break;
    case 0xCD: out.size = 3; out.cycles = 17; out.mnemonic = imm16(memory, pc, "call"); break;
    case 0xD6: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "sub"); break;
    case 0xD3: out.size = 2; out.cycles = 11; out.mnemonic = imm8(memory, pc, "out"), out.mnemonic += ",a"; break;
    case 0xDB: out.size = 2; out.cycles = 11; out.mnemonic = imm8(memory, pc, "in a,"); break;
    case 0xD9: out.cycles = 4; out.mnemonic = "exx"; break;
    case 0xDD: {
        const u8 op = memory[static_cast<u16>(pc + 1)];
        out.size = (op == 0xCB || op == 0x36) ? 4 :
            (((op & 0xC7) == 0x46 || (op & 0xF8) == 0x70 || (op & 0xC7) == 0x86 || op == 0x34 || op == 0x35) ? 3 :
            ((op == 0x21 || op == 0x22 || op == 0x2A) ? 4 : 2));
        out.cycles = 8;
        out.mnemonic = index_mnemonic(memory, pc, false, op);
        break;
    }
    case 0xDE: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "sbc a,"); break;
    case 0xED: {
        const u8 ed = memory[static_cast<u16>(pc + 1)];
        out.size = ((ed & 0xCF) == 0x43 || (ed & 0xCF) == 0x4B) ? 4 : 2;
        out.cycles = out.size == 4 ? 20 : 8;
        out.mnemonic = ed_mnemonic(memory, pc, ed);
        break;
    }
    case 0xE3: out.cycles = 19; out.mnemonic = "ex (sp),hl"; break;
    case 0xE6: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "and"); break;
    case 0xE9: out.cycles = 4; out.mnemonic = "jp (hl)"; break;
    case 0xEB: out.cycles = 4; out.mnemonic = "ex de,hl"; break;
    case 0xEE: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "xor"); break;
    case 0x37: out.cycles = 4; out.mnemonic = "scf"; break;
    case 0x3F: out.cycles = 4; out.mnemonic = "ccf"; break;
    case 0xF3: out.cycles = 4; out.mnemonic = "di"; break;
    case 0xFD: {
        const u8 op = memory[static_cast<u16>(pc + 1)];
        out.size = (op == 0xCB || op == 0x36) ? 4 :
            (((op & 0xC7) == 0x46 || (op & 0xF8) == 0x70 || (op & 0xC7) == 0x86 || op == 0x34 || op == 0x35) ? 3 :
            ((op == 0x21 || op == 0x22 || op == 0x2A) ? 4 : 2));
        out.cycles = 8;
        out.mnemonic = index_mnemonic(memory, pc, true, op);
        break;
    }
    case 0xF6: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "or"); break;
    case 0xF9: out.cycles = 6; out.mnemonic = "ld sp,hl"; break;
    case 0xFB: out.cycles = 4; out.mnemonic = "ei"; break;
    case 0xFE: out.size = 2; out.cycles = 7; out.mnemonic = imm8(memory, pc, "cp"); break;
    default:
        if ((opcode & 0xCF) == 0x01) {
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "ld %s,$%04X", rp_name((opcode >> 4) & 0x03),
                make_u16(memory[static_cast<u16>(pc + 1)], memory[static_cast<u16>(pc + 2)]));
            out.size = 3;
            out.cycles = 10;
            out.mnemonic = buffer;
        } else if ((opcode & 0xCF) == 0x03) {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "inc %s", rp_name((opcode >> 4) & 0x03));
            out.cycles = 6;
            out.mnemonic = buffer;
        } else if ((opcode & 0xCF) == 0x09) {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "add hl,%s", rp_name((opcode >> 4) & 0x03));
            out.cycles = 11;
            out.mnemonic = buffer;
        } else if ((opcode & 0xCF) == 0x0B) {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "dec %s", rp_name((opcode >> 4) & 0x03));
            out.cycles = 6;
            out.mnemonic = buffer;
        } else if ((opcode & 0xC7) == 0x04) {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "inc %s", reg_name((opcode >> 3) & 0x07));
            out.cycles = ((opcode >> 3) & 0x07) == 6 ? 11 : 4;
            out.mnemonic = buffer;
        } else if ((opcode & 0xC7) == 0x05) {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "dec %s", reg_name((opcode >> 3) & 0x07));
            out.cycles = ((opcode >> 3) & 0x07) == 6 ? 11 : 4;
            out.mnemonic = buffer;
        } else if ((opcode & 0xC7) == 0x06) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "ld %s,$%02X", reg_name((opcode >> 3) & 0x07),
                memory[static_cast<u16>(pc + 1)]);
            out.size = 2;
            out.cycles = ((opcode >> 3) & 0x07) == 6 ? 10 : 7;
            out.mnemonic = buffer;
        } else if ((opcode & 0xC7) == 0xC0) {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "ret %s", condition_name((opcode >> 3) & 0x07));
            out.cycles = 11;
            out.mnemonic = buffer;
        } else if ((opcode & 0xC7) == 0xC2) {
            char buffer[40];
            const u16 target = make_u16(memory[static_cast<u16>(pc + 1)], memory[static_cast<u16>(pc + 2)]);
            std::snprintf(buffer, sizeof(buffer), "jp %s,$%04X", condition_name((opcode >> 3) & 0x07), target);
            out.size = 3;
            out.cycles = 10;
            out.mnemonic = buffer;
        } else if ((opcode & 0xC7) == 0xC4) {
            char buffer[40];
            const u16 target = make_u16(memory[static_cast<u16>(pc + 1)], memory[static_cast<u16>(pc + 2)]);
            std::snprintf(buffer, sizeof(buffer), "call %s,$%04X", condition_name((opcode >> 3) & 0x07), target);
            out.size = 3;
            out.cycles = 17;
            out.mnemonic = buffer;
        } else if ((opcode & 0xCF) == 0xC1) {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "pop %s", qq_name((opcode >> 4) & 0x03));
            out.cycles = 10;
            out.mnemonic = buffer;
        } else if ((opcode & 0xCF) == 0xC5) {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "push %s", qq_name((opcode >> 4) & 0x03));
            out.cycles = 11;
            out.mnemonic = buffer;
        } else if ((opcode & 0xC7) == 0xC7) {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "rst $%02X", opcode & 0x38);
            out.cycles = 11;
            out.mnemonic = buffer;
        } else if ((opcode & 0xC0) == 0x40) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "ld %s,%s", reg_name((opcode >> 3) & 0x07), reg_name(opcode & 0x07));
            out.cycles = ((opcode & 0x07) == 6 || ((opcode >> 3) & 0x07) == 6) ? 7 : 4;
            out.mnemonic = buffer;
        } else if ((opcode & 0xC0) == 0x80) {
            char buffer[32];
            static constexpr const char* ops[] = {"add a", "adc a", "sub", "sbc a", "and", "xor", "or", "cp"};
            std::snprintf(buffer, sizeof(buffer), "%s,%s", ops[(opcode >> 3) & 0x07], reg_name(opcode & 0x07));
            out.cycles = (opcode & 0x07) == 6 ? 7 : 4;
            out.mnemonic = buffer;
        }
        break;
    }
    return out;
}

void dump_z80_state(std::ostream& out, const Z80State& cpu) {
    const auto flags = out.flags();
    const auto fill = out.fill();
    out << std::hex << std::uppercase << std::setfill('0')
        << "PC=" << std::setw(4) << cpu.pc
        << " LAST=" << std::setw(4) << cpu.last_pc
        << " SP=" << std::setw(4) << cpu.sp
        << " AF=" << std::setw(4) << cpu.af()
        << " BC=" << std::setw(4) << cpu.bc()
        << " DE=" << std::setw(4) << cpu.de()
        << " HL=" << std::setw(4) << cpu.hl()
        << " IX=" << std::setw(4) << make_u16(cpu.ixl, cpu.ixh)
        << " IY=" << std::setw(4) << make_u16(cpu.iyl, cpu.iyh)
        << " I=" << std::setw(2) << static_cast<int>(cpu.i)
        << " R=" << std::setw(2) << static_cast<int>(cpu.r)
        << " IM=" << std::dec << static_cast<int>(cpu.interrupt_mode)
        << " IFF=" << cpu.iff1 << "/" << cpu.iff2
        << " CYC=" << cpu.cycles;
    out.flags(flags);
    out.fill(fill);
}

bool service_maskable_interrupt(Z80State& cpu, Bus& bus) {
    if (!cpu.iff1 || cpu.ei_pending) {
        return false;
    }

    cpu.halted = false;
    cpu.iff1 = false;
    cpu.iff2 = false;
    push16(cpu, bus, cpu.pc);

    if (cpu.interrupt_mode == 2) {
        cpu.pc = read16(bus, static_cast<u16>((static_cast<u16>(cpu.i) << 8) | 0xFF));
    } else {
        cpu.pc = 0x0038;
    }

    cpu.cycles += 13;
    return true;
}

void service_non_maskable_interrupt(Z80State& cpu, Bus& bus) {
    cpu.halted = false;
    cpu.ei_pending = false;
    cpu.iff2 = cpu.iff1;
    cpu.iff1 = false;
    push16(cpu, bus, cpu.pc);
    cpu.pc = 0x0066;
    cpu.cycles += 11;
}

void execute_one(Z80State& cpu, Bus& bus) {
    if (cpu.halted) {
        if (cpu.ei_pending) {
            cpu.iff1 = true;
            cpu.iff2 = true;
            cpu.ei_pending = false;
        }
        increment_refresh(cpu);
        cpu.cycles += 4;
        return;
    }

    if (cpu.ei_pending) {
        cpu.iff1 = true;
        cpu.iff2 = true;
        cpu.ei_pending = false;
    }

    const u8 opcode = fetch8(cpu, bus);
    cpu.last_pc = static_cast<u16>(cpu.pc - 1);
    increment_refresh(cpu);

    switch (opcode) {
    case 0x00: cpu.cycles += 4; break;
    case 0x02:
        bus.write(cpu.bc(), cpu.a);
        cpu.cycles += 7;
        break;
    case 0x07: {
        const bool carry = (cpu.a & 0x80) != 0;
        cpu.a = static_cast<u8>((cpu.a << 1) | (carry ? 1 : 0));
        cpu.f = static_cast<u8>((cpu.f & (flag_s | flag_z | flag_pv)) |
            (cpu.a & (flag_y | flag_x)) | (carry ? flag_c : 0));
        cpu.cycles += 4;
        break;
    }
    case 0x08:
        std::swap(cpu.a, cpu.a_alt);
        std::swap(cpu.f, cpu.f_alt);
        cpu.cycles += 4;
        break;
    case 0x0A:
        cpu.a = bus.read(cpu.bc());
        cpu.cycles += 7;
        break;
    case 0x0F: {
        const bool carry = (cpu.a & 0x01) != 0;
        cpu.a = static_cast<u8>((cpu.a >> 1) | (carry ? 0x80 : 0));
        cpu.f = static_cast<u8>((cpu.f & (flag_s | flag_z | flag_pv)) |
            (cpu.a & (flag_y | flag_x)) | (carry ? flag_c : 0));
        cpu.cycles += 4;
        break;
    }
    case 0x10: {
        const s8 displacement = static_cast<s8>(fetch8(cpu, bus));
        cpu.b = static_cast<u8>(cpu.b - 1);
        if (cpu.b != 0) {
            relative_jump(cpu, displacement);
            cpu.cycles += 13;
        } else {
            cpu.cycles += 8;
        }
        break;
    }
    case 0x12:
        bus.write(cpu.de(), cpu.a);
        cpu.cycles += 7;
        break;
    case 0x17: {
        const bool carry = (cpu.a & 0x80) != 0;
        cpu.a = static_cast<u8>((cpu.a << 1) | (cpu.f & flag_c));
        cpu.f = static_cast<u8>((cpu.f & (flag_s | flag_z | flag_pv)) |
            (cpu.a & (flag_y | flag_x)) | (carry ? flag_c : 0));
        cpu.cycles += 4;
        break;
    }
    case 0x18: relative_jump(cpu, static_cast<s8>(fetch8(cpu, bus))); cpu.cycles += 12; break;
    case 0x1A:
        cpu.a = bus.read(cpu.de());
        cpu.cycles += 7;
        break;
    case 0x1F: {
        const bool carry = (cpu.a & 0x01) != 0;
        cpu.a = static_cast<u8>((cpu.a >> 1) | ((cpu.f & flag_c) ? 0x80 : 0));
        cpu.f = static_cast<u8>((cpu.f & (flag_s | flag_z | flag_pv)) |
            (cpu.a & (flag_y | flag_x)) | (carry ? flag_c : 0));
        cpu.cycles += 4;
        break;
    }
    case 0x20: {
        const s8 displacement = static_cast<s8>(fetch8(cpu, bus));
        if ((cpu.f & flag_z) == 0) {
            relative_jump(cpu, displacement);
            cpu.cycles += 12;
        } else {
            cpu.cycles += 7;
        }
        break;
    }
    case 0x22:
        write16(bus, fetch16(cpu, bus), cpu.hl());
        cpu.cycles += 16;
        break;
    case 0x28: {
        const s8 displacement = static_cast<s8>(fetch8(cpu, bus));
        if ((cpu.f & flag_z) != 0) {
            relative_jump(cpu, displacement);
            cpu.cycles += 12;
        } else {
            cpu.cycles += 7;
        }
        break;
    }
    case 0x2A:
        cpu.set_hl(read16(bus, fetch16(cpu, bus)));
        cpu.cycles += 16;
        break;
    case 0x27:
        daa(cpu);
        cpu.cycles += 4;
        break;
    case 0x2F:
        cpu.a = static_cast<u8>(~cpu.a);
        cpu.f = static_cast<u8>((cpu.f & (flag_s | flag_z | flag_pv | flag_c)) |
            (cpu.a & (flag_y | flag_x)) | flag_h | flag_n);
        cpu.cycles += 4;
        break;
    case 0x30: {
        const s8 displacement = static_cast<s8>(fetch8(cpu, bus));
        if ((cpu.f & flag_c) == 0) {
            relative_jump(cpu, displacement);
            cpu.cycles += 12;
        } else {
            cpu.cycles += 7;
        }
        break;
    }
    case 0x32: bus.write(fetch16(cpu, bus), cpu.a); cpu.cycles += 13; break;
    case 0x38: {
        const s8 displacement = static_cast<s8>(fetch8(cpu, bus));
        if ((cpu.f & flag_c) != 0) {
            relative_jump(cpu, displacement);
            cpu.cycles += 12;
        } else {
            cpu.cycles += 7;
        }
        break;
    }
    case 0x37:
        cpu.f = static_cast<u8>((cpu.f & (flag_s | flag_z | flag_pv)) |
            (cpu.a & (flag_y | flag_x)) | flag_c);
        cpu.cycles += 4;
        break;
    case 0x3F: {
        const bool old_carry = (cpu.f & flag_c) != 0;
        cpu.f = static_cast<u8>((cpu.f & (flag_s | flag_z | flag_pv)) |
            (cpu.a & (flag_y | flag_x)) |
            (old_carry ? flag_h : 0) |
            (old_carry ? 0 : flag_c));
        cpu.cycles += 4;
        break;
    }
    case 0x3A: cpu.a = bus.read(fetch16(cpu, bus)); cpu.cycles += 13; break;
    case 0x76: cpu.halted = true; cpu.cycles += 4; break;
    case 0xC3: cpu.pc = fetch16(cpu, bus); cpu.cycles += 10; break;
    case 0xC6: cpu.a = add8(cpu, cpu.a, fetch8(cpu, bus)); cpu.cycles += 7; break;
    case 0xC9: cpu.pc = pop16(cpu, bus); cpu.cycles += 10; break;
    case 0xCB:
        increment_refresh(cpu);
        execute_cb(cpu, bus, fetch8(cpu, bus));
        break;
    case 0xCD: {
        const u16 target = fetch16(cpu, bus);
        push16(cpu, bus, cpu.pc);
        cpu.pc = target;
        cpu.cycles += 17;
        break;
    }
    case 0xCE: cpu.a = adc8(cpu, cpu.a, fetch8(cpu, bus)); cpu.cycles += 7; break;
    case 0xD3: bus.output(fetch8(cpu, bus), cpu.a); cpu.cycles += 11; break;
    case 0xD6: cpu.a = sub8(cpu, cpu.a, fetch8(cpu, bus)); cpu.cycles += 7; break;
    case 0xDB: cpu.a = bus.input(fetch8(cpu, bus)); cpu.cycles += 11; break;
    case 0xD9:
        std::swap(cpu.b, cpu.b_alt);
        std::swap(cpu.c, cpu.c_alt);
        std::swap(cpu.d, cpu.d_alt);
        std::swap(cpu.e, cpu.e_alt);
        std::swap(cpu.h, cpu.h_alt);
        std::swap(cpu.l, cpu.l_alt);
        cpu.cycles += 4;
        break;
    case 0xDD:
        increment_refresh(cpu);
        execute_index(cpu, bus, false, fetch8(cpu, bus));
        break;
    case 0xDE: cpu.a = sbc8(cpu, cpu.a, fetch8(cpu, bus)); cpu.cycles += 7; break;
    case 0xED:
        increment_refresh(cpu);
        execute_ed(cpu, bus, fetch8(cpu, bus));
        break;
    case 0xE3: {
        const u16 stack_value = make_u16(bus.read(cpu.sp), bus.read(static_cast<u16>(cpu.sp + 1)));
        bus.write(cpu.sp, cpu.l);
        bus.write(static_cast<u16>(cpu.sp + 1), cpu.h);
        cpu.set_hl(stack_value);
        cpu.cycles += 19;
        break;
    }
    case 0xE6: cpu.a = and8(cpu, cpu.a, fetch8(cpu, bus)); cpu.cycles += 7; break;
    case 0xE9:
        cpu.pc = cpu.hl();
        cpu.cycles += 4;
        break;
    case 0xEB: {
        const u16 de = cpu.de();
        cpu.set_de(cpu.hl());
        cpu.set_hl(de);
        cpu.cycles += 4;
        break;
    }
    case 0xEE: cpu.a = xor8(cpu, cpu.a, fetch8(cpu, bus)); cpu.cycles += 7; break;
    case 0xF3:
        cpu.iff1 = false;
        cpu.iff2 = false;
        cpu.ei_pending = false;
        cpu.cycles += 4;
        break;
    case 0xFD:
        increment_refresh(cpu);
        execute_index(cpu, bus, true, fetch8(cpu, bus));
        break;
    case 0xF6: cpu.a = or8(cpu, cpu.a, fetch8(cpu, bus)); cpu.cycles += 7; break;
    case 0xF9:
        cpu.sp = cpu.hl();
        cpu.cycles += 6;
        break;
    case 0xFB:
        cpu.ei_pending = true;
        cpu.cycles += 4;
        break;
    case 0xFE: {
        const u8 rhs = fetch8(cpu, bus);
        (void)sub8(cpu, cpu.a, rhs);
        cpu.cycles += 7;
        break;
    }
    default:
        if ((opcode & 0xCF) == 0x01) {
            write_rp(cpu, static_cast<u8>((opcode >> 4) & 0x03), fetch16(cpu, bus));
            cpu.cycles += 10;
            break;
        }
        if ((opcode & 0xCF) == 0x03) {
            const u8 rp = static_cast<u8>((opcode >> 4) & 0x03);
            write_rp(cpu, rp, static_cast<u16>(read_rp(cpu, rp) + 1));
            cpu.cycles += 6;
            break;
        }
        if ((opcode & 0xCF) == 0x09) {
            add_hl(cpu, read_rp(cpu, static_cast<u8>((opcode >> 4) & 0x03)));
            cpu.cycles += 11;
            break;
        }
        if ((opcode & 0xCF) == 0x0B) {
            const u8 rp = static_cast<u8>((opcode >> 4) & 0x03);
            write_rp(cpu, rp, static_cast<u16>(read_rp(cpu, rp) - 1));
            cpu.cycles += 6;
            break;
        }
        if ((opcode & 0xC7) == 0x04) {
            const u8 reg = static_cast<u8>((opcode >> 3) & 0x07);
            write_reg(cpu, bus, reg, inc8(cpu, read_reg(cpu, bus, reg)));
            cpu.cycles += reg == 6 ? 11 : 4;
            break;
        }
        if ((opcode & 0xC7) == 0x05) {
            const u8 reg = static_cast<u8>((opcode >> 3) & 0x07);
            write_reg(cpu, bus, reg, dec8(cpu, read_reg(cpu, bus, reg)));
            cpu.cycles += reg == 6 ? 11 : 4;
            break;
        }
        if ((opcode & 0xC7) == 0x06) {
            const u8 reg = static_cast<u8>((opcode >> 3) & 0x07);
            write_reg(cpu, bus, reg, fetch8(cpu, bus));
            cpu.cycles += reg == 6 ? 10 : 7;
            break;
        }
        if ((opcode & 0xC7) == 0xC0) {
            if (condition(cpu, static_cast<u8>((opcode >> 3) & 0x07))) {
                cpu.pc = pop16(cpu, bus);
                cpu.cycles += 11;
            } else {
                cpu.cycles += 5;
            }
            break;
        }
        if ((opcode & 0xC7) == 0xC2) {
            const u16 target = fetch16(cpu, bus);
            if (condition(cpu, static_cast<u8>((opcode >> 3) & 0x07))) {
                cpu.pc = target;
            }
            cpu.cycles += 10;
            break;
        }
        if ((opcode & 0xC7) == 0xC4) {
            const u16 target = fetch16(cpu, bus);
            if (condition(cpu, static_cast<u8>((opcode >> 3) & 0x07))) {
                push16(cpu, bus, cpu.pc);
                cpu.pc = target;
                cpu.cycles += 17;
            } else {
                cpu.cycles += 10;
            }
            break;
        }
        if ((opcode & 0xCF) == 0xC1) {
            write_qq(cpu, static_cast<u8>((opcode >> 4) & 0x03), pop16(cpu, bus));
            cpu.cycles += 10;
            break;
        }
        if ((opcode & 0xCF) == 0xC5) {
            push16(cpu, bus, read_qq(cpu, static_cast<u8>((opcode >> 4) & 0x03)));
            cpu.cycles += 11;
            break;
        }
        if ((opcode & 0xC7) == 0xC7) {
            push16(cpu, bus, cpu.pc);
            cpu.pc = static_cast<u16>(opcode & 0x38);
            cpu.cycles += 11;
            break;
        }
        if ((opcode & 0xC0) == 0x40) {
            const u8 src = read_reg(cpu, bus, opcode & 0x07);
            write_reg(cpu, bus, static_cast<u8>((opcode >> 3) & 0x07), src);
            cpu.cycles += ((opcode & 0x07) == 6 || ((opcode >> 3) & 0x07) == 6) ? 7 : 4;
            break;
        }
        if ((opcode & 0xC0) == 0x80) {
            const u8 rhs = read_reg(cpu, bus, opcode & 0x07);
            switch ((opcode >> 3) & 0x07) {
            case 0: cpu.a = add8(cpu, cpu.a, rhs); break;
            case 1: cpu.a = adc8(cpu, cpu.a, rhs); break;
            case 2: cpu.a = sub8(cpu, cpu.a, rhs); break;
            case 3: cpu.a = sbc8(cpu, cpu.a, rhs); break;
            case 4: cpu.a = and8(cpu, cpu.a, rhs); break;
            case 5: cpu.a = xor8(cpu, cpu.a, rhs); break;
            case 6: cpu.a = or8(cpu, cpu.a, rhs); break;
            case 7: (void)sub8(cpu, cpu.a, rhs); break;
            }
            cpu.cycles += (opcode & 0x07) == 6 ? 7 : 4;
            break;
        }
        throw std::runtime_error("Z80 opcode not implemented in fallback interpreter");
    }
}

} // namespace sgrecomp
