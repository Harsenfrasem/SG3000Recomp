#include "sgrecomp/bus.h"
#include "sgrecomp/joypad.h"
#include "sgrecomp/psg.h"
#include "sgrecomp/vdp.h"
#include "sgrecomp/z80.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace sgrecomp;

struct Options {
    std::filesystem::path input;
    std::filesystem::path bios;
    std::filesystem::path output = "recompiled_rom.cpp";
    ConsoleModel model = ConsoleModel::MasterSystem;
    bool disassemble_only = false;
    bool run_smoke = false;
    bool trace = false;
    std::size_t max_steps = 200000;
};

std::vector<u8> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open input file");
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void print_usage() {
    std::cout << "usage: sgrecomp <rom.sms|rom.sg> [-o generated.cpp] [--model sms|sg3000] [--disasm] [--bios bios.sms]\n"
              << "       sgrecomp <rom.sms|rom.sg> --run-smoke [--steps n] [--trace] [--bios bios.sms]\n";
}

Options parse_args(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            std::exit(0);
        }
        if (arg == "-o" && i + 1 < argc) {
            opts.output = argv[++i];
            continue;
        }
        if (arg == "--bios" && i + 1 < argc) {
            opts.bios = argv[++i];
            continue;
        }
        if (arg == "--model" && i + 1 < argc) {
            const std::string model = argv[++i];
            if (model == "sms" || model == "mastersystem") {
                opts.model = ConsoleModel::MasterSystem;
            } else if (model == "sg3000" || model == "sg-3000") {
                opts.model = ConsoleModel::SG3000;
            } else {
                throw std::runtime_error("unknown model: " + model);
            }
            continue;
        }
        if (arg == "--disasm") {
            opts.disassemble_only = true;
            continue;
        }
        if (arg == "--run-smoke") {
            opts.run_smoke = true;
            continue;
        }
        if (arg == "--trace") {
            opts.trace = true;
            continue;
        }
        if (arg == "--steps" && i + 1 < argc) {
            opts.max_steps = static_cast<std::size_t>(std::stoull(argv[++i]));
            continue;
        }
        if (opts.input.empty()) {
            opts.input = arg;
            continue;
        }
        throw std::runtime_error("unexpected argument: " + arg);
    }
    if (opts.input.empty()) {
        throw std::runtime_error("missing input ROM");
    }
    return opts;
}

std::array<u8, 0x10000> image_for_decode(ConsoleModel model, const std::vector<u8>& rom, const std::vector<u8>* bios = nullptr) {
    Vdp vdp;
    Psg psg;
    Joypad joypad;
    Bus bus(model, vdp, psg, joypad);
    if (bios != nullptr) {
        bus.load_bios(*bios);
    }
    bus.load_rom(rom);
    return bus.debug_memory();
}

void disassemble(const std::array<u8, 0x10000>& image, std::size_t limit) {
    u16 pc = 0;
    while (pc < limit && pc < 0xC000) {
        const auto insn = decode_z80(image, pc);
        std::cout << std::hex << std::setw(4) << std::setfill('0') << insn.pc
                  << "  " << std::setw(2) << static_cast<int>(insn.opcode)
                  << "  " << insn.mnemonic << "\n";
        pc = static_cast<u16>(pc + insn.size);
    }
}

void run_smoke(ConsoleModel model, const std::vector<u8>& rom, const std::vector<u8>* bios, std::size_t max_steps, bool trace) {
    Vdp vdp;
    Psg psg;
    Joypad joypad;
    Bus bus(model, vdp, psg, joypad);
    Z80State cpu;
    if (bios != nullptr) {
        bus.load_bios(*bios);
    }
    bus.load_rom(rom);

    const auto& image = bus.debug_memory();
    for (std::size_t step = 0; step < max_steps; ++step) {
        const u16 pc_before = cpu.pc;
        const auto insn = decode_z80(image, pc_before);
        if (trace) {
            std::cout << std::dec << std::setw(8) << step << "  ";
            dump_z80_state(std::cout, cpu);
            std::cout << "  " << insn.mnemonic << "\n";
        }
        try {
            const u64 cycles_before = cpu.cycles;
            execute_one(cpu, bus);
            vdp.tick(static_cast<int>(cpu.cycles - cycles_before));
            if (vdp.irq_pending()) {
                const u64 irq_before = cpu.cycles;
                if (service_maskable_interrupt(cpu, bus)) {
                    vdp.tick(static_cast<int>(cpu.cycles - irq_before));
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "execution stopped at step " << step << ": " << e.what() << "\n";
            std::cerr << "instruction: ";
            std::cerr << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(4) << pc_before << "  "
                      << std::setw(2) << static_cast<int>(insn.opcode) << "  "
                      << insn.mnemonic << "\n";
            dump_z80_state(std::cerr, cpu);
            std::cerr << "\n";
            return;
        }
        if (cpu.halted) {
            std::cout << "halted after " << (step + 1) << " steps\n";
            dump_z80_state(std::cout, cpu);
            std::cout << "\n";
            return;
        }
    }

    std::cout << "step limit reached: " << max_steps << "\n";
    dump_z80_state(std::cout, cpu);
    std::cout << "\n";
}

void emit_case(std::ostream& out, const std::array<u8, 0x10000>& image, u16 pc) {
    const u8 opcode = image[pc];
    out << "    case 0x" << std::hex << std::setw(4) << std::setfill('0') << pc << ": ";
    switch (opcode) {
    case 0x00:
        out << "cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 4; return;\n";
        break;
    case 0x3E:
        out << "cpu.a = 0x" << std::setw(2) << static_cast<int>(image[pc + 1])
            << "; cpu.pc = 0x" << std::setw(4) << (pc + 2) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x06:
        out << "cpu.b = 0x" << std::setw(2) << static_cast<int>(image[pc + 1])
            << "; cpu.pc = 0x" << std::setw(4) << (pc + 2) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x0E:
        out << "cpu.c = 0x" << std::setw(2) << static_cast<int>(image[pc + 1])
            << "; cpu.pc = 0x" << std::setw(4) << (pc + 2) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x18: {
        const auto displacement = static_cast<s8>(image[pc + 1]);
        const u16 target = static_cast<u16>(pc + 2 + displacement);
        out << "cpu.pc = 0x" << std::setw(4) << target << "; cpu.cycles += 12; return;\n";
        break;
    }
    case 0xC3: {
        const u16 target = make_u16(image[pc + 1], image[pc + 2]);
        out << "cpu.pc = 0x" << std::setw(4) << target << "; cpu.cycles += 10; return;\n";
        break;
    }
    case 0x76:
        out << "cpu.halted = true; cpu.pc = 0x" << std::setw(4) << (pc + 1)
            << "; cpu.cycles += 4; return;\n";
        break;
    default:
        out << "sgrecomp::execute_one(cpu, bus); return;\n";
        break;
    }
}

void generate_cpp(const std::filesystem::path& output, const std::array<u8, 0x10000>& image, std::size_t limit) {
    std::ofstream out(output);
    if (!out) {
        throw std::runtime_error("cannot open output file");
    }

    out << "#include \"sgrecomp/bus.h\"\n";
    out << "#include \"sgrecomp/z80.h\"\n\n";
    out << "#include <array>\n\n";
    out << "namespace {\n";
    out << "constexpr std::array<sgrecomp::u8, " << std::dec << limit << "> kRom = {\n";
    for (std::size_t i = 0; i < limit; ++i) {
        if (i % 16 == 0) {
            out << "    ";
        }
        out << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(image[i]);
        if (i + 1 != limit) {
            out << ", ";
        }
        if (i % 16 == 15 || i + 1 == limit) {
            out << "\n";
        }
    }
    out << "};\n";
    out << "} // namespace\n\n";
    out << "extern \"C\" void sgrecomp_load_rom(sgrecomp::Bus& bus) {\n";
    out << "    bus.load_rom(kRom);\n";
    out << "}\n\n";
    out << "extern \"C\" void sgrecomp_run_instruction(sgrecomp::Z80State& cpu, sgrecomp::Bus& bus) {\n";
    out << "    if (cpu.halted) { cpu.cycles += 4; return; }\n";
    out << "    switch (cpu.pc) {\n";
    for (u16 pc = 0; pc < limit && pc < 0xC000;) {
        const auto insn = decode_z80(image, pc);
        emit_case(out, image, pc);
        pc = static_cast<u16>(pc + insn.size);
    }
    out << "    default: sgrecomp::execute_one(cpu, bus); return;\n";
    out << "    }\n";
    out << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options opts = parse_args(argc, argv);
        const auto rom = read_file(opts.input);
        const std::optional<std::vector<u8>> bios = opts.bios.empty()
            ? std::optional<std::vector<u8>>{}
            : std::optional<std::vector<u8>>{read_file(opts.bios)};
        const auto image = image_for_decode(opts.model, rom, opts.disassemble_only && bios ? &*bios : nullptr);
        const std::size_t limit = std::min<std::size_t>(rom.size(), 0xC000);

        if (opts.disassemble_only) {
            disassemble(image, limit);
        } else if (opts.run_smoke) {
            run_smoke(opts.model, rom, bios ? &*bios : nullptr, opts.max_steps, opts.trace);
        } else {
            generate_cpp(opts.output, image, limit);
            std::cout << "generated " << opts.output.string() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "sgrecomp: " << e.what() << "\n";
        print_usage();
        return 1;
    }
    return 0;
}
