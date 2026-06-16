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
    };
    write_binary(rom_path, rom);

    const std::string generate_command = quote_arg(SGRECOMP_TOOL_PATH) + " " + quote(rom_path) + " -o " + quote(generated_path);
    assert(run_command(generate_command) == 0);

    const std::string generated = read_text(generated_path);
    assert(contains(generated, "cpu.set_hl(0xc000)"));
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

    compile_generated_cpp(generated_path, object_path);
    assert(std::filesystem::exists(object_path));
}
