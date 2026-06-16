#include "sgrecomp/bus.h"
#include "sgrecomp/joypad.h"
#include "sgrecomp/psg.h"
#include "sgrecomp/vdp.h"
#include "sgrecomp/z80.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <bitset>
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
    std::filesystem::path dump_frame;
    std::filesystem::path dump_vram;
    std::filesystem::path dump_cram;
    std::filesystem::path dump_sram;
    std::filesystem::path dump_coverage;
    std::filesystem::path load_sram;
    std::filesystem::path save_sram;
    std::filesystem::path output = "recompiled_rom.cpp";
    ConsoleModel model = ConsoleModel::SMS;
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

std::vector<u8> normalize_rom_payload(std::vector<u8> rom) {
    if (rom.size() > 512 && (rom.size() % 0x4000) == 512) {
        rom.erase(rom.begin(), rom.begin() + 512);
    }
    return rom;
}

void print_usage() {
    std::cout << "usage: sgrecomp <rom.sms|rom.sg> [-o generated.cpp] [--model sms|sg3000] [--disasm] [--bios bios.sms]\n"
              << "       sgrecomp <rom.sms|rom.sg> --run-smoke [--steps n] [--trace] [--bios bios.sms]\n"
              << "                [--dump-frame frame.ppm] [--dump-vram vram.bin] [--dump-cram cram.bin]\n"
              << "                [--load-sram save.sav] [--save-sram save.sav] [--dump-sram sram.bin]\n"
              << "                [--dump-coverage pcs.csv]\n";
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
        if (arg == "--dump-frame" && i + 1 < argc) {
            opts.dump_frame = argv[++i];
            continue;
        }
        if (arg == "--dump-vram" && i + 1 < argc) {
            opts.dump_vram = argv[++i];
            continue;
        }
        if (arg == "--dump-cram" && i + 1 < argc) {
            opts.dump_cram = argv[++i];
            continue;
        }
        if (arg == "--dump-sram" && i + 1 < argc) {
            opts.dump_sram = argv[++i];
            continue;
        }
        if (arg == "--dump-coverage" && i + 1 < argc) {
            opts.dump_coverage = argv[++i];
            continue;
        }
        if (arg == "--load-sram" && i + 1 < argc) {
            opts.load_sram = argv[++i];
            continue;
        }
        if (arg == "--save-sram" && i + 1 < argc) {
            opts.save_sram = argv[++i];
            continue;
        }
        if (arg == "--model" && i + 1 < argc) {
            const std::string model = argv[++i];
            if (model == "sms") {
                opts.model = ConsoleModel::SMS;
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

void write_frame_ppm(const std::filesystem::path& path, const std::array<u32, Vdp::width * Vdp::height>& framebuffer) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open frame output file");
    }

    out << "P6\n" << Vdp::width << " " << Vdp::height << "\n255\n";
    for (const u32 pixel : framebuffer) {
        const char rgb[3] = {
            static_cast<char>((pixel >> 16) & 0xFF),
            static_cast<char>((pixel >> 8) & 0xFF),
            static_cast<char>(pixel & 0xFF),
        };
        out.write(rgb, sizeof(rgb));
    }
}

template <typename Container>
void write_binary_dump(const std::filesystem::path& path, const Container& bytes) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open dump output file");
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void write_coverage_csv(
    const std::filesystem::path& path,
    const std::array<u32, 0x10000>& pc_counts,
    const std::array<u8, 0x10000>& image) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open coverage output file");
    }

    out << "pc,count,opcode,mnemonic\n";
    for (std::size_t pc = 0; pc < pc_counts.size(); ++pc) {
        if (pc_counts[pc] == 0) {
            continue;
        }
        const auto decoded = decode_z80(image, static_cast<u16>(pc));
        out << "0x" << std::hex << std::setw(4) << std::setfill('0') << pc
            << std::dec << "," << pc_counts[pc]
            << ",0x" << std::hex << std::setw(2) << static_cast<int>(decoded.opcode)
            << std::dec << "," << decoded.mnemonic << "\n";
    }
}

void run_smoke(ConsoleModel model, const std::vector<u8>& rom, const std::vector<u8>* bios, std::size_t max_steps, bool trace, const Options& opts) {
    Vdp vdp;
    Psg psg;
    Joypad joypad;
    Bus bus(model, vdp, psg, joypad);
    Z80State cpu;
    if (bios != nullptr) {
        bus.load_bios(*bios);
    }
    bus.load_rom(rom);
    if (!opts.load_sram.empty()) {
        bus.load_cartridge_ram(read_file(opts.load_sram));
    }

    const auto& image = bus.debug_memory();
    std::bitset<0x10000> visited_pc;
    std::array<u32, 0x10000> pc_counts{};
    const auto print_runtime_summary = [&]() {
        const auto& framebuffer = vdp.framebuffer();
        const auto lit_pixels = std::count_if(framebuffer.begin(), framebuffer.end(), [](u32 pixel) {
            return (pixel & 0x00FFFFFF) != 0;
        });
        const auto audio = psg.sample();
        std::cout << "visited pcs: " << visited_pc.count()
                  << "\nframebuffer lit pixels: " << lit_pixels
                  << "\npsg sample: " << std::fixed << std::setprecision(4)
                  << audio[0] << "," << audio[1] << "\n";
        if (!opts.dump_frame.empty()) {
            write_frame_ppm(opts.dump_frame, framebuffer);
            std::cout << "frame dumped: " << opts.dump_frame.string() << "\n";
        }
        if (!opts.dump_vram.empty()) {
            write_binary_dump(opts.dump_vram, vdp.debug_vram());
            std::cout << "vram dumped: " << opts.dump_vram.string() << "\n";
        }
        if (!opts.dump_cram.empty()) {
            write_binary_dump(opts.dump_cram, vdp.debug_cram());
            std::cout << "cram dumped: " << opts.dump_cram.string() << "\n";
        }
        if (!opts.dump_sram.empty()) {
            write_binary_dump(opts.dump_sram, bus.debug_cartridge_ram());
            std::cout << "sram dumped: " << opts.dump_sram.string() << "\n";
        }
        if (!opts.save_sram.empty()) {
            write_binary_dump(opts.save_sram, bus.debug_cartridge_ram());
            std::cout << "sram saved: " << opts.save_sram.string()
                      << (bus.cartridge_ram_dirty() ? " (dirty)" : " (unchanged)") << "\n";
        }
        if (!opts.dump_coverage.empty()) {
            write_coverage_csv(opts.dump_coverage, pc_counts, image);
            std::cout << "coverage dumped: " << opts.dump_coverage.string() << "\n";
        }
    };
    for (std::size_t step = 0; step < max_steps; ++step) {
        const u16 pc_before = cpu.pc;
        visited_pc.set(pc_before);
        ++pc_counts[pc_before];
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
            print_runtime_summary();
            return;
        }
    }

    std::cout << "step limit reached: " << max_steps << "\n";
    dump_z80_state(std::cout, cpu);
    std::cout << "\n";
    print_runtime_summary();
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
        const auto rom = normalize_rom_payload(read_file(opts.input));
        const std::optional<std::vector<u8>> bios = opts.bios.empty()
            ? std::optional<std::vector<u8>>{}
            : std::optional<std::vector<u8>>{read_file(opts.bios)};
        const auto image = image_for_decode(opts.model, rom, opts.disassemble_only && bios ? &*bios : nullptr);
        const std::size_t limit = std::min<std::size_t>(rom.size(), 0xC000);

        if (opts.disassemble_only) {
            disassemble(image, limit);
        } else if (opts.run_smoke) {
            run_smoke(opts.model, rom, bios ? &*bios : nullptr, opts.max_steps, opts.trace, opts);
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
