#include "sgrecomp/console.h"
#include "sgrecomp/z80.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace sgrecomp;

namespace {

struct ExternalRegisters {
    u16 a;
    u16 f;
    u16 b;
    u16 c;
    u16 d;
    u16 e;
    u16 h;
    u16 l;
    u16 i;
    u16 r;
    u16 ix;
    u16 iy;
    u16 sp;
    u16 pc;
    u16 af_alt;
    u16 bc_alt;
    u16 de_alt;
    u16 hl_alt;
    u16 interrupt_mode;
    u16 iff1;
    u16 iff2;
    u16 ei_pending;
    u16 q;
    u16 memptr;
};

struct ExternalCase {
    const char* name;
    ExternalRegisters initial;
    ExternalRegisters expected;
    u64 cycles;
    std::vector<std::pair<u16, u8>> initial_memory;
    std::vector<std::pair<u16, u8>> expected_memory;
    struct Port {
        u16 address;
        u8 value;
        bool write;
    };
    std::vector<Port> ports;
};

#include "z80_external_vectors.inc"

void load_registers(Z80State& cpu, const ExternalRegisters& state) {
    cpu = {};
    cpu.a = static_cast<u8>(state.a);
    cpu.f = static_cast<u8>(state.f);
    cpu.b = static_cast<u8>(state.b);
    cpu.c = static_cast<u8>(state.c);
    cpu.d = static_cast<u8>(state.d);
    cpu.e = static_cast<u8>(state.e);
    cpu.h = static_cast<u8>(state.h);
    cpu.l = static_cast<u8>(state.l);
    cpu.i = static_cast<u8>(state.i);
    cpu.r = static_cast<u8>(state.r);
    cpu.ixh = static_cast<u8>(state.ix >> 8);
    cpu.ixl = static_cast<u8>(state.ix);
    cpu.iyh = static_cast<u8>(state.iy >> 8);
    cpu.iyl = static_cast<u8>(state.iy);
    cpu.sp = state.sp;
    cpu.pc = state.pc;
    cpu.a_alt = static_cast<u8>(state.af_alt >> 8);
    cpu.f_alt = static_cast<u8>(state.af_alt);
    cpu.b_alt = static_cast<u8>(state.bc_alt >> 8);
    cpu.c_alt = static_cast<u8>(state.bc_alt);
    cpu.d_alt = static_cast<u8>(state.de_alt >> 8);
    cpu.e_alt = static_cast<u8>(state.de_alt);
    cpu.h_alt = static_cast<u8>(state.hl_alt >> 8);
    cpu.l_alt = static_cast<u8>(state.hl_alt);
    cpu.interrupt_mode = static_cast<u8>(state.interrupt_mode);
    cpu.iff1 = state.iff1 != 0;
    cpu.iff2 = state.iff2 != 0;
    // Upstream documents `ei` as an ignorable generator-internal marker, not the architectural IFF delay state.
    cpu.q = static_cast<u8>(state.q);
    cpu.memptr = state.memptr;
}

bool registers_match(const Z80State& cpu, const ExternalRegisters& expected) {
    return cpu.a == expected.a && cpu.f == expected.f && cpu.b == expected.b && cpu.c == expected.c &&
           cpu.d == expected.d && cpu.e == expected.e && cpu.h == expected.h && cpu.l == expected.l &&
           cpu.i == expected.i && cpu.r == expected.r && make_u16(cpu.ixl, cpu.ixh) == expected.ix &&
           make_u16(cpu.iyl, cpu.iyh) == expected.iy && cpu.sp == expected.sp && cpu.pc == expected.pc &&
           make_u16(cpu.f_alt, cpu.a_alt) == expected.af_alt && make_u16(cpu.c_alt, cpu.b_alt) == expected.bc_alt &&
           make_u16(cpu.e_alt, cpu.d_alt) == expected.de_alt && make_u16(cpu.l_alt, cpu.h_alt) == expected.hl_alt &&
           cpu.interrupt_mode == expected.interrupt_mode && cpu.iff1 == (expected.iff1 != 0) &&
           cpu.iff2 == (expected.iff2 != 0);
}

void print_register_difference(const Z80State& cpu, const ExternalRegisters& expected) {
    std::cerr << " actual AF=" << cpu.af() << " BC=" << cpu.bc() << " DE=" << cpu.de() << " HL=" << cpu.hl()
              << " IX=" << make_u16(cpu.ixl, cpu.ixh) << " IY=" << make_u16(cpu.iyl, cpu.iyh) << " SP=" << cpu.sp
              << " PC=" << cpu.pc << " I=" << static_cast<int>(cpu.i) << " R=" << static_cast<int>(cpu.r)
              << " IFF=" << cpu.iff1 << cpu.iff2 << " IM=" << static_cast<int>(cpu.interrupt_mode) << '\n'
              << " expect AF=" << make_u16(static_cast<u8>(expected.f), static_cast<u8>(expected.a))
              << " BC=" << make_u16(static_cast<u8>(expected.c), static_cast<u8>(expected.b))
              << " DE=" << make_u16(static_cast<u8>(expected.e), static_cast<u8>(expected.d))
              << " HL=" << make_u16(static_cast<u8>(expected.l), static_cast<u8>(expected.h)) << " IX=" << expected.ix
              << " IY=" << expected.iy << " SP=" << expected.sp << " PC=" << expected.pc << " I=" << expected.i
              << " R=" << expected.r << " IFF=" << expected.iff1 << expected.iff2 << " IM=" << expected.interrupt_mode
              << '\n';
}

} // namespace

int main() {
    std::size_t failures = 0;
    for (const auto& test : kExternalCases) {
        Console console(ConsoleModel::SMS);
        auto& bus = console.bus();
        bus.set_flat_memory_mode_for_cpu_conformance(true);
        for (const auto [address, value] : test.initial_memory) {
            bus.set_flat_memory_byte_for_cpu_conformance(address, value);
        }
        for (const auto& port : test.ports) {
            if (!port.write) {
                bus.set_flat_io_input_for_cpu_conformance(static_cast<u8>(port.address), port.value);
            }
        }
        auto& cpu = console.cpu();
        load_registers(cpu, test.initial);
        execute_one(cpu, bus);

        bool memory_matches = true;
        for (const auto [address, value] : test.expected_memory) {
            memory_matches = memory_matches && bus.read(address) == value;
        }
        bool ports_match = true;
        for (const auto& port : test.ports) {
            if (port.write) {
                u8 value = 0;
                ports_match = ports_match &&
                              bus.flat_io_output_for_cpu_conformance(static_cast<u8>(port.address), value) &&
                              value == port.value;
            }
        }
        if (!registers_match(cpu, test.expected) || cpu.cycles != test.cycles || !memory_matches || !ports_match) {
            if (failures < 20) {
                std::cerr << test.name << ": registers=" << registers_match(cpu, test.expected)
                          << " cycles=" << cpu.cycles << "/" << test.cycles << " memory=" << memory_matches
                          << " ports=" << ports_match << '\n';
                print_register_difference(cpu, test.expected);
            }
            ++failures;
        }
    }
    std::cout << "SingleStepTests/z80 cases: " << kExternalCases.size() << ", failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
