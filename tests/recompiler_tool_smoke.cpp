#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef SGRECOMP_TOOL_PATH
#error "SGRECOMP_TOOL_PATH must be provided by CMake"
#endif

#ifndef SGRECOMP_TEST_OUTPUT_DIR
#error "SGRECOMP_TEST_OUTPUT_DIR must be provided by CMake"
#endif

#ifndef SGRECOMP_SOURCE_DIR
#error "SGRECOMP_SOURCE_DIR must be provided by CMake"
#endif

#ifndef SGRECOMP_CXX_COMPILER
#error "SGRECOMP_CXX_COMPILER must be provided by CMake"
#endif

#ifndef SGRECOMP_CXX_COMPILER_ARG1
#define SGRECOMP_CXX_COMPILER_ARG1 ""
#endif

#ifndef SGRECOMP_CXX_COMPILER_ID
#define SGRECOMP_CXX_COMPILER_ID ""
#endif

namespace {

std::string quote(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

std::string quote_arg(const std::string& arg) {
    return "\"" + arg + "\"";
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path);
    assert(in);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<unsigned char> read_binary(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    assert(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_binary(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream out(path, std::ios::binary);
    assert(out);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

int run_command(const std::string& command) {
#ifdef _WIN32
    const std::string wrapped = "cmd /C call " + command;
#else
    const std::string wrapped = command;
#endif
    const int result = std::system(wrapped.c_str());
    if (result != 0) {
        std::cerr << "command failed: " << wrapped << "\n";
    }
    return result;
}

std::string compiler_command_prefix() {
    std::string command = quote_arg(SGRECOMP_CXX_COMPILER);
    const std::string arg1 = SGRECOMP_CXX_COMPILER_ARG1;
    if (!arg1.empty()) {
        command += " " + arg1;
    }
    return command;
}

void compile_generated_cpp(const std::filesystem::path& generated, const std::filesystem::path& object) {
    const std::filesystem::path include_dir = std::filesystem::path(SGRECOMP_SOURCE_DIR) / "include";
    std::string command = compiler_command_prefix();
    const std::string compiler_id = SGRECOMP_CXX_COMPILER_ID;

    if (compiler_id == "MSVC") {
        command += " /nologo /std:c++20 /I" + quote(include_dir) + " /c " + quote(generated) + " /Fo" + quote(object);
    } else {
        command += " -std=c++20 -I" + quote(include_dir) + " -c " + quote(generated) + " -o " + quote(object);
    }

    const int result = run_command(command);
    assert(result == 0);
}

} // namespace

int main() {
    const std::filesystem::path output_dir = SGRECOMP_TEST_OUTPUT_DIR;
    std::filesystem::create_directories(output_dir);

    const std::filesystem::path rom_path = output_dir / "fixture.sms";
    const std::filesystem::path generated_path = output_dir / "generated_fixture.cpp";
    const std::filesystem::path analysis_path = output_dir / "analysis.txt";
    const std::filesystem::path audio_rom_path = output_dir / "audio_fixture.sms";
    const std::filesystem::path frame_bmp_path = output_dir / "frame_fixture.bmp";
    const std::filesystem::path audio_path = output_dir / "audio_fixture.wav";
    const std::filesystem::path vgm_path = output_dir / "audio_fixture.vgm";
    const std::filesystem::path host_frame_path = output_dir / "host_frame.bmp";
    const std::filesystem::path host_audio_path = output_dir / "host_audio.wav";
    const std::filesystem::path object_path = output_dir / "generated_fixture.obj";

    const std::vector<unsigned char> rom = {
        0x31, 0x00, 0xC1,       // ld sp,$c100
        0x21, 0x00, 0xC0,       // ld hl,$c000
        0x3E, 0x5A,             // ld a,$5a
        0x77,                   // ld (hl),a
        0x7E,                   // ld a,(hl)
        0x32, 0x10, 0xC0,       // ld ($c010),a
        0x3A, 0x10, 0xC0,       // ld a,($c010)
        0x18, 0x01,             // jr +1
        0x00,                   // skipped nop
        0x76,                   // halt
        0xC5,                   // push bc
        0xD1,                   // pop de
        0xCD, 0x20, 0x00,       // call $0020
        0xC9,                   // ret
        0xC2, 0x20, 0x00,       // jp nz,$0020
        0xC0,                   // ret nz
        0xE9,                   // jp (hl)
        0x22, 0x00, 0xC0,       // ld ($c000),hl
        0x2A, 0x00, 0xC0,       // ld hl,($c000)
        0xF3,                   // di
        0xED, 0x56,             // im 1
        0xFB,                   // ei
        0xC7,                   // rst $00
        0x23,                   // inc hl
        0x13,                   // inc de
        0x0C,                   // inc c
        0xB7,                   // or a
        0xC6, 0x01,             // add a,$01
        0xD3, 0xBF,             // out ($bf),a
        0x10, 0xFC,             // djnz -4
        0xED, 0x43, 0x00, 0xC0, // ld ($c000),bc
        0xED, 0x4B, 0x00, 0xC0, // ld bc,($c000)
        0xED, 0xA0,             // ldi
        0xED, 0xB0,             // ldir
        0x08,                   // ex af,af'
        0x17,                   // rla
        0x19,                   // add hl,de
        0xDB, 0xDD,             // in a,($dd)
        0xD9,                   // exx
        0xDD, 0xE1,             // pop ix
        0xDD, 0xE5,             // push ix
        0xFD, 0xE1,             // pop iy
        0xFD, 0xE5,             // push iy
    };
    write_binary(rom_path, rom);

    const std::string generate_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(rom_path)
        + " -o " + quote(generated_path) + " --dump-analysis " + quote(analysis_path);
    assert(run_command(generate_command) == 0);

    const std::string generated = read_text(generated_path);
    assert(contains(generated, "cpu.set_hl(0xc000)"));
    assert(contains(generated, "/* ld hl,"));
    assert(contains(generated, "bus.write(cpu.hl(), cpu.a);"));
    assert(contains(generated, "cpu.a = bus.read(cpu.hl())"));
    assert(contains(generated, "bus.write(0xc010, cpu.a);"));
    assert(contains(generated, "cpu.a = bus.read(0xc010);"));
    assert(contains(generated, "cpu.pc = 0x0013;"));
    assert(contains(generated, "cpu.set_de(sgrecomp::make_u16(lo, hi));"));
    assert(contains(generated, "cpu.pc = 0x0020; cpu.cycles += 17;"));
    assert(contains(generated, "if ((cpu.f & 0x40) == 0) { cpu.pc = 0x0020;"));
    assert(contains(generated, "cpu.pc = cpu.hl();"));
    assert(contains(generated, "bus.write(0xc000, cpu.l); bus.write(0xc001, cpu.h);"));
    assert(contains(generated, "cpu.l = bus.read(0xc000); cpu.h = bus.read(0xc001);"));
    assert(contains(generated, "cpu.iff1 = false; cpu.iff2 = false; cpu.ei_pending = false;"));
    assert(contains(generated, "cpu.interrupt_mode = 1; cpu.pc = 0x0028;"));
    assert(contains(generated, "cpu.ei_pending = true; cpu.pc = 0x0029;"));
    assert(contains(generated, "cpu.pc = 0x0000; cpu.cycles += 11;"));
    assert(contains(generated, "cpu.set_hl(static_cast<sgrecomp::u16>(cpu.hl() + 1));"));
    assert(contains(generated, "cpu.c = sgrecomp_inc8(cpu, cpu.c);"));
    assert(contains(generated, "cpu.a = sgrecomp_or8(cpu, cpu.a, cpu.a);"));
    assert(contains(generated, "cpu.a = sgrecomp_add8(cpu, cpu.a, 0x01);"));
    assert(contains(generated, "bus.output(0xbf, cpu.a);"));
    assert(contains(generated, "cpu.b = static_cast<sgrecomp::u8>(cpu.b - 1);"));
    assert(contains(generated, "const auto value = cpu.bc(); bus.write(0xc000"));
    assert(contains(generated, "cpu.set_bc(sgrecomp::make_u16(lo, hi));"));
    assert(contains(generated, "bus.write(cpu.de(), bus.read(cpu.hl()));"));
    assert(contains(generated, "cpu.a = cpu.a_alt; cpu.f = cpu.f_alt;"));
    assert(contains(generated, "const bool carry = (cpu.a & 0x80) != 0; cpu.a = static_cast<sgrecomp::u8>((cpu.a << 1)"));
    assert(contains(generated, "const auto lhs = cpu.hl(); const auto rhs = cpu.de();"));
    assert(contains(generated, "cpu.a = bus.input(0xdd);"));
    assert(contains(generated, "cpu.b = cpu.b_alt; cpu.c = cpu.c_alt;"));
    assert(contains(generated, "cpu.ixl = lo; cpu.ixh = hi;"));
    assert(contains(generated, "cpu.iyl = lo; cpu.iyh = hi;"));
    assert(contains(generated, "sgrecomp::make_u16(cpu.ixl, cpu.ixh)"));
    assert(contains(generated, "sgrecomp::make_u16(cpu.iyl, cpu.iyh)"));

    const std::string analysis = read_text(analysis_path);
    assert(contains(analysis, "SG3000Recomp static analysis"));
    assert(contains(analysis, "basic_blocks:"));
    assert(contains(analysis, "direct_emit_instructions:"));
    assert(contains(analysis, "fallback_instructions:"));
    assert(contains(analysis, "successors=0x0013"));
    assert(contains(analysis, "successors=none"));

    compile_generated_cpp(generated_path, object_path);
    assert(std::filesystem::exists(object_path));

    const std::vector<unsigned char> audio_rom = {
        0x3E, 0x80, // ld a,$80: tone channel 0 latch
        0xD3, 0x7F, // out ($7f),a
        0x3E, 0x00, // ld a,$00: tone high bits
        0xD3, 0x7F, // out ($7f),a
        0x3E, 0x90, // ld a,$90: channel 0 volume max
        0xD3, 0x7F, // out ($7f),a
        0x18, 0xFE, // jr -2
    };
    write_binary(audio_rom_path, audio_rom);

    const std::string audio_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(audio_rom_path)
        + " --run-smoke --steps 4096 --dump-frame-bmp " + quote(frame_bmp_path)
        + " --dump-audio " + quote(audio_path) + " --dump-vgm " + quote(vgm_path);
    assert(run_command(audio_command) == 0);
    const auto frame_bmp = read_binary(frame_bmp_path);
    assert(frame_bmp.size() > 54);
    assert(frame_bmp[0] == 'B');
    assert(frame_bmp[1] == 'M');
    assert(frame_bmp[10] == 54);
    assert(frame_bmp[18] == 0);
    assert(frame_bmp[19] == 1);
    assert(frame_bmp[22] == 192);

    const std::string wav = read_text(audio_path);
    assert(wav.size() > 44);
    assert(wav.substr(0, 4) == "RIFF");
    assert(wav.substr(8, 4) == "WAVE");
    assert(wav.substr(36, 4) == "data");

    const auto vgm = read_binary(vgm_path);
    assert(vgm.size() > 0x100);
    assert(vgm[0] == 'V');
    assert(vgm[1] == 'g');
    assert(vgm[2] == 'm');
    assert(vgm[3] == ' ');
    assert(vgm[0x100] == 0x50);
    assert(vgm.back() == 0x66);

    const std::string host_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(audio_rom_path)
        + " --run-host --frames 2 --dump-frame-bmp " + quote(host_frame_path)
        + " --dump-audio " + quote(host_audio_path);
    assert(run_command(host_command) == 0);

    const auto host_frame = read_binary(host_frame_path);
    assert(host_frame.size() > 54);
    assert(host_frame[0] == 'B');
    assert(host_frame[1] == 'M');

    const std::string host_wav = read_text(host_audio_path);
    assert(host_wav.size() > 44);
    assert(host_wav.substr(0, 4) == "RIFF");
    assert(host_wav.substr(8, 4) == "WAVE");
}
