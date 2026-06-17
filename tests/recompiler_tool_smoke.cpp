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
    const std::filesystem::path header_rom_path = output_dir / "header_fixture.sms";
    const std::filesystem::path header_analysis_path = output_dir / "header_analysis.txt";
    const std::filesystem::path entry_rom_path = output_dir / "entry_fixture.sms";
    const std::filesystem::path entry_analysis_path = output_dir / "entry_analysis.txt";
    const std::filesystem::path pointer_rom_path = output_dir / "pointer_fixture.sms";
    const std::filesystem::path pointer_analysis_path = output_dir / "pointer_analysis.txt";
    const std::filesystem::path hardware_rom_path = output_dir / "hardware_fixture.sms";
    const std::filesystem::path hardware_analysis_path = output_dir / "hardware_analysis.txt";
    const std::filesystem::path audio_rom_path = output_dir / "audio_fixture.sms";
    const std::filesystem::path frame_bmp_path = output_dir / "frame_fixture.bmp";
    const std::filesystem::path audio_path = output_dir / "audio_fixture.wav";
    const std::filesystem::path vgm_path = output_dir / "audio_fixture.vgm";
    const std::filesystem::path io_log_path = output_dir / "io_fixture.csv";
    const std::filesystem::path tilemap_path = output_dir / "tilemap_fixture.csv";
    const std::filesystem::path sprites_path = output_dir / "sprites_fixture.csv";
    const std::filesystem::path debug_rom_path = output_dir / "debug_fixture.sms";
    const std::filesystem::path memory_log_path = output_dir / "memory_fixture.csv";
    const std::filesystem::path vdp_log_path = output_dir / "vdp_fixture.csv";
    const std::filesystem::path filtered_io_log_path = output_dir / "filtered_io_fixture.csv";
    const std::filesystem::path fm_rom_path = output_dir / "fm_fixture.sms";
    const std::filesystem::path fm_log_path = output_dir / "fm_fixture.csv";
    const std::filesystem::path host_frame_path = output_dir / "host_frame.bmp";
    const std::filesystem::path host_audio_path = output_dir / "host_audio.wav";
    const std::filesystem::path host_sram_rom_path = output_dir / "host_sram_fixture.sms";
    const std::filesystem::path host_sram_path = output_dir / "host_save.sav";
    const std::filesystem::path host_profile_path = output_dir / "host_profiles.txt";
    const std::filesystem::path host_profile_sram_path = output_dir / "host_profile_save.sav";
    const std::filesystem::path state_path = output_dir / "fixture.sgstate";
    const std::filesystem::path state_reload_path = output_dir / "fixture_reload.sgstate";
    const std::filesystem::path state_mismatch_rom_path = output_dir / "state_mismatch.sms";
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
    assert(contains(generated, "void sgrecomp_block_0000"));
    assert(contains(generated, "sgrecomp_block_0000(cpu, bus); return;"));
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
    assert(contains(analysis, "entry_points: 0x0000"));
    assert(contains(analysis, "basic_blocks:"));
    assert(contains(analysis, "header_found: no"));
    assert(contains(analysis, "direct_emit_instructions:"));
    assert(contains(analysis, "fallback_instructions:"));
    assert(contains(analysis, "static_hardware_accesses:"));
    assert(contains(analysis, "in 0xdd joypad"));
    assert(contains(analysis, "successors=0x0013"));
    assert(contains(analysis, "successors=none"));

    compile_generated_cpp(generated_path, object_path);
    assert(std::filesystem::exists(object_path));

    std::vector<unsigned char> header_rom(0x8000, 0x00);
    header_rom[0x0000] = 0x76; // halt
    const std::string magic = "TMR SEGA";
    for (std::size_t i = 0; i < magic.size(); ++i) {
        header_rom[0x7FF0 + i] = static_cast<unsigned char>(magic[i]);
    }
    header_rom[0x7FFA] = 0x34;
    header_rom[0x7FFB] = 0x12;
    header_rom[0x7FFC] = 0x56;
    header_rom[0x7FFD] = 0x78;
    header_rom[0x7FFE] = 0x91; // product nibble + version 1
    header_rom[0x7FFF] = 0x4C; // export, 32 KiB
    write_binary(header_rom_path, header_rom);

    const std::string header_analysis_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(header_rom_path)
        + " --dump-analysis " + quote(header_analysis_path);
    assert(run_command(header_analysis_command) == 0);
    const std::string header_analysis = read_text(header_analysis_path);
    assert(contains(header_analysis, "header_found: yes"));
    assert(contains(header_analysis, "header_offset: 0x7ff0"));
    assert(contains(header_analysis, "header_platform: Master System"));
    assert(contains(header_analysis, "header_region: SMS export"));
    assert(contains(header_analysis, "header_size_code: 32 KiB"));
    assert(contains(header_analysis, "header_checksum_stored: 0x1234"));
    assert(contains(header_analysis, "header_declared_size_bytes: 32768"));
    assert(contains(header_analysis, "header_checksum_declared_size:"));
    assert(contains(header_analysis, "header_checksum_matches_declared_size: no"));

    std::vector<unsigned char> entry_rom(0x80, 0x00);
    entry_rom[0x0000] = 0x76; // halt
    entry_rom[0x0038] = 0x3E; // ld a,$38
    entry_rom[0x0039] = 0x38;
    entry_rom[0x003A] = 0x76; // halt
    entry_rom[0x0066] = 0x3E; // ld a,$66
    entry_rom[0x0067] = 0x66;
    entry_rom[0x0068] = 0x76; // halt
    write_binary(entry_rom_path, entry_rom);

    const std::string entry_analysis_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(entry_rom_path)
        + " --dump-analysis " + quote(entry_analysis_path);
    assert(run_command(entry_analysis_command) == 0);
    const std::string entry_analysis = read_text(entry_analysis_path);
    assert(contains(entry_analysis, "entry_points: 0x0000 0x0038 0x0066"));
    assert(contains(entry_analysis, "block 0x0000"));
    assert(contains(entry_analysis, "block 0x0038"));
    assert(contains(entry_analysis, "block 0x0066"));

    std::vector<unsigned char> pointer_rom(0x100, 0x00);
    pointer_rom[0x0000] = 0x76; // halt
    pointer_rom[0x0038] = 0x76; // halt
    pointer_rom[0x0066] = 0x76; // halt
    pointer_rom[0x007E] = 0xFF; pointer_rom[0x007F] = 0xFF;
    pointer_rom[0x0080] = 0x00; pointer_rom[0x0081] = 0x00;
    pointer_rom[0x0082] = 0x38; pointer_rom[0x0083] = 0x00;
    pointer_rom[0x0084] = 0x66; pointer_rom[0x0085] = 0x00;
    write_binary(pointer_rom_path, pointer_rom);

    const std::string pointer_analysis_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(pointer_rom_path)
        + " --dump-analysis " + quote(pointer_analysis_path);
    assert(run_command(pointer_analysis_command) == 0);
    const std::string pointer_analysis = read_text(pointer_analysis_path);
    assert(contains(pointer_analysis, "pointer_tables: 1"));
    assert(contains(pointer_analysis, "table 0x0080 entries=3 targets=0x0000,0x0038,0x0066"));

    const std::vector<unsigned char> hardware_rom = {
        0x3E, 0x12,       // ld a,$12
        0xD3, 0xBF,       // out ($bf),a
        0xDB, 0xDD,       // in a,($dd)
        0x32, 0xFC, 0xFF, // ld ($fffc),a
        0x76,             // halt
    };
    write_binary(hardware_rom_path, hardware_rom);

    const std::string hardware_analysis_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(hardware_rom_path)
        + " --dump-analysis " + quote(hardware_analysis_path);
    assert(run_command(hardware_analysis_command) == 0);
    const std::string hardware_analysis = read_text(hardware_analysis_path);
    assert(contains(hardware_analysis, "static_hardware_accesses: 3"));
    assert(contains(hardware_analysis, "out 0xbf vdp_control"));
    assert(contains(hardware_analysis, "in 0xdd joypad"));
    assert(contains(hardware_analysis, "mapper_write 0xfffc mapper_register"));

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
        + " --dump-audio " + quote(audio_path) + " --dump-vgm " + quote(vgm_path)
        + " --dump-io-log " + quote(io_log_path)
        + " --dump-tilemap " + quote(tilemap_path)
        + " --dump-sprites " + quote(sprites_path);
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

    const std::string io_log = read_text(io_log_path);
    assert(contains(io_log, "cycle,direction,port,value"));
    assert(contains(io_log, "write,0x7f"));

    const std::string tilemap = read_text(tilemap_path);
    assert(contains(tilemap, "x,y,address,tile,palette,flip_x,flip_y,priority"));
    assert(contains(tilemap, "0,0,0x"));

    const std::string sprites = read_text(sprites_path);
    assert(contains(sprites, "index,raw_y,x,y,tile,terminator"));

    const std::vector<unsigned char> debug_rom = {
        0x3E, 0x5A,       // ld a,$5a
        0x32, 0x00, 0xC0, // ld ($c000),a
        0x3E, 0x40,       // ld a,$40
        0xD3, 0xBF,       // out ($bf),a: register value
        0x3E, 0x81,       // ld a,$81
        0xD3, 0xBF,       // out ($bf),a: register 1
        0x3E, 0x00,       // ld a,$00
        0xD3, 0xBF,       // out ($bf),a: VRAM address low
        0x3E, 0x40,       // ld a,$40
        0xD3, 0xBF,       // out ($bf),a: VRAM write high
        0x3E, 0xAA,       // ld a,$aa
        0xD3, 0xBE,       // out ($be),a: VRAM data
        0x76,             // halt
    };
    write_binary(debug_rom_path, debug_rom);

    const std::string debug_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(debug_rom_path)
        + " --run-smoke --steps 64"
        + " --dump-memory-log " + quote(memory_log_path) + " --watch 0xc000"
        + " --dump-vdp-log " + quote(vdp_log_path) + " --watch-vdp 0x0000-0x0001"
        + " --dump-io-log " + quote(filtered_io_log_path) + " --io-port 0xbe"
        + " --save-state " + quote(state_path);
    assert(run_command(debug_command) == 0);

    const std::string memory_log = read_text(memory_log_path);
    assert(contains(memory_log, "cycle,kind,address,physical,value"));
    assert(contains(memory_log, "ram,0xc000"));

    const std::string vdp_log = read_text(vdp_log_path);
    assert(contains(vdp_log, "cycle,kind,address,value"));
    assert(contains(vdp_log, "vram,0x0000,0xaa"));

    const std::string filtered_io_log = read_text(filtered_io_log_path);
    assert(contains(filtered_io_log, "cycle,direction,port,value"));
    assert(contains(filtered_io_log, "write,0xbe,0xaa"));
    assert(!contains(filtered_io_log, "0xbf"));

    const auto state_bytes = read_binary(state_path);
    assert(state_bytes.size() > 0x10000);
    assert(state_bytes[0] == 'S');
    assert(state_bytes[1] == 'G');
    assert(state_bytes[2] == 'S');
    assert(state_bytes[3] == 'S');

    const std::string reload_state_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(debug_rom_path)
        + " --run-smoke --steps 8 --load-state " + quote(state_path)
        + " --save-state " + quote(state_reload_path);
    assert(run_command(reload_state_command) == 0);
    assert(read_binary(state_reload_path).size() == state_bytes.size());

    std::vector<unsigned char> mismatch_rom = debug_rom;
    mismatch_rom.push_back(0x00);
    write_binary(state_mismatch_rom_path, mismatch_rom);
    const std::string mismatch_state_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(state_mismatch_rom_path)
        + " --run-smoke --steps 8 --load-state " + quote(state_path);
    assert(run_command(mismatch_state_command) != 0);

    const std::string forced_state_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(state_mismatch_rom_path)
        + " --run-smoke --steps 8 --load-state " + quote(state_path) + " --force-state";
    assert(run_command(forced_state_command) == 0);

    const std::vector<unsigned char> fm_rom = {
        0x3E, 0x01, // ld a,$01: enable FM, mute PSG
        0xD3, 0xF2, // out ($f2),a
        0x3E, 0x20, // ld a,$20: channel 0 key/block/fnum high
        0xD3, 0xF0, // out ($f0),a
        0x3E, 0x11, // ld a,$11
        0xD3, 0xF1, // out ($f1),a
        0x76,       // halt
    };
    write_binary(fm_rom_path, fm_rom);

    const std::string fm_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(fm_rom_path)
        + " --run-smoke --enable-fm --steps 64 --dump-fm-log " + quote(fm_log_path);
    assert(run_command(fm_command) == 0);
    const std::string fm_log = read_text(fm_log_path);
    assert(contains(fm_log, "cycle,port,value"));
    assert(contains(fm_log, "0xf2,0x01"));
    assert(contains(fm_log, "0xf0,0x20"));
    assert(contains(fm_log, "0xf1,0x11"));

    const std::string host_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(audio_rom_path)
        + " --run-host --frames 2 --dump-frame-bmp " + quote(host_frame_path)
        + " --audio-sample-rate 22050 --dump-audio " + quote(host_audio_path);
    assert(run_command(host_command) == 0);

    const auto host_frame = read_binary(host_frame_path);
    assert(host_frame.size() > 54);
    assert(host_frame[0] == 'B');
    assert(host_frame[1] == 'M');

    const std::string host_wav = read_text(host_audio_path);
    assert(host_wav.size() > 44);
    assert(host_wav.substr(0, 4) == "RIFF");
    assert(host_wav.substr(8, 4) == "WAVE");
    const auto host_wav_bytes = read_binary(host_audio_path);
    const unsigned sample_rate = static_cast<unsigned>(host_wav_bytes[24])
        | (static_cast<unsigned>(host_wav_bytes[25]) << 8)
        | (static_cast<unsigned>(host_wav_bytes[26]) << 16)
        | (static_cast<unsigned>(host_wav_bytes[27]) << 24);
    assert(sample_rate == 22050);

#ifdef SGRECOMP_HOST_PATH
    std::vector<unsigned char> host_sram_rom(0x10000, 0x00);
    host_sram_rom[0x0000] = 0x3E; // ld a,$08: enable cartridge RAM bank 0
    host_sram_rom[0x0001] = 0x08;
    host_sram_rom[0x0002] = 0x32; // ld ($fffc),a
    host_sram_rom[0x0003] = 0xFC;
    host_sram_rom[0x0004] = 0xFF;
    host_sram_rom[0x0005] = 0x3E; // ld a,$5a
    host_sram_rom[0x0006] = 0x5A;
    host_sram_rom[0x0007] = 0x32; // ld ($8000),a
    host_sram_rom[0x0008] = 0x00;
    host_sram_rom[0x0009] = 0x80;
    host_sram_rom[0x000A] = 0x76; // halt
    write_binary(host_sram_rom_path, host_sram_rom);

    const std::string host_sram_command = quote_arg(SGRECOMP_HOST_PATH) + " " + quote(host_sram_rom_path)
        + " --mute --no-overlay --quit-after-frames 1 --save-sram " + quote(host_sram_path);
    assert(run_command(host_sram_command) == 0);

    const auto host_sram = read_binary(host_sram_path);
    assert(host_sram.size() == 0x8000);
    assert(host_sram[0] == 0x5A);

    write_binary(host_profile_path, std::vector<unsigned char>(
        {'[', 'p', 'r', 'o', 'f', 'i', 'l', 'e', ']', '\n',
         'n', 'a', 'm', 'e', ' ', '=', ' ', '"', 's', 'm', 'o', 'k', 'e', '"', '\n',
         'h', 'a', 's', 'h', ' ', '=', ' ', '"', 'f', 'n', 'v', '1', 'a', '6', '4', ':',
         'd', '3', 'f', '7', '1', '7', '5', '8', '4', 'f', 'a', 'e', '6', 'e', '4', '2', '"', '\n',
         'm', 'o', 'd', 'e', ' ', '=', ' ', '"', 'e', 'n', 'h', 'a', 'n', 'c', 'e', 'd', '"', '\n',
         'r', 'e', 'd', 'u', 'c', 'e', '_', 'f', 'l', 'i', 'c', 'k', 'e', 'r', ' ', '=', ' ', 't', 'r', 'u', 'e', '\n',
         'a', 'u', 'd', 'i', 'o', '_', 'l', 'a', 't', 'e', 'n', 'c', 'y', '_', 'm', 's', ' ', '=', ' ', '1', '2', '0', '\n'}));

    const std::string host_profile_command = quote_arg(SGRECOMP_HOST_PATH) + " " + quote(host_sram_rom_path)
        + " --profile " + quote(host_profile_path)
        + " --mute --no-overlay --quit-after-frames 1 --save-sram " + quote(host_profile_sram_path);
    assert(run_command(host_profile_command) == 0);
    const auto host_profile_sram = read_binary(host_profile_sram_path);
    assert(host_profile_sram.size() == 0x8000);
    assert(host_profile_sram[0] == 0x5A);
#endif
}
