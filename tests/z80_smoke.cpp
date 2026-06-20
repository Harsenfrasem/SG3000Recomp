#include "sgrecomp/console.h"
#include "sgrecomp/cartridge.h"
#include "sgrecomp/game_profile.h"
#include "sgrecomp/host_runtime.h"
#include "sgrecomp/input_script.h"
#include "sgrecomp/recent_games.h"
#include "sgrecomp/save_state.h"

#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sgrecomp;

void test_recent_games_are_local_deduplicated_and_pruned() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "sgrecomp_recent_games_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path first = root / "first game.sms";
    const std::filesystem::path second = root / "second&game.sg";
    std::ofstream(first, std::ios::binary).put('\0');
    std::ofstream(second, std::ios::binary).put('\0');

    const std::array initial{first, second, first, root / "missing.sms"};
    auto recent = touch_recent_game(initial, second, 3);
    assert(recent.size() == 2);
    assert(recent[0] == second);
    assert(recent[1] == first);

    const std::filesystem::path list = root / "recent-games.txt";
    save_recent_games(list, recent);
    recent = load_recent_games(list);
    assert(recent.size() == 2);
    assert(recent[0] == second);
    assert(recent[1] == first);

    std::filesystem::remove(second);
    recent = load_recent_games(list);
    assert(recent.size() == 1);
    assert(recent[0] == first);
    assert(touch_recent_game(recent, first, 0).empty());
    std::filesystem::remove_all(root);
}

void run_until_halt(Console& console, u64 max_cycles = 4096) {
    console.run_cycles(max_cycles);
    assert(console.cpu().halted);
}

void setup_visible_sprites(Vdp& vdp, int count);

void test_cartridge_header_analysis() {
    std::vector<u8> rom(0x8000, 0x00);
    const std::array<u8, 8> magic{'T', 'M', 'R', ' ', 'S', 'E', 'G', 'A'};
    for (std::size_t i = 0; i < magic.size(); ++i) {
        rom[0x7FF0 + i] = magic[i];
    }
    rom[0x7FFC] = 0x34;
    rom[0x7FFD] = 0x12;
    rom[0x7FFE] = 0xA2;
    rom[0x7FFF] = 0x7C;
    u32 checksum = 0;
    for (std::size_t i = 0; i < rom.size(); ++i) {
        if (i >= 0x7FF0 && i < 0x8000) {
            continue;
        }
        checksum += rom[i];
    }
    rom[0x7FFA] = static_cast<u8>(checksum & 0xFF);
    rom[0x7FFB] = static_cast<u8>((checksum >> 8) & 0xFF);

    const CartridgeHeaderInfo header = analyze_cartridge_header(rom);
    assert(header.found);
    assert(header.offset == 0x7FF0);
    assert(header.product_code == "3412A");
    assert(header.version == 2);
    assert(header.region == CartridgeHeaderRegion::GameGearInternational);
    assert(header.declared_size_bytes == 32768);
    assert(header.declared_size_available);
    assert(header.declared_size_fits_rom);
    assert(header.stored_checksum == header.declared_size_checksum);
    assert(header.checksum_matches_declared_size);
    assert(cartridge_header_platform(header) == CartridgePlatform::GameGear);
    assert(cartridge_header_is_game_gear(header));
    assert(std::string(cartridge_platform_name(cartridge_header_platform(header))) == "Game Gear");
    assert(std::string(cartridge_region_name(header.region)) == "Game Gear international");
    assert(std::string(cartridge_size_code_name(header.region_size)) == "32 KiB");
}

void test_cartridge_header_generation_and_checksum_rebuild() {
    std::vector<u8> rom(0x8000, 0x11);
    CartridgeHeaderBuildOptions options;
    options.preserve_existing = false;
    options.region = CartridgeHeaderRegion::SmsJapan;
    options.product_code = "12ABF";
    options.version = 3;

    CartridgeHeaderInfo header = rebuild_cartridge_header(rom, options);
    assert(header.found);
    assert(header.offset == 0x7FF0);
    assert(header.region == CartridgeHeaderRegion::SmsJapan);
    assert(header.product_code == "12ABF");
    assert(header.version == 3);
    assert(header.checksum_matches_declared_size);

    rom[0x0100] ^= 0xFF;
    assert(!analyze_cartridge_header(rom).checksum_matches_declared_size);
    header = rebuild_cartridge_header(rom);
    assert(header.product_code == "12ABF");
    assert(header.version == 3);
    assert(header.checksum_matches_declared_size);
}

void test_cartridge_header_rejects_truncated_declared_size() {
    std::vector<u8> rom(0x8000, 0);
    const std::array<u8, 8> magic{'T', 'M', 'R', ' ', 'S', 'E', 'G', 'A'};
    std::copy(magic.begin(), magic.end(), rom.begin() + 0x7FF0);
    rom[0x7FFF] = 0x4E; // SMS export, claims 64 KiB
    const CartridgeHeaderInfo header = analyze_cartridge_header(rom);
    assert(header.declared_size_available);
    assert(!header.declared_size_fits_rom);
    bool rejected = false;
    try {
        (void)rebuild_cartridge_header(rom);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void test_basic_program() {
    const std::vector<u8> rom = {
        0x3E,
        0x12, // ld a,$12
        0x06,
        0x08, // ld b,$08
        0x04, // inc b
        0x05, // dec b
        0x80, // add a,b
        0xFE,
        0x1A, // cp $1a
        0x20,
        0x02, // jr nz,+2
        0x32,
        0x00,
        0xC0, // ld ($C000),a
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x1A);
    assert(console.bus().read(0xC000) == 0x1A);
}

void test_djnz_loop() {
    const std::vector<u8> rom = {
        0x06,
        0x03, // ld b,$03
        0x3E,
        0x00, // ld a,$00
        0x3C, // inc a
        0x10,
        0xFD, // djnz -3
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 3);
    assert(console.cpu().b == 0);
}

void test_stack_and_conditional_call() {
    const std::vector<u8> rom = {
        0x31, 0x00, 0xC1, // ld sp,$c100
        0x21, 0x34, 0x12, // ld hl,$1234
        0xE5,             // push hl
        0xD1,             // pop de
        0x7A,             // ld a,d
        0xFE, 0x12,       // cp $12
        0xCC, 0x0F, 0x00, // call z,$000f
        0x76,             // halt
        0x7B,             // $000f: ld a,e
        0xFE, 0x34,       // cp $34
        0xC8,             // ret z
        0x3E, 0x00,       // ld a,$00
        0xC9,             // ret
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().de() == 0x1234);
    assert(console.cpu().a == 0x34);
}

void test_immediate_alu() {
    const std::vector<u8> rom = {
        0x3E,
        0x10, // ld a,$10
        0xC6,
        0x0F, // add a,$0f -> 1f
        0xE6,
        0x1B, // and $1b -> 1b
        0xEE,
        0x03, // xor $03 -> 18
        0xF6,
        0x80, // or $80 -> 98
        0xD6,
        0x08, // sub $08 -> 90
        0xFE,
        0x90, // cp $90
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x90);
    assert((console.cpu().f & 0x40) != 0);
}

void test_exchange_and_rst() {
    std::vector<u8> rom(0x20, 0x00);
    rom[0x00] = 0x21; // ld hl,$1234
    rom[0x01] = 0x34;
    rom[0x02] = 0x12;
    rom[0x03] = 0x11; // ld de,$abcd
    rom[0x04] = 0xCD;
    rom[0x05] = 0xAB;
    rom[0x06] = 0xEB; // ex de,hl
    rom[0x07] = 0xD7; // rst $10
    rom[0x08] = 0x76; // halt
    rom[0x10] = 0xC9; // ret

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().hl() == 0xABCD);
    assert(console.cpu().de() == 0x1234);
    assert(console.cpu().sp == 0xDFF0);
}

void test_alternate_registers_and_interrupt_flipflops() {
    const std::vector<u8> rom = {
        0x3E,
        0x11, // ld a,$11
        0x08, // ex af,af'
        0x3E,
        0x22, // ld a,$22
        0x08, // ex af,af'
        0x06,
        0x33, // ld b,$33
        0xD9, // exx
        0x06,
        0x44, // ld b,$44
        0xD9, // exx
        0xFB, // ei
        0xF3, // di
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x11);
    assert(console.cpu().a_alt == 0x22);
    assert(console.cpu().b == 0x33);
    assert(console.cpu().b_alt == 0x44);
    assert(!console.cpu().iff1);
    assert(!console.cpu().iff2);
}

void test_cb_register_operations() {
    const std::vector<u8> rom = {
        0x06,
        0x81, // ld b,$81
        0xCB,
        0x00, // rlc b -> $03, carry set
        0xCB,
        0x40, // bit 0,b -> z clear
        0xCB,
        0x80, // res 0,b -> $02
        0xCB,
        0xC0, // set 0,b -> $03
        0x78, // ld a,b
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x03);
    assert((console.cpu().f & 0x40) == 0);
}

void test_cb_memory_operations() {
    const std::vector<u8> rom = {
        0x21,
        0x00,
        0xC0, // ld hl,$c000
        0x36,
        0x80, // ld (hl),$80
        0xCB,
        0x26, // sla (hl) -> $00, carry set
        0xCB,
        0xFE, // set 7,(hl) -> $80
        0xCB,
        0xBE, // res 7,(hl) -> $00
        0x7E, // ld a,(hl)
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x00);
    assert(console.bus().read(0xC000) == 0x00);
}

void test_ed_interrupt_and_special_registers() {
    const std::vector<u8> rom = {
        0x3E,
        0x42, // ld a,$42
        0xED,
        0x47, // ld i,a
        0x3E,
        0x00, // ld a,$00
        0xFB, // ei
        0xED,
        0x57, // ld a,i
        0xED,
        0x5E, // im 2
        0xED,
        0x44, // neg
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().i == 0x42);
    assert(console.cpu().a == 0xBE);
    assert(console.cpu().interrupt_mode == 2);
}

void test_ed_16bit_memory_load_store() {
    const std::vector<u8> rom = {
        0x01,
        0x34,
        0x12, // ld bc,$1234
        0xED,
        0x43,
        0x00,
        0xC0, // ld ($c000),bc
        0x11,
        0x00,
        0x00, // ld de,$0000
        0xED,
        0x5B,
        0x00,
        0xC0, // ld de,($c000)
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().de() == 0x1234);
    assert(console.bus().read(0xC000) == 0x34);
    assert(console.bus().read(0xC001) == 0x12);
}

void test_ed_block_transfer_and_search() {
    std::vector<u8> rom = {
        0x21, 0x00, 0xC0, // ld hl,$c000
        0x11, 0x10, 0xC0, // ld de,$c010
        0x01, 0x03, 0x00, // ld bc,$0003
        0xED, 0xB0,       // ldir
        0x21, 0x10, 0xC0, // ld hl,$c010
        0x01, 0x03, 0x00, // ld bc,$0003
        0x3E, 0x22,       // ld a,$22
        0xED, 0xB1,       // cpir
        0x76,             // halt
    };
    rom.resize(0x40, 0x00);

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    console.bus().write(0xC000, 0x11);
    console.bus().write(0xC001, 0x22);
    console.bus().write(0xC002, 0x33);
    run_until_halt(console);

    assert(console.bus().read(0xC010) == 0x11);
    assert(console.bus().read(0xC011) == 0x22);
    assert(console.bus().read(0xC012) == 0x33);
    assert(console.cpu().hl() == 0xC012);
    assert(console.cpu().bc() == 0x0001);
    assert((console.cpu().f & 0x40) != 0);
}

void test_ed_nibble_rotates() {
    const std::vector<u8> rom = {
        0x21,
        0x00,
        0xC0, // ld hl,$c000
        0x36,
        0x12, // ld (hl),$12
        0x3E,
        0xA5, // ld a,$a5
        0xED,
        0x6F, // rld -> a=$a1, (hl)=$25
        0xED,
        0x67, // rrd -> a=$a5, (hl)=$12
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0xA5);
    assert(console.bus().read(0xC000) == 0x12);
}

void test_ed_block_io() {
    const std::vector<u8> rom = {
        0x21,
        0x00,
        0xC0, // ld hl,$c000
        0x01,
        0x7F,
        0x02, // ld bc,$027f
        0xED,
        0xB3, // otir
        0x21,
        0x10,
        0xC0, // ld hl,$c010
        0x01,
        0xDD,
        0x02, // ld bc,$02dd
        0xED,
        0xB2, // inir
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    console.bus().write(0xC000, 0x90);
    console.bus().write(0xC001, 0x91);
    run_until_halt(console);

    assert(console.cpu().b == 0);
    assert(console.cpu().hl() == 0xC012);
    assert((console.cpu().f & 0x40) != 0);
}

void test_ed_port_register_io() {
    const std::vector<u8> rom = {
        0x0E,
        0x7F, // ld c,$7f
        0x16,
        0x55, // ld d,$55
        0xED,
        0x51, // out (c),d
        0x0E,
        0xDD, // ld c,$dd
        0xED,
        0x78, // in a,(c)
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0xFF);
}

void test_vblank_interrupt_im1() {
    std::vector<u8> rom(0x80, 0x00);
    rom[0x00] = 0x3E; // ld a,$20
    rom[0x01] = 0x20;
    rom[0x02] = 0xD3; // out ($bf),a ; register value
    rom[0x03] = 0xBF;
    rom[0x04] = 0x3E; // ld a,$81 ; VDP register 1 command
    rom[0x05] = 0x81;
    rom[0x06] = 0xD3; // out ($bf),a
    rom[0x07] = 0xBF;
    rom[0x08] = 0xFB; // ei
    rom[0x09] = 0x00; // nop loop
    rom[0x0A] = 0x18; // jr -3
    rom[0x0B] = 0xFD;
    rom[0x38] = 0x3E; // interrupt: ld a,$77
    rom[0x39] = 0x77;
    rom[0x3A] = 0x76; // halt

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    console.run_cycles(70000);

    assert(console.cpu().halted);
    assert(console.cpu().a == 0x77);
}

void test_ed_block_io_exact_flags() {
    {
        const std::array<u8, 2> rom{0xED, 0xA2}; // ini
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().set_bc(0x297E);
        console.cpu().set_hl(0xC000);
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().b == 0x28);
        assert(console.bus().read(0xC000) == 0x00); // V counter at first line
        assert(console.cpu().f == 0x28);            // X/Y from B; odd parity clears P/V
    }
    {
        const std::array<u8, 2> rom{0xED, 0xAA}; // ind
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().set_bc(0x297E);
        console.cpu().set_hl(0xC001);
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().b == 0x28);
        assert(console.cpu().hl() == 0xC000);
        assert(console.cpu().f == 0x2C); // X/Y plus even parity
    }
    {
        const std::array<u8, 2> rom{0xED, 0xA3}; // outi
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().set_bc(0x017F);
        console.cpu().set_hl(0xC000);
        console.bus().write(0xC000, 0xFF);
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().b == 0);
        assert(console.cpu().f == 0x57); // Z,H,P/V,N,C
    }
    {
        const std::array<u8, 2> rom{0xED, 0xAB}; // outd
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().set_bc(0x297F);
        console.cpu().set_hl(0xC001);
        console.bus().write(0xC001, 0x7F);
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().b == 0x28);
        assert(console.cpu().f == 0x28);
    }
}

void test_ed_decrementing_block_transfer_and_search() {
    const std::vector<u8> rom = {
        0x21, 0x02, 0xC0, // ld hl,$c002
        0x11, 0x12, 0xC0, // ld de,$c012
        0x01, 0x03, 0x00, // ld bc,$0003
        0xED, 0xB8,       // lddr
        0x21, 0x12, 0xC0, // ld hl,$c012
        0x01, 0x03, 0x00, // ld bc,$0003
        0x3E, 0x11,       // ld a,$11
        0xED, 0xB9,       // cpdr
        0x76,             // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    console.bus().write(0xC000, 0x11);
    console.bus().write(0xC001, 0x22);
    console.bus().write(0xC002, 0x33);
    run_until_halt(console);

    assert(console.bus().read(0xC010) == 0x11);
    assert(console.bus().read(0xC011) == 0x22);
    assert(console.bus().read(0xC012) == 0x33);
    assert(console.cpu().hl() == 0xC00F);
    assert(console.cpu().bc() == 0);
    assert((console.cpu().f & 0x40) != 0);
}

void test_all_z80_opcode_pages_have_runtime_behavior() {
    for (int opcode = 0; opcode <= 0xFF; ++opcode) {
        Console base(ConsoleModel::SMS);
        const std::array<u8, 5> base_rom{static_cast<u8>(opcode), 0, 0, 0, 0};
        base.load_rom(base_rom);
        execute_one(base.cpu(), base.bus());

        Console ed(ConsoleModel::SMS);
        const std::array<u8, 5> ed_rom{0xED, static_cast<u8>(opcode), 0, 0, 0};
        ed.load_rom(ed_rom);
        execute_one(ed.cpu(), ed.bus());

        Console ix(ConsoleModel::SMS);
        const std::array<u8, 6> ix_rom{0xDD, static_cast<u8>(opcode), 0, 0, 0, 0};
        ix.load_rom(ix_rom);
        execute_one(ix.cpu(), ix.bus());

        Console iy(ConsoleModel::SMS);
        const std::array<u8, 6> iy_rom{0xFD, static_cast<u8>(opcode), 0, 0, 0, 0};
        iy.load_rom(iy_rom);
        execute_one(iy.cpu(), iy.bus());
    }

    Console undefined_ed(ConsoleModel::SMS);
    const std::array<u8, 2> undefined_ed_rom{0xED, 0x00};
    undefined_ed.load_rom(undefined_ed_rom);
    execute_one(undefined_ed.cpu(), undefined_ed.bus());
    assert(undefined_ed.cpu().pc == 2);
    assert(undefined_ed.cpu().cycles == 8);

    Console ignored_index(ConsoleModel::SMS);
    const std::array<u8, 2> ignored_index_rom{0xDD, 0x00};
    ignored_index.load_rom(ignored_index_rom);
    execute_one(ignored_index.cpu(), ignored_index.bus());
    assert(ignored_index.cpu().pc == 2);
    assert(ignored_index.cpu().cycles == 8);
    assert(ignored_index.cpu().r == 2);
}

void test_z80_undocumented_xy_flags() {
    constexpr u8 xy = 0x28;

    {
        const std::array<u8, 2> rom{0xC6, 0x28}; // add a,$28
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().a = 0x10;
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().a == 0x38);
        assert((console.cpu().f & xy) == xy);
    }
    {
        const std::array<u8, 1> rom{0x04}; // inc b
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().b = 0x27;
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().b == 0x28);
        assert((console.cpu().f & xy) == xy);
    }
    {
        const std::array<u8, 2> rom{0xCB, 0x00}; // rlc b
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().b = 0x14;
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().b == 0x28);
        assert((console.cpu().f & xy) == xy);
    }
    {
        const std::array<u8, 2> rom{0xCB, 0x40}; // bit 0,b
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().b = 0x28;
        execute_one(console.cpu(), console.bus());
        assert((console.cpu().f & xy) == xy);
        assert((console.cpu().f & 0x40) != 0);
    }
    {
        const std::array<u8, 4> rom{0xDD, 0xCB, 0x00, 0x46}; // bit 0,(ix+0)
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().ixh = 0xA8;
        console.cpu().ixl = 0x00;
        execute_one(console.cpu(), console.bus());
        assert((console.cpu().f & xy) == xy); // copied from effective address high byte
    }
    {
        const std::array<u8, 1> rom{0x09}; // add hl,bc
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().set_hl(0x1800);
        console.cpu().set_bc(0x1000);
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().hl() == 0x2800);
        assert((console.cpu().f & xy) == xy);
    }
    {
        const std::array<u8, 1> rom{0x37}; // scf
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().a = 0x28;
        execute_one(console.cpu(), console.bus());
        assert((console.cpu().f & xy) == xy);
    }
    {
        const std::array<u8, 2> rom{0xED, 0xA0}; // ldi
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().a = 0x02;
        console.cpu().set_hl(0xC000);
        console.cpu().set_de(0xC001);
        console.cpu().set_bc(1);
        console.bus().write(0xC000, 0x08); // A + value = $0a: bit 3 -> X, bit 1 -> Y
        execute_one(console.cpu(), console.bus());
        assert((console.cpu().f & xy) == xy);
    }
}

void test_z80_refresh_register_and_prefix_cycles() {
    {
        const std::array<u8, 1> rom{0x00};
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().r = 0xFF;
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().r == 0x80); // bit 7 preserved, low 7 bits wrap
        assert(console.cpu().cycles == 4);
    }
    {
        const std::array<u8, 2> rom{0xCB, 0x00};
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().r == 2);
        assert(console.cpu().cycles == 8);
    }
    {
        const std::array<u8, 2> rom{0xED, 0x00};
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().r == 2);
        assert(console.cpu().cycles == 8);
    }
    {
        const std::array<u8, 4> rom{0xDD, 0x21, 0x34, 0x12};
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().r == 2);
        assert(console.cpu().cycles == 14);
    }
    {
        const std::array<u8, 4> rom{0xFD, 0x21, 0x34, 0x12};
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().r == 2);
        assert(console.cpu().cycles == 14);
    }
    {
        const std::array<u8, 4> rom{0xDD, 0xCB, 0x00, 0x46};
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().ixh = 0xC0;
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().r == 2); // final DDCB opcode is not an M1 fetch
        assert(console.cpu().cycles == 20);
    }
    {
        const std::array<u8, 3> rom{0xDD, 0xDD, 0x00};
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().r == 3);
        assert(console.cpu().cycles == 12);
    }
    {
        const std::array<u8, 1> rom{0x76};
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        execute_one(console.cpu(), console.bus());
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().r == 2);
        assert(console.cpu().cycles == 8);
    }
}

void test_z80_conditional_and_block_cycle_counts() {
    const auto flags_for_condition = [](int condition, bool taken) -> u8 {
        const u8 mask = condition < 2 ? 0x40 : condition < 4 ? 0x01 : condition < 6 ? 0x04 : 0x80;
        const bool condition_when_set = (condition & 1) != 0;
        return taken == condition_when_set ? mask : 0;
    };

    for (int condition = 0; condition < 8; ++condition) {
        for (const bool taken : {false, true}) {
            {
                const std::array<u8, 1> rom{static_cast<u8>(0xC0 | (condition << 3))};
                Console console(ConsoleModel::SMS);
                console.load_rom(rom);
                console.cpu().f = flags_for_condition(condition, taken);
                console.cpu().sp = 0xC000;
                console.bus().write(0xC000, 0x34);
                console.bus().write(0xC001, 0x12);
                execute_one(console.cpu(), console.bus());
                assert(console.cpu().cycles == (taken ? 11 : 5));
                assert(console.cpu().pc == (taken ? 0x1234 : 0x0001));
                assert(console.cpu().sp == (taken ? 0xC002 : 0xC000));
            }
            {
                const std::array<u8, 3> rom{static_cast<u8>(0xC2 | (condition << 3)), 0x34, 0x12};
                Console console(ConsoleModel::SMS);
                console.load_rom(rom);
                console.cpu().f = flags_for_condition(condition, taken);
                execute_one(console.cpu(), console.bus());
                assert(console.cpu().cycles == 10);
                assert(console.cpu().pc == (taken ? 0x1234 : 0x0003));
            }
            {
                const std::array<u8, 3> rom{static_cast<u8>(0xC4 | (condition << 3)), 0x34, 0x12};
                Console console(ConsoleModel::SMS);
                console.load_rom(rom);
                console.cpu().f = flags_for_condition(condition, taken);
                console.cpu().sp = 0xC100;
                execute_one(console.cpu(), console.bus());
                assert(console.cpu().cycles == (taken ? 17 : 10));
                assert(console.cpu().pc == (taken ? 0x1234 : 0x0003));
                assert(console.cpu().sp == (taken ? 0xC0FE : 0xC100));
                if (taken) {
                    assert(console.bus().read(0xC0FE) == 0x03);
                    assert(console.bus().read(0xC0FF) == 0x00);
                }
            }
        }
    }

    for (int condition = 0; condition < 4; ++condition) {
        for (const bool taken : {false, true}) {
            const std::array<u8, 2> rom{static_cast<u8>(0x20 | (condition << 3)), 0x02};
            Console console(ConsoleModel::SMS);
            console.load_rom(rom);
            console.cpu().f = flags_for_condition(condition, taken);
            execute_one(console.cpu(), console.bus());
            assert(console.cpu().cycles == (taken ? 12 : 7));
            assert(console.cpu().pc == (taken ? 0x0004 : 0x0002));
        }
    }

    for (const bool taken : {false, true}) {
        const std::array<u8, 2> rom{0x10, 0x02}; // djnz +2
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().b = taken ? 2 : 1;
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().cycles == (taken ? 13 : 8));
        assert(console.cpu().pc == (taken ? 0x0004 : 0x0002));
    }

    for (const bool repeats : {false, true}) {
        const std::array<u8, 2> rom{0xED, 0xB0}; // ldir
        Console console(ConsoleModel::SMS);
        console.load_rom(rom);
        console.cpu().set_hl(0xC000);
        console.cpu().set_de(0xC010);
        console.cpu().set_bc(repeats ? 2 : 1);
        execute_one(console.cpu(), console.bus());
        assert(console.cpu().cycles == (repeats ? 21 : 16));
        assert(console.cpu().pc == (repeats ? 0x0000 : 0x0002));
    }
}

void test_vdp_line_interrupt_im1() {
    std::vector<u8> rom(0x80, 0x00);
    rom[0x00] = 0x3E; // ld a,$01
    rom[0x01] = 0x01;
    rom[0x02] = 0xD3; // out ($bf),a ; line counter value
    rom[0x03] = 0xBF;
    rom[0x04] = 0x3E; // ld a,$8a ; VDP register 10 command
    rom[0x05] = 0x8A;
    rom[0x06] = 0xD3; // out ($bf),a
    rom[0x07] = 0xBF;
    rom[0x08] = 0x3E; // ld a,$10
    rom[0x09] = 0x10;
    rom[0x0A] = 0xD3; // out ($bf),a ; line irq enable
    rom[0x0B] = 0xBF;
    rom[0x0C] = 0x3E; // ld a,$80 ; VDP register 0 command
    rom[0x0D] = 0x80;
    rom[0x0E] = 0xD3; // out ($bf),a
    rom[0x0F] = 0xBF;
    rom[0x10] = 0xFB; // ei
    rom[0x11] = 0x00; // nop loop
    rom[0x12] = 0x18; // jr -3
    rom[0x13] = 0xFD;
    rom[0x38] = 0x3E; // interrupt: ld a,$44
    rom[0x39] = 0x44;
    rom[0x3A] = 0x76; // halt

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    console.run_cycles(4000);

    assert(console.cpu().halted);
    assert(console.cpu().a == 0x44);
}

void test_vdp_data_port_clears_pending_control_latch() {
    Vdp vdp;
    vdp.write_control(0x12);
    vdp.write_data(0xAA);
    vdp.write_control(0x81);
    assert(vdp.debug_registers()[1] == 0x00);

    (void)vdp.read_status();
    vdp.write_control(0x40);
    vdp.write_control(0x81);
    assert(vdp.debug_registers()[1] == 0x40);

    (void)vdp.read_status();
    vdp.write_control(0x34);
    (void)vdp.read_data();
    vdp.write_control(0x82);
    assert(vdp.debug_registers()[2] == 0x00);
}

void test_vdp_buffered_vram_reads_and_state_round_trip() {
    Vdp vdp;
    vdp.write_control(0x34);
    vdp.write_control(0x52); // VRAM write at $1234
    vdp.write_data(0xAA);
    vdp.write_data(0xBB);

    vdp.write_control(0x34);
    vdp.write_control(0x12); // VRAM read command prefetches $1234
    VdpState prefetched = vdp.save_state();
    assert(prefetched.address == 0x1235);
    assert(prefetched.read_buffer == 0xAA);
    assert(vdp.read_data() == 0xAA);
    VdpState after_first = vdp.save_state();
    assert(after_first.address == 0x1236);
    assert(after_first.read_buffer == 0xBB);
    assert(vdp.read_data() == 0xBB);

    Vdp wrapped;
    wrapped.write_control(0xFF);
    wrapped.write_control(0x7F); // VRAM write at $3fff
    wrapped.write_data(0x5A);
    wrapped.write_data(0xA5); // wraps to $0000
    wrapped.write_control(0xFF);
    wrapped.write_control(0x3F); // read command at $3fff, address wraps immediately
    assert(wrapped.save_state().address == 0x0000);
    assert(wrapped.read_data() == 0x5A);
    assert(wrapped.read_data() == 0xA5);

    Console source(ConsoleModel::SMS);
    source.vdp().write_control(0x00);
    source.vdp().write_control(0x50); // VRAM write at $1000
    source.vdp().write_data(0x3C);
    source.vdp().write_control(0x00);
    source.vdp().write_control(0x10); // prefetch $1000
    const auto bytes = serialize_console_state(source.save_state());
    const ConsoleState decoded = deserialize_console_state(bytes);
    Vdp restored;
    restored.load_state(decoded.vdp);
    assert(restored.save_state().address == 0x1001);
    assert(restored.read_data() == 0x3C);
}

void test_vdp_line_irq_is_not_status_overflow_bit() {
    Vdp vdp;
    vdp.write_control(0x10);
    vdp.write_control(0x80); // register 0: line IRQ enable
    vdp.write_control(0x00);
    vdp.write_control(0x8A); // register 10: line counter 0

    vdp.tick(228);
    assert(!vdp.irq_pending());
    vdp.tick(228);
    assert(vdp.irq_pending());
    assert((vdp.read_status() & 0x40) == 0);
    assert(!vdp.irq_pending());
}

void test_vdp_sprite_overflow_status_does_not_raise_line_irq() {
    Vdp vdp;
    vdp.write_control(0x10);
    vdp.write_control(0x80); // register 0: line IRQ enabled
    setup_visible_sprites(vdp, 9);

    vdp.tick(228);
    assert((vdp.read_status() & 0x40) != 0);
    assert(!vdp.irq_pending());
}

void test_pause_triggers_nmi() {
    std::vector<u8> rom(0x80, 0x00);
    rom[0x00] = 0x00; // nop
    rom[0x01] = 0x18; // jr -3
    rom[0x02] = 0xFD;
    rom[0x66] = 0x3E; // nmi: ld a,$66
    rom[0x67] = 0x66;
    rom[0x68] = 0x76; // halt

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    console.run_cycles(16);
    console.press_pause();
    run_until_halt(console);

    assert(console.cpu().a == 0x66);
}

void test_two_player_joypad_ports() {
    Console console(ConsoleModel::SMS);
    console.joypad().set_player1(static_cast<u8>(Joypad::Up | Joypad::Button1));
    console.joypad().set_player2(static_cast<u8>(Joypad::Down | Joypad::Button2));

    const u8 port_a = console.bus().input(0xDC);
    const u8 port_b = console.bus().input(0xDD);

    assert((port_a & 0x01) == 0);
    assert((port_a & 0x10) == 0);
    assert((port_a & 0x02) != 0);
    assert((port_a & 0x40) != 0);
    assert((port_a & 0x80) == 0);
    assert((port_b & 0x08) == 0);
    assert((port_b & 0x01) != 0);
}

void test_vdp_mode4_background_pixel() {
    Vdp vdp;
    vdp.write_control(0x00);
    vdp.write_control(0x80); // register 0
    vdp.write_control(0x40);
    vdp.write_control(0x81); // register 1: display enabled
    vdp.write_control(0x0E);
    vdp.write_control(0x82); // register 2: name table at $3800
    vdp.write_control(0xFF);
    vdp.write_control(0x84); // register 4 is ignored by Mode 4 background patterns

    vdp.write_control(0x01);
    vdp.write_control(0xC0); // CRAM write at 1
    vdp.write_data(0x03);    // red

    vdp.write_control(0x00);
    vdp.write_control(0x40); // VRAM pattern 0
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x78); // VRAM name table $3800
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.tick(228);

    assert(vdp.framebuffer()[0] == 0xFFFF0000);
    assert(vdp.framebuffer()[1] == 0xFF000000);
}

void test_vdp_mode4_renders_ninth_tile_bit_palette_and_flips() {
    Vdp vdp;
    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x0E);
    vdp.write_control(0x82); // name table at $3800

    vdp.write_control(0x12);
    vdp.write_control(0xC0); // palette 1, color 2
    vdp.write_data(0x0C);    // green

    vdp.write_control(0x3C);
    vdp.write_control(0x60); // tile $101, row 7 at $203c
    vdp.write_data(0x00);
    vdp.write_data(0x01); // color 2 at bit zero
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x78); // first name-table entry
    vdp.write_data(0x01);
    vdp.write_data(0x0F); // tile bit 8, flip X/Y, palette 1

    vdp.tick(228);
    assert(vdp.framebuffer()[0] == 0xFF00FF00);
    assert(vdp.framebuffer()[7] == 0xFF000000);
    const VdpTileEntry entry = vdp.debug_tilemap()[0];
    assert(entry.tile == 0x101);
    assert(entry.palette1 && entry.flip_x && entry.flip_y);
}

void test_vdp_display_disabled_uses_backdrop_color() {
    Vdp vdp;
    vdp.write_control(0x10);
    vdp.write_control(0xC0);
    vdp.write_data(0x03);

    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x0E);
    vdp.write_control(0x82); // name table at $3800
    vdp.write_control(0x01);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C);
    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_control(0x00);
    vdp.write_control(0x78);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.tick(228);
    assert(vdp.framebuffer()[0] == 0xFF00FF00);

    vdp.write_control(0x00);
    vdp.write_control(0x81); // display disabled
    vdp.tick(228);
    assert(vdp.framebuffer()[Vdp::width] == 0xFFFF0000);
}

void test_vdp_backdrop_uses_register7_color() {
    Vdp vdp;
    vdp.write_control(0x13);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C); // palette 1 color 3: green
    vdp.write_control(0x03);
    vdp.write_control(0x87); // register 7 backdrop color 3

    vdp.tick(228);
    assert(vdp.framebuffer()[0] == 0xFF00FF00);

    vdp.write_control(0x60);
    vdp.write_control(0x80); // blank left column, lock top hscroll
    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.tick(228);
    assert(vdp.framebuffer()[Vdp::width] == 0xFF00FF00);
}

void test_sg3000_vdp_tms_graphics1_background_pixel() {
    Console console(ConsoleModel::SG3000);
    Vdp& vdp = console.vdp();
    assert(vdp.video_mode() == VdpVideoMode::TmsGraphics1);

    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x02);
    vdp.write_control(0x82); // name table at $0800
    vdp.write_control(0x80);
    vdp.write_control(0x83); // color table at $2000
    vdp.write_control(0x00);
    vdp.write_control(0x84); // pattern table at $0000

    vdp.write_control(0x00);
    vdp.write_control(0x40); // pattern 0 row 0: first pixel set
    vdp.write_data(0x80);

    vdp.write_control(0x00);
    vdp.write_control(0x48); // name table first tile
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x60); // color table group 0: white on black
    vdp.write_data(0xF1);

    vdp.tick(228);
    assert(vdp.framebuffer()[0] == 0xFFFFFFFF);
    assert(vdp.framebuffer()[1] == 0xFF000000);
}

void test_sg3000_vdp_tms_text_mode() {
    Console console(ConsoleModel::SG3000);
    Vdp& vdp = console.vdp();
    vdp.write_control(0x50);
    vdp.write_control(0x81); // display enabled + M1 text mode
    vdp.write_control(0x02);
    vdp.write_control(0x82); // name table at $0800
    vdp.write_control(0x00);
    vdp.write_control(0x84); // pattern table at $0000
    vdp.write_control(0xF1);
    vdp.write_control(0x87); // white text on black

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x80); // first glyph pixel set
    vdp.write_control(0x00);
    vdp.write_control(0x48);
    vdp.write_data(0x00); // first character uses glyph zero

    vdp.tick(228);
    assert(vdp.video_mode() == VdpVideoMode::TmsText);
    assert(vdp.framebuffer()[0] == 0xFF000000); // 8-pixel border
    assert(vdp.framebuffer()[8] == 0xFFFFFFFF);
    assert(vdp.framebuffer()[9] == 0xFF000000);
}

void test_sg3000_vdp_tms_graphics2_mode() {
    Console console(ConsoleModel::SG3000);
    Vdp& vdp = console.vdp();
    vdp.write_control(0x48);
    vdp.write_control(0x81); // display enabled + M2 Graphics II
    vdp.write_control(0x0E);
    vdp.write_control(0x82); // name table at $3800
    vdp.write_control(0xFF);
    vdp.write_control(0x83); // color table at $2000
    vdp.write_control(0x03);
    vdp.write_control(0x84); // pattern table at $0000

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_control(0x00);
    vdp.write_control(0x60);
    vdp.write_data(0xF1);
    vdp.write_control(0x00);
    vdp.write_control(0x78);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x48); // second Graphics II section pattern
    vdp.write_data(0x80);
    vdp.write_control(0x00);
    vdp.write_control(0x68); // second Graphics II section colors
    vdp.write_data(0xF4);
    vdp.write_control(0x00);
    vdp.write_control(0x79); // name table row 8
    vdp.write_data(0x00);

    vdp.tick(228 * 65);
    assert(vdp.video_mode() == VdpVideoMode::TmsGraphics2);
    assert(vdp.framebuffer()[0] == 0xFFFFFFFF);
    assert(vdp.framebuffer()[1] == 0xFF000000);
    assert(vdp.framebuffer()[64 * Vdp::width] == 0xFFFFFFFF);
    assert(vdp.framebuffer()[64 * Vdp::width + 1] == 0xFF5455ED);
}

void test_sg3000_vdp_tms_multicolor_mode() {
    Console console(ConsoleModel::SG3000);
    Vdp& vdp = console.vdp();
    vdp.write_control(0x02);
    vdp.write_control(0x80); // M3 multicolor
    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x02);
    vdp.write_control(0x82); // name table at $0800
    vdp.write_control(0x00);
    vdp.write_control(0x84); // pattern table at $0000

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0xF4); // white left block, blue right block
    vdp.write_control(0x00);
    vdp.write_control(0x48);
    vdp.write_data(0x00);

    vdp.tick(228);
    assert(vdp.video_mode() == VdpVideoMode::TmsMulticolor);
    assert(vdp.framebuffer()[0] == 0xFFFFFFFF);
    assert(vdp.framebuffer()[3] == 0xFFFFFFFF);
    assert(vdp.framebuffer()[4] == 0xFF5455ED);
    assert(vdp.framebuffer()[7] == 0xFF5455ED);
}

void test_sg3000_vdp_tms_sprite_pixel() {
    Console console(ConsoleModel::SG3000);
    Vdp& vdp = console.vdp();

    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x36);
    vdp.write_control(0x85); // sprite attribute table at $1b00
    vdp.write_control(0x00);
    vdp.write_control(0x86); // sprite pattern table at $0000

    vdp.write_control(0x00);
    vdp.write_control(0x40); // sprite pattern 0 row 0
    vdp.write_data(0x80);

    vdp.write_control(0x00);
    vdp.write_control(0x5B); // sprite attribute table
    vdp.write_data(0x00);    // y = 1
    vdp.write_data(0x00);    // x = 0
    vdp.write_data(0x00);    // tile 0
    vdp.write_data(0x0F);    // white
    vdp.write_data(0xD0);    // terminator

    vdp.tick(228 * 2);
    assert(vdp.framebuffer()[Vdp::width] == 0xFFFFFFFF);
    assert(vdp.framebuffer()[Vdp::width + 1] == 0xFF000000);
}

void test_sg3000_vdp_tms_large_sprite_quadrants_and_zoom() {
    const auto setup = [](Vdp& vdp, u8 register1) {
        vdp.write_control(register1);
        vdp.write_control(0x81); // display, 16x16, optional zoom
        vdp.write_control(0x36);
        vdp.write_control(0x85); // SAT at $1b00
        vdp.write_control(0x00);
        vdp.write_control(0x86); // patterns at $0000

        for (u8 tile = 0; tile < 4; ++tile) {
            vdp.write_control(static_cast<u8>(tile * 8));
            vdp.write_control(0x40);
            vdp.write_data(0x80); // first pixel in each quadrant
        }

        vdp.write_control(0x00);
        vdp.write_control(0x5B);
        vdp.write_data(0xFF); // y = 0
        vdp.write_data(0x00); // x = 0
        vdp.write_data(0x03); // masked to pattern group 0
        vdp.write_data(0x0F); // white
        vdp.write_data(0xD0);
    };

    Console normal(ConsoleModel::SG3000);
    setup(normal.vdp(), 0x42);
    normal.vdp().tick(228 * 9);
    assert(normal.vdp().framebuffer()[0] == 0xFFFFFFFF);
    assert(normal.vdp().framebuffer()[8] == 0xFFFFFFFF);
    assert(normal.vdp().framebuffer()[8 * Vdp::width] == 0xFFFFFFFF);
    assert(normal.vdp().framebuffer()[8 * Vdp::width + 8] == 0xFFFFFFFF);
    assert(normal.vdp().framebuffer()[9] == 0xFF000000);

    Console zoomed(ConsoleModel::SG3000);
    setup(zoomed.vdp(), 0x43);
    zoomed.vdp().tick(228 * 17);
    assert(zoomed.vdp().framebuffer()[0] == 0xFFFFFFFF);
    assert(zoomed.vdp().framebuffer()[1] == 0xFFFFFFFF);
    assert(zoomed.vdp().framebuffer()[16] == 0xFFFFFFFF);
    assert(zoomed.vdp().framebuffer()[17] == 0xFFFFFFFF);
    assert(zoomed.vdp().framebuffer()[16 * Vdp::width + 16] == 0xFFFFFFFF);
}

void test_vdp_name_table_register_masks_low_bit() {
    Vdp vdp;
    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x0F);
    vdp.write_control(0x82); // register 2 low bit ignored, base remains $3800

    vdp.write_control(0x01);
    vdp.write_control(0xC0);
    vdp.write_data(0x03);
    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x78);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.tick(228);
    assert(vdp.framebuffer()[0] == 0xFFFF0000);
    assert(vdp.debug_tilemap()[0].address == 0x3800);
}

void test_vdp_scroll_lock_and_left_blank() {
    Vdp vdp;
    vdp.write_control(0x60);
    vdp.write_control(0x80); // register 0: blank left column, lock top hscroll
    vdp.write_control(0x40);
    vdp.write_control(0x81); // register 1: display enabled
    vdp.write_control(0x0E);
    vdp.write_control(0x82); // register 2: name table at $3800
    vdp.write_control(0x04);
    vdp.write_control(0x88); // register 8: horizontal scroll

    vdp.write_control(0x00);
    vdp.write_control(0xC0); // backdrop color black
    vdp.write_data(0x00);
    vdp.write_control(0x01);
    vdp.write_control(0xC0); // palette color 1 red
    vdp.write_data(0x03);

    vdp.write_control(0x00);
    vdp.write_control(0x40); // tile 0: red first pixel
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x78); // name table $3800
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.tick(228);

    assert(vdp.framebuffer()[0] == 0xFF000000);
    assert(vdp.framebuffer()[8] == 0xFFFF0000);
}

void test_vdp_horizontal_scroll_moves_background_right() {
    Vdp vdp;
    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x0E);
    vdp.write_control(0x82); // name table at $3800
    vdp.write_control(0x08);
    vdp.write_control(0x88); // move background right by one tile

    vdp.write_control(0x01);
    vdp.write_control(0xC0);
    vdp.write_data(0x03); // red
    vdp.write_control(0x02);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C); // green

    vdp.write_control(0x20);
    vdp.write_control(0x40); // tile 1: red first pixel
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_control(0x40);
    vdp.write_control(0x40); // tile 2: green first pixel
    vdp.write_data(0x00);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x78);
    for (int tile_x = 0; tile_x < 32; ++tile_x) {
        vdp.write_data(tile_x == 31 ? 0x01 : (tile_x == 0 ? 0x02 : 0x00));
        vdp.write_data(0x00);
    }

    vdp.tick(228);
    assert(vdp.framebuffer()[0] == 0xFFFF0000); // source x=248, tile 31
    assert(vdp.framebuffer()[8] == 0xFF00FF00); // source x=0, tile 0
}

void test_vdp_left_column_blank_masks_sprites() {
    Vdp vdp;
    vdp.write_control(0x20);
    vdp.write_control(0x80); // blank left column
    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x7E);
    vdp.write_control(0x85); // sprite table at $3f00
    vdp.write_control(0x10);
    vdp.write_control(0xC0);
    vdp.write_data(0x03); // backdrop red
    vdp.write_control(0x11);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C); // sprite green

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_control(0x00);
    vdp.write_control(0x7F);
    vdp.write_data(0xFF);
    vdp.write_data(0xFF);
    vdp.write_data(0xD0);
    vdp.write_control(0x80);
    vdp.write_control(0x7F);
    vdp.write_data(0x04);
    vdp.write_data(0x00);
    vdp.write_data(0x04);
    vdp.write_data(0x00);

    vdp.tick(228);
    assert(vdp.framebuffer()[4] == 0xFFFF0000);
    assert(vdp.framebuffer()[8] == 0xFF000000);
    assert((vdp.read_status() & 0x20) != 0); // masking occurs after hardware collision evaluation
}

void test_vdp_right_column_vertical_scroll_lock() {
    Vdp vdp;
    vdp.write_control(0x80);
    vdp.write_control(0x80); // register 0: lock right columns vertical scroll
    vdp.write_control(0x40);
    vdp.write_control(0x81); // register 1: display enabled
    vdp.write_control(0x0E);
    vdp.write_control(0x82); // register 2: name table at $3800
    vdp.write_control(0x08);
    vdp.write_control(0x89); // register 9: vertical scroll one tile

    vdp.write_control(0x01);
    vdp.write_control(0xC0);
    vdp.write_data(0x03); // red
    vdp.write_control(0x02);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C); // green

    vdp.write_control(0x20);
    vdp.write_control(0x40); // tile 1 pattern
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_control(0x40);
    vdp.write_control(0x40); // tile 2 pattern
    vdp.write_data(0x00);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x78); // name table $3800
    for (int i = 0; i < 32 * 32; ++i) {
        const u8 tile = i == 24 ? 0x02 : (i == 32 ? 0x01 : 0x00);
        vdp.write_data(tile);
        vdp.write_data(0x00);
    }

    vdp.tick(228);

    assert(vdp.framebuffer()[0] == 0xFFFF0000);
    assert(vdp.framebuffer()[24 * 8] == 0xFF00FF00);
}

void test_vdp_vertical_scroll_wraps_at_224_pixels() {
    Vdp vdp;
    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x0E);
    vdp.write_control(0x82); // name table at $3800
    vdp.write_control(0x28);
    vdp.write_control(0x89); // line 191 samples source line 7 after 224 wrap

    vdp.write_control(0x01);
    vdp.write_control(0xC0);
    vdp.write_data(0x03); // red
    vdp.write_control(0x02);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C); // green

    vdp.write_control(0x3C);
    vdp.write_control(0x40); // tile 1, row 7: red first pixel
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_control(0x5C);
    vdp.write_control(0x40); // tile 2, row 7: green first pixel
    vdp.write_data(0x00);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x78);
    for (int row = 0; row < 32; ++row) {
        for (int column = 0; column < 32; ++column) {
            const u8 tile = row == 0 && column == 0 ? 0x01 : (row == 28 && column == 0 ? 0x02 : 0x00);
            vdp.write_data(tile);
            vdp.write_data(0x00);
        }
    }

    vdp.tick(228 * 192);
    assert(vdp.framebuffer()[191 * Vdp::width] == 0xFFFF0000);
}

void test_vdp_right_column_vertical_scroll_lock_uses_screen_column() {
    Vdp vdp;
    vdp.write_control(0x80);
    vdp.write_control(0x80); // lock right columns vertical scroll
    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x0E);
    vdp.write_control(0x82); // name table at $3800
    vdp.write_control(0x40);
    vdp.write_control(0x88); // hscroll selects source tile 16 at screen column 24
    vdp.write_control(0x08);
    vdp.write_control(0x89); // vscroll one tile

    vdp.write_control(0x01);
    vdp.write_control(0xC0);
    vdp.write_data(0x03); // red
    vdp.write_control(0x02);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C); // green

    vdp.write_control(0x20);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_control(0x40);
    vdp.write_control(0x40);
    vdp.write_data(0x00);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x78);
    for (int i = 0; i < 32 * 32; ++i) {
        const u8 tile = i == 16 ? 0x02 : (i == 32 ? 0x01 : 0x00);
        vdp.write_data(tile);
        vdp.write_data(0x00);
    }

    vdp.tick(228);
    assert(vdp.framebuffer()[24 * 8] == 0xFF00FF00);
}

void test_vdp_basic_sprite_pixel() {
    Vdp vdp;
    vdp.write_control(0x40);
    vdp.write_control(0x81); // register 1: display enabled
    vdp.write_control(0x7E);
    vdp.write_control(0x85); // register 5: sprite table at $3f00

    vdp.write_control(0x11);
    vdp.write_control(0xC0); // CRAM write at 17
    vdp.write_data(0x0C);    // green

    vdp.write_control(0x00);
    vdp.write_control(0x40); // VRAM pattern 0
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x7F); // VRAM sprite table $3f00
    vdp.write_data(0xFF);    // top line after wrap
    vdp.write_data(0xD0);    // terminator

    vdp.write_control(0x80);
    vdp.write_control(0x7F); // VRAM sprite attributes $3f80
    vdp.write_data(0x04);    // x
    vdp.write_data(0x00);    // tile

    vdp.tick(228);

    assert(vdp.framebuffer()[4] == 0xFF00FF00);
    assert(vdp.framebuffer()[5] == 0xFF000000);
}

void test_vdp_sprite_shift_and_zoom() {
    Vdp vdp;
    vdp.write_control(0x08);
    vdp.write_control(0x80); // register 0: shift sprites left 8
    vdp.write_control(0x41);
    vdp.write_control(0x81); // register 1: display enabled, zoomed sprites
    vdp.write_control(0x7E);
    vdp.write_control(0x85); // register 5: sprite table at $3f00

    vdp.write_control(0x11);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C);

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x7F);
    vdp.write_data(0xFF);
    vdp.write_data(0xD0);

    vdp.write_control(0x80);
    vdp.write_control(0x7F);
    vdp.write_data(0x10);
    vdp.write_data(0x00);

    vdp.tick(228);
    vdp.tick(228);

    assert(vdp.framebuffer()[8] == 0xFF00FF00);
    assert(vdp.framebuffer()[9] == 0xFF00FF00);
    assert(vdp.framebuffer()[Vdp::width + 8] == 0xFF00FF00);
}

void test_vdp_sprite_pattern_base_register() {
    Vdp vdp;
    vdp.write_control(0x40);
    vdp.write_control(0x81); // register 1: display enabled
    vdp.write_control(0x7E);
    vdp.write_control(0x85); // register 5: sprite table at $3f00
    vdp.write_control(0x04);
    vdp.write_control(0x86); // register 6: sprite patterns at $2000

    vdp.write_control(0x11);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C);

    vdp.write_control(0x00);
    vdp.write_control(0x40); // tile 0 at $0000 remains empty
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x60); // tile 0 at sprite pattern base $2000
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x7F);
    vdp.write_data(0xFF);
    vdp.write_data(0xD0);
    vdp.write_control(0x80);
    vdp.write_control(0x7F);
    vdp.write_data(0x04);
    vdp.write_data(0x00);

    vdp.tick(228);
    assert(vdp.framebuffer()[4] == 0xFF00FF00);
}

void test_vdp_tall_sprite_uses_second_tile() {
    Vdp vdp;
    vdp.write_control(0x42);
    vdp.write_control(0x81); // display enabled, 8x16 sprites
    vdp.write_control(0x7E);
    vdp.write_control(0x85); // register 5: sprite table at $3f00

    vdp.write_control(0x11);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C);

    vdp.write_control(0x00);
    vdp.write_control(0x40); // tile 0 is empty
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x20);
    vdp.write_control(0x40); // tile 1, first row set
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x7F);
    vdp.write_data(0xFF);
    vdp.write_data(0xD0);
    vdp.write_control(0x80);
    vdp.write_control(0x7F);
    vdp.write_data(0x04);
    vdp.write_data(0x00);

    for (int i = 0; i < 9; ++i) {
        vdp.tick(228);
    }
    assert(vdp.framebuffer()[8 * Vdp::width + 4] == 0xFF00FF00);
}

void test_vdp_sprite_y_wraps_above_top() {
    Vdp vdp;
    vdp.write_control(0x40);
    vdp.write_control(0x81); // register 1: display enabled
    vdp.write_control(0x7E);
    vdp.write_control(0x85); // register 5: sprite table at $3f00

    vdp.write_control(0x11);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C);

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    for (int row = 0; row < 8; ++row) {
        vdp.write_data(row == 4 ? 0x80 : 0x00);
        vdp.write_data(0x00);
        vdp.write_data(0x00);
        vdp.write_data(0x00);
    }

    vdp.write_control(0x00);
    vdp.write_control(0x7F);
    vdp.write_data(0xFC); // y = -3, row 4 appears on scanline 1
    vdp.write_data(0xD0);
    vdp.write_control(0x80);
    vdp.write_control(0x7F);
    vdp.write_data(0x04);
    vdp.write_data(0x00);

    vdp.tick(228);
    vdp.tick(228);
    assert(vdp.framebuffer()[Vdp::width + 4] == 0xFF00FF00);
    assert(vdp.debug_sprites()[0].y == -3);
}

void test_vdp_sprite_collision_and_overflow_flags() {
    Vdp vdp;
    vdp.write_control(0x40);
    vdp.write_control(0x81); // register 1: display enabled
    vdp.write_control(0x7E);
    vdp.write_control(0x85); // register 5: sprite table at $3f00

    vdp.write_control(0x11);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C);

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x7F);
    for (int i = 0; i < 9; ++i) {
        vdp.write_data(0xFF);
    }
    vdp.write_data(0xD0);

    vdp.write_control(0x80);
    vdp.write_control(0x7F);
    for (int i = 0; i < 9; ++i) {
        vdp.write_data(0x04);
        vdp.write_data(0x00);
    }

    vdp.tick(228);
    const u8 status = vdp.read_status();

    assert((status & 0x20) != 0);
    assert((status & 0x40) != 0);
}

void test_vdp_overlapping_sprites_keep_lower_index_priority() {
    Vdp vdp;
    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x7E);
    vdp.write_control(0x85); // SAT at $3f00

    vdp.write_control(0x11);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C); // sprite color 1: green
    vdp.write_control(0x12);
    vdp.write_control(0xC0);
    vdp.write_data(0x30); // sprite color 2: blue

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x80); // tile 0, color 1 at first pixel
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_control(0x20);
    vdp.write_control(0x40);
    vdp.write_data(0x00); // tile 1, color 2 at first pixel
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x7F);
    vdp.write_data(0xFF);
    vdp.write_data(0xFF);
    vdp.write_data(0xD0);
    vdp.write_control(0x80);
    vdp.write_control(0x7F);
    vdp.write_data(0x04);
    vdp.write_data(0x00);
    vdp.write_data(0x04);
    vdp.write_data(0x01);

    vdp.tick(228);
    assert(vdp.framebuffer()[4] == 0xFF00FF00); // sprite 0 remains visible
    assert((vdp.read_status() & 0x20) != 0);
}

void test_tms_overlapping_sprites_keep_lower_index_priority() {
    Console console(ConsoleModel::SG3000);
    Vdp& vdp = console.vdp();
    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x36);
    vdp.write_control(0x85); // SAT at $1b00
    vdp.write_control(0x00);
    vdp.write_control(0x86); // sprite patterns at $0000

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_control(0x00);
    vdp.write_control(0x5B);
    vdp.write_data(0xFF);
    vdp.write_data(0x04);
    vdp.write_data(0x00);
    vdp.write_data(0x02); // sprite 0: green
    vdp.write_data(0xFF);
    vdp.write_data(0x04);
    vdp.write_data(0x00);
    vdp.write_data(0x04); // sprite 1: blue
    vdp.write_data(0xD0);

    vdp.tick(228);
    assert(vdp.framebuffer()[4] == 0xFF21C842); // sprite 0 remains visible
    assert((vdp.read_status() & 0x20) != 0);
}

void test_tms_sprite_overflow_reports_fifth_sprite_index() {
    Console console(ConsoleModel::SG3000);
    Vdp& vdp = console.vdp();
    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x36);
    vdp.write_control(0x85); // SAT at $1b00

    vdp.write_control(0x00);
    vdp.write_control(0x5B);
    for (u8 sprite = 0; sprite < 5; ++sprite) {
        vdp.write_data(0xFF); // all visible on line zero
        vdp.write_data(static_cast<u8>(sprite * 8));
        vdp.write_data(0x00);
        vdp.write_data(0x00); // transparent still participates in evaluation
    }
    vdp.write_data(0xD0);

    vdp.tick(228);
    const u8 status = vdp.read_status();
    assert((status & 0x40) != 0);
    assert((status & 0x1F) == 4);
    assert(vdp.read_status() == 0);
}

void test_tms_enhanced_sprites_do_not_create_hardware_collision() {
    Console console(ConsoleModel::SG3000);
    Vdp& vdp = console.vdp();
    EnhancementConfig config;
    config.mode = RuntimeMode::Enhanced;
    config.disable_sprite_limit = true;
    vdp.set_enhancements(config);
    vdp.write_control(0x40);
    vdp.write_control(0x81); // display enabled
    vdp.write_control(0x36);
    vdp.write_control(0x85); // SAT at $1b00
    vdp.write_control(0x00);
    vdp.write_control(0x86); // sprite patterns at $0000

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x80); // one opaque pixel in pattern zero
    vdp.write_control(0x00);
    vdp.write_control(0x5B);
    for (u8 sprite = 0; sprite < 5; ++sprite) {
        vdp.write_data(0xFF);
        vdp.write_data(sprite == 4 ? 0 : static_cast<u8>(sprite * 8));
        vdp.write_data(0x00);
        vdp.write_data(0x02);
    }
    vdp.write_data(0xD0);

    vdp.tick(228);
    assert(vdp.framebuffer()[0] == 0xFF21C842);
    const u8 status = vdp.read_status();
    assert((status & 0x40) != 0);
    assert((status & 0x20) == 0);
}

void test_vdp_background_priority_hides_sprite_pixel() {
    Vdp vdp;
    vdp.write_control(0x40);
    vdp.write_control(0x81); // register 1: display enabled
    vdp.write_control(0x00);
    vdp.write_control(0x82); // register 2: name table base $0000
    vdp.write_control(0x85);
    vdp.write_control(0x85); // register 5: sprite table base $0200

    vdp.write_control(0x01);
    vdp.write_control(0xC0);
    vdp.write_data(0x03); // background color 1: red
    vdp.write_control(0x11);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C); // sprite color 1: green

    vdp.write_control(0x20);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_control(0x40);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x01);
    vdp.write_data(0x10); // tile 1 with priority bit
    vdp.write_control(0x00);
    vdp.write_control(0x42);
    vdp.write_data(0xFF); // y=0
    vdp.write_control(0x80);
    vdp.write_control(0x42);
    vdp.write_data(0x00);
    vdp.write_data(0x02);

    vdp.tick(228);
    assert(vdp.framebuffer()[0] == 0xFFFF0000);
}

void test_vdp_sprite_overlays_non_priority_background_pixel() {
    Vdp vdp;
    vdp.write_control(0x40);
    vdp.write_control(0x81); // register 1: display enabled
    vdp.write_control(0x00);
    vdp.write_control(0x82); // register 2: name table base $0000
    vdp.write_control(0x85);
    vdp.write_control(0x85); // register 5: sprite table base $0200

    vdp.write_control(0x01);
    vdp.write_control(0xC0);
    vdp.write_data(0x03);
    vdp.write_control(0x11);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C);

    vdp.write_control(0x20);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_control(0x40);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x01);
    vdp.write_data(0x00); // tile 1 without priority
    vdp.write_control(0x00);
    vdp.write_control(0x42);
    vdp.write_data(0xFF);
    vdp.write_control(0x80);
    vdp.write_control(0x42);
    vdp.write_data(0x00);
    vdp.write_data(0x02);

    vdp.tick(228);
    assert(vdp.framebuffer()[0] == 0xFF00FF00);
}

void setup_visible_sprites(Vdp& vdp, int count) {
    vdp.write_control(0x40);
    vdp.write_control(0x81); // register 1: display enabled
    vdp.write_control(0x7E);
    vdp.write_control(0x85); // register 5: sprite table at $3f00

    vdp.write_control(0x11);
    vdp.write_control(0xC0);
    vdp.write_data(0x0C); // green

    vdp.write_control(0x00);
    vdp.write_control(0x40); // tile 0, first pixel set
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x7F); // sprite y table
    for (int i = 0; i < count; ++i) {
        vdp.write_data(0xFF);
    }
    vdp.write_data(0xD0);

    vdp.write_control(0x80);
    vdp.write_control(0x7F); // sprite attributes
    for (int i = 0; i < count; ++i) {
        vdp.write_data(static_cast<u8>(i * 8));
        vdp.write_data(0x00);
    }
}

void test_vdp_sprite_limit_enhancement() {
    Vdp accurate;
    setup_visible_sprites(accurate, 9);
    accurate.tick(228);
    assert(accurate.framebuffer()[0] == 0xFF00FF00);
    assert(accurate.framebuffer()[0x38] == 0xFF00FF00);
    assert(accurate.framebuffer()[0x40] == 0xFF000000);
    assert((accurate.read_status() & 0x40) != 0);

    Vdp enhanced;
    EnhancementConfig config;
    config.mode = RuntimeMode::Enhanced;
    config.disable_sprite_limit = true;
    enhanced.set_enhancements(config);
    setup_visible_sprites(enhanced, 9);
    enhanced.tick(228);
    assert(enhanced.framebuffer()[0] == 0xFF00FF00);
    assert(enhanced.framebuffer()[0x40] == 0xFF00FF00);
    assert((enhanced.read_status() & 0x40) != 0);
}

void test_vdp_enhanced_sprites_do_not_create_hardware_collision() {
    Vdp vdp;
    EnhancementConfig config;
    config.mode = RuntimeMode::Enhanced;
    config.disable_sprite_limit = true;
    vdp.set_enhancements(config);
    setup_visible_sprites(vdp, 9);

    vdp.write_control(0x90);
    vdp.write_control(0x7F); // ninth sprite attribute at $3f90
    vdp.write_data(0x00);    // overlap sprite zero beyond the hardware limit

    vdp.tick(228);
    assert(vdp.framebuffer()[0] == 0xFF00FF00);
    const u8 status = vdp.read_status();
    assert((status & 0x40) != 0);
    assert((status & 0x20) == 0);
}

void test_vdp_reduce_flicker_uses_conservative_sprite_limit() {
    Vdp reduced;
    EnhancementConfig config;
    config.mode = RuntimeMode::Enhanced;
    config.reduce_flicker = true;
    reduced.set_enhancements(config);
    setup_visible_sprites(reduced, 17);
    reduced.tick(228);
    assert(reduced.framebuffer()[0x78] == 0xFF00FF00);
    assert(reduced.framebuffer()[0x80] == 0xFF000000);
    assert((reduced.read_status() & 0x40) != 0);

    Vdp unlimited;
    config.disable_sprite_limit = true;
    unlimited.set_enhancements(config);
    setup_visible_sprites(unlimited, 17);
    unlimited.tick(228);
    assert(unlimited.framebuffer()[0x80] == 0xFF00FF00);
    assert((unlimited.read_status() & 0x40) != 0);
}

void test_vdp_debug_tilemap_and_sprite_snapshots() {
    Vdp vdp;
    vdp.write_control(0x00);
    vdp.write_control(0x82); // register 2: name table base $0000
    vdp.write_control(0x85);
    vdp.write_control(0x85); // register 5: sprite table base $0200

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x23);
    vdp.write_data(0x0E); // tile $023, palette 1, flip x/y

    vdp.write_control(0x00);
    vdp.write_control(0x42);
    vdp.write_data(0x20); // sprite raw y

    vdp.write_control(0x80);
    vdp.write_control(0x42);
    vdp.write_data(0x40); // sprite x
    vdp.write_data(0x07); // sprite tile

    const auto tilemap = vdp.debug_tilemap();
    assert(tilemap.size() == 1024);
    assert(tilemap[0].tile == 0x023);
    assert(tilemap[0].palette1);
    assert(tilemap[0].flip_x);
    assert(tilemap[0].flip_y);

    const auto sprites = vdp.debug_sprites();
    assert(!sprites.empty());
    assert(sprites[0].raw_y == 0x20);
    assert(sprites[0].y == 0x21);
    assert(sprites[0].x == 0x40);
    assert(sprites[0].tile == 0x07);
}

void test_bus_io_logging_records_reads_and_writes() {
    Vdp vdp;
    Psg psg;
    Ym2413 ym2413;
    Joypad joypad;
    Bus bus(ConsoleModel::SMS, vdp, psg, ym2413, joypad);

    bus.set_io_logging_enabled(true);
    bus.set_cycle(12);
    bus.output(0x7F, 0x90);
    bus.set_cycle(24);
    (void)bus.input(0xDC);

    const auto& log = bus.logged_io();
    assert(log.size() == 2);
    assert(log[0].cycle == 12);
    assert(log[0].write);
    assert(log[0].port == 0x7F);
    assert(log[0].value == 0x90);
    assert(log[1].cycle == 24);
    assert(!log[1].write);
    assert(log[1].port == 0xDC);
}

void test_bus_mirrors_vdp_psg_and_counter_ports() {
    Vdp vdp;
    Psg psg;
    Ym2413 ym2413;
    Joypad joypad;
    Bus bus(ConsoleModel::SMS, vdp, psg, ym2413, joypad);

    psg.set_write_logging_enabled(true);
    bus.output(0x40, 0x90);
    bus.output(0x7F, 0x91);
    assert(psg.logged_writes().size() == 2);
    assert(psg.logged_writes()[0].value == 0x90);
    assert(psg.logged_writes()[1].value == 0x91);

    bus.output(0x81, 0x05);
    bus.output(0x81, 0x87);
    assert(vdp.debug_registers()[7] == 0x05);

    bus.output(0x81, 0x10);
    bus.output(0x81, 0x40);
    bus.output(0x80, 0xAA);
    assert(vdp.debug_vram()[0x0010] == 0xAA);

    assert(bus.input(0x40) == vdp.read_v_counter());
    assert(bus.input(0x41) == vdp.read_h_counter());
}

void test_bus_memory_logging_records_ram_mapper_and_cartridge_ram() {
    Vdp vdp;
    Psg psg;
    Ym2413 ym2413;
    Joypad joypad;
    Bus bus(ConsoleModel::SMS, vdp, psg, ym2413, joypad);
    std::vector<u8> rom(0x10000, 0x00);
    bus.load_rom(rom);

    bus.set_memory_logging_enabled(true);
    bus.set_cycle(10);
    bus.write(0xC123, 0x5A);
    bus.set_cycle(20);
    bus.write(0xFFFC, 0x08);
    bus.set_cycle(30);
    bus.write(0x8000, 0xA5);

    const auto& log = bus.logged_memory();
    bool saw_ram = false;
    bool saw_mapper = false;
    bool saw_cartridge_ram = false;
    for (const auto& access : log) {
        saw_ram = saw_ram ||
                  (access.kind == BusMemoryAccessKind::Ram && access.address == 0xC123 && access.physical == 0xC123);
        saw_mapper = saw_mapper || (access.kind == BusMemoryAccessKind::Mapper && access.address == 0xFFFC);
        saw_cartridge_ram = saw_cartridge_ram || (access.kind == BusMemoryAccessKind::CartridgeRam &&
                                                  access.address == 0x8000 && access.physical == 0x0000);
    }
    assert(saw_ram);
    assert(saw_mapper);
    assert(saw_cartridge_ram);
}

void test_vdp_access_logging_records_register_vram_and_cram_writes() {
    Vdp vdp;
    vdp.set_access_logging_enabled(true);
    vdp.set_cycle(11);
    vdp.write_control(0x40);
    vdp.write_control(0x81);
    vdp.set_cycle(22);
    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0xAA);
    vdp.set_cycle(33);
    vdp.write_control(0x00);
    vdp.write_control(0xC0);
    vdp.write_data(0x03);

    const auto& log = vdp.logged_accesses();
    assert(log.size() == 3);
    assert(log[0].kind == VdpAccessKind::Register);
    assert(log[0].address == 1);
    assert(log[0].value == 0x40);
    assert(log[1].kind == VdpAccessKind::Vram);
    assert(log[1].address == 0x0000);
    assert(log[1].value == 0xAA);
    assert(log[2].kind == VdpAccessKind::Cram);
    assert(log[2].address == 0x0000);
    assert(log[2].value == 0x03);
}

void test_vdp_debug_snapshot_reports_timing_and_status() {
    Vdp vdp;
    auto snapshot = vdp.debug_snapshot();
    assert(snapshot.scanline == 0);
    assert(!snapshot.display_enabled);
    assert(!snapshot.frame_irq_pending);

    vdp.write_control(0x60);
    vdp.write_control(0x81); // display + frame IRQ enabled
    snapshot = vdp.debug_snapshot();
    assert(snapshot.display_enabled);
    assert(snapshot.frame_irq_enabled);

    vdp.tick(228 * Vdp::height);
    snapshot = vdp.debug_snapshot();
    assert(snapshot.scanline == Vdp::height);
    assert(snapshot.frame_irq_pending);
    assert((snapshot.status & 0x80) != 0);
    assert(vdp.irq_pending());

    (void)vdp.read_status();
    snapshot = vdp.debug_snapshot();
    assert(!snapshot.frame_irq_pending);
    assert(!vdp.irq_pending());
}

void test_vdp_custom_timing_controls_scanline_wrap() {
    Vdp vdp;
    vdp.set_timing({200, 313});
    auto snapshot = vdp.debug_snapshot();
    assert(snapshot.cpu_cycles_per_scanline == 200);
    assert(snapshot.scanlines_per_frame == 313);

    vdp.tick(199);
    assert(vdp.debug_snapshot().scanline == 0);
    assert(vdp.debug_snapshot().scanline_cycles == 199);
    vdp.tick(1);
    assert(vdp.debug_snapshot().scanline == 1);
    assert(vdp.debug_snapshot().scanline_cycles == 0);

    vdp.tick(200 * 312);
    snapshot = vdp.debug_snapshot();
    assert(snapshot.scanline == 0);
}

void test_vdp_extended_mode_heights_and_vblank() {
    const auto configure_height = [](Vdp& vdp, bool mode_240) {
        vdp.write_control(0x02);
        vdp.write_control(0x80); // M2 selects extended Mode 4
        vdp.write_control(static_cast<u8>(mode_240 ? 0x10 : 0x00));
        vdp.write_control(0x81); // M1 selects 240 instead of 224 lines
        vdp.write_control(0x10);
        vdp.write_control(0xC0);
        vdp.write_data(0x03); // backdrop red, visible even with display disabled
    };

    Vdp mode_224;
    configure_height(mode_224, false);
    assert(mode_224.active_height() == 224);
    mode_224.tick(228 * 224);
    assert(mode_224.debug_snapshot().scanline == 224);
    assert((mode_224.debug_snapshot().status & 0x80) != 0);
    assert(mode_224.framebuffer()[223 * Vdp::width] == 0xFFFF0000);

    Vdp mode_240;
    configure_height(mode_240, true);
    assert(mode_240.active_height() == 240);
    mode_240.tick(228 * 240);
    assert(mode_240.debug_snapshot().scanline == 240);
    assert((mode_240.debug_snapshot().status & 0x80) != 0);
    assert(mode_240.framebuffer()[239 * Vdp::width] == 0xFFFF0000);
}

void test_host_runtime_config_sets_vdp_timing() {
    HostRuntimeConfig config;
    config.cpu_cycles_per_scanline = 200;
    config.scanlines_per_frame = 313;
    config.audio_sample_rate = 22050;
    HostRuntime host(ConsoleModel::SMS, config);
    const auto snapshot = host.console().vdp().debug_snapshot();
    assert(snapshot.cpu_cycles_per_scanline == 200);
    assert(snapshot.scanlines_per_frame == 313);
    assert(host.config().cycles_per_frame() == 62600);
    assert(host.config().audio_sample_rate == 22050);
}

void test_console_enhancement_config_propagates_to_runtime_devices() {
    Console console(ConsoleModel::SMS);
    assert(console.enhancements().mode == RuntimeMode::Accurate);
    assert(!console.vdp().enhancements().disable_sprite_limit);
    assert(!console.psg().enhancements().disable_sprite_limit);
    assert(!console.ym2413().present());

    EnhancementConfig config;
    config.mode = RuntimeMode::Enhanced;
    config.disable_sprite_limit = true;
    config.reduce_flicker = true;
    config.enable_fm = true;
    console.set_enhancements(config);

    assert(console.enhancements().mode == RuntimeMode::Enhanced);
    assert(console.vdp().enhancements().disable_sprite_limit);
    assert(console.vdp().enhancements().reduce_flicker);
    assert(console.psg().enhancements().disable_sprite_limit);
    assert(console.psg().enhancements().reduce_flicker);
    assert(console.ym2413().present());
}

void test_psg_tone_generates_sample() {
    Psg psg;
    psg.write(0x80 | 0x01); // tone channel 0 low bits
    psg.write(0x00);        // tone channel 0 high bits
    psg.write(0x90 | 0x00); // channel 0 volume max

    const auto before = psg.sample();
    psg.tick(32);
    const auto after = psg.sample();

    assert(before[0] != after[0]);
    assert(after[0] == after[1]);
}

void test_ym2413_audio_control_and_register_writes() {
    EnhancementConfig config;
    config.enable_fm = true;
    Console console(ConsoleModel::SMS, config);

    assert(console.ym2413().present());
    assert(console.bus().input(0xF2) == 0x00);
    console.bus().output(0xF2, 0x01);
    assert(console.ym2413().fm_enabled());
    assert(!console.ym2413().psg_enabled());

    console.bus().output(0xF0, 0x20);
    console.bus().output(0xF1, 0x11);
    assert(console.ym2413().selected_register() == 0x20);
    assert(console.ym2413().debug_registers()[0x20] == 0x11);

    console.bus().output(0xF0, 0x10);
    console.bus().output(0xF1, 0x80);
    console.bus().output(0xF0, 0x30);
    console.bus().output(0xF1, 0x00);
    console.ym2413().tick(4096);
    const auto sample = console.ym2413().sample();
    assert(sample[0] == sample[1]);
}

void test_ym2413_absent_audio_control_probe() {
    Console console(ConsoleModel::SMS);
    assert(!console.ym2413().present());
    assert(console.bus().input(0xF2) == 0x02);
    console.bus().output(0xF2, 0x01);
    assert(!console.ym2413().fm_enabled());
}

void test_ym2612_experimental_ports_audio_and_state() {
    Console accurate(ConsoleModel::SMS);
    accurate.bus().output(0xF4, 0xA0);
    accurate.bus().output(0xF5, 0x69);
    assert(!accurate.ym2612().enabled());
    assert(accurate.ym2612().debug_registers()[0xA0] == 0);

    EnhancementConfig config;
    config.mode = RuntimeMode::Enhanced;
    config.enable_ym2612 = true;
    Console console(ConsoleModel::SMS, config);
    assert(console.ym2612().enabled());

    const auto write = [&](int bank, u8 address, u8 value) {
        console.bus().output(bank == 0 ? 0xF4 : 0xF6, address);
        console.run_cycles(64);
        console.bus().output(bank == 0 ? 0xF5 : 0xF7, value);
        console.run_cycles(64);
    };

    write(0, 0xA0, 0x69);
    write(0, 0xA4, 0x22);
    for (u8 slot = 0; slot < 4; ++slot) {
        write(0, static_cast<u8>(0x30 + slot * 4), 0x01);
        write(0, static_cast<u8>(0x40 + slot * 4), 0x00);
        write(0, static_cast<u8>(0x50 + slot * 4), 0x1F);
        write(0, static_cast<u8>(0x80 + slot * 4), 0x0F);
    }
    write(0, 0xB0, 0x07);
    write(0, 0xB4, 0xC0);
    write(1, 0xA0, 0x55);
    assert(console.ym2612().selected_register(0) == 0xB4);
    assert(console.ym2612().selected_register(1) == 0xA0);
    assert(console.ym2612().debug_registers()[0x1A0] == 0x55);
    assert(console.bus().input(0xF4) != 0xFF);

    write(0, 0x28, 0xF0);
    assert(console.ym2612().channel_key_on(0));
    console.run_cycles(32768);
    const auto tone = console.ym2612().sample();
    assert(std::abs(tone[0]) > 0.0001F || std::abs(tone[1]) > 0.0001F);

    const auto saved = console.save_state();
    const auto serialized = serialize_console_state(saved);
    const auto serialized_state = deserialize_console_state(serialized);
    assert(serialized_state.ym2612.enabled);
    assert(!serialized_state.ym2612.core_state.empty());
    assert(serialized_state.ym2612.registers[0xA0] == 0x69);
    write(0, 0x28, 0x00);
    assert(!console.ym2612().channel_key_on(0));
    console.load_state(saved);
    assert(console.ym2612().enabled());
    assert(console.enhancements().enable_ym2612);
    assert(console.ym2612().channel_key_on(0));
    assert(console.ym2612().debug_registers()[0xA0] == 0x69);

    write(0, 0x2B, 0x80);
    write(0, 0x2A, 0xFF);
    const auto dac = console.ym2612().sample();
    assert(dac[0] > 0.0F && dac[1] > 0.0F);
}

void test_host_runtime_mixes_ym2612_when_enabled() {
    EnhancementConfig config;
    config.mode = RuntimeMode::Enhanced;
    config.enable_ym2612 = true;
    HostRuntime host(ConsoleModel::SMS, config);
    host.load_rom(std::vector<u8>(0x8000, 0x00));
    const auto write = [&](u8 address, u8 value) {
        host.console().bus().output(0xF4, address);
        host.console().run_cycles(64);
        host.console().bus().output(0xF5, value);
        host.console().run_cycles(64);
    };
    write(0xA0, 0x69);
    write(0xA4, 0x22);
    for (u8 slot = 0; slot < 4; ++slot) {
        write(static_cast<u8>(0x30 + slot * 4), 0x01);
        write(static_cast<u8>(0x40 + slot * 4), 0x00);
        write(static_cast<u8>(0x50 + slot * 4), 0x1F);
        write(static_cast<u8>(0x80 + slot * 4), 0x0F);
    }
    write(0xB0, 0x07);
    write(0xB4, 0xC0);
    write(0x28, 0xF0);
    host.run_frame();
    assert(std::any_of(host.audio().begin(), host.audio().end(), [](s16 sample) { return sample != 0; }));
}

void test_misc_jumps_and_flags() {
    std::vector<u8> rom(0x40, 0x00);
    rom[0x00] = 0x3E; // ld a,$55
    rom[0x01] = 0x55;
    rom[0x02] = 0x2F; // cpl -> $aa
    rom[0x03] = 0x37; // scf
    rom[0x04] = 0x3F; // ccf
    rom[0x05] = 0x21; // ld hl,$0010
    rom[0x06] = 0x10;
    rom[0x07] = 0x00;
    rom[0x08] = 0xE9; // jp (hl)
    rom[0x10] = 0x76; // halt

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0xAA);
    assert((console.cpu().f & 0x01) == 0);
    assert((console.cpu().f & 0x10) != 0);
}

void test_v_counter_port() {
    const std::vector<u8> rom = {
        0xDB,
        0x7E, // in a,($7e)
        0xFE,
        0xB0, // cp $b0
        0x20,
        0xFA, // jr nz,-6
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console, 100000);

    assert(console.cpu().a == 0xB0);
}

void test_v_counter_uses_video_standard_timing() {
    Vdp ntsc;
    ntsc.tick(228 * 0xDB);
    assert(ntsc.debug_snapshot().scanline == 0xDB);
    assert(ntsc.read_v_counter() == 0xD5);

    Vdp pal;
    pal.set_timing({228, 313});
    pal.tick(228 * 0xF2);
    assert(pal.read_v_counter() == 0xF2);
    pal.tick(228);
    assert(pal.debug_snapshot().scanline == 0xF3);
    assert(pal.read_v_counter() == 0xBA);
}

void test_v_counter_tracks_extended_mode_discontinuities() {
    const auto select_height = [](Vdp& vdp, bool mode_240) {
        vdp.write_control(0x02);
        vdp.write_control(0x80);
        vdp.write_control(static_cast<u8>(mode_240 ? 0x10 : 0x00));
        vdp.write_control(0x81);
    };

    Vdp ntsc_224;
    select_height(ntsc_224, false);
    ntsc_224.tick(228 * 0xEA);
    assert(ntsc_224.read_v_counter() == 0xEA);
    ntsc_224.tick(228);
    assert(ntsc_224.read_v_counter() == 0xE5);

    Vdp ntsc_240;
    select_height(ntsc_240, true);
    ntsc_240.tick(228 * 0xFF);
    assert(ntsc_240.read_v_counter() == 0xFF);
    ntsc_240.tick(228);
    assert(ntsc_240.read_v_counter() == 0x00);

    Vdp pal_224;
    pal_224.set_timing({228, 313});
    select_height(pal_224, false);
    pal_224.tick(228 * 258);
    assert(pal_224.read_v_counter() == 0x02);
    pal_224.tick(228);
    assert(pal_224.read_v_counter() == 0xCA);

    Vdp pal_240;
    pal_240.set_timing({228, 313});
    select_height(pal_240, true);
    pal_240.tick(228 * 266);
    assert(pal_240.read_v_counter() == 0x0A);
    pal_240.tick(228);
    assert(pal_240.read_v_counter() == 0xD2);
}

void test_index_register_basics() {
    std::vector<u8> rom(0x80, 0x00);
    rom[0x00] = 0x31; // ld sp,$c100
    rom[0x01] = 0x00;
    rom[0x02] = 0xC1;
    rom[0x03] = 0xDD; // ld ix,$1234
    rom[0x04] = 0x21;
    rom[0x05] = 0x34;
    rom[0x06] = 0x12;
    rom[0x07] = 0xFD; // ld iy,$5678
    rom[0x08] = 0x21;
    rom[0x09] = 0x78;
    rom[0x0A] = 0x56;
    rom[0x0B] = 0xDD; // push ix
    rom[0x0C] = 0xE5;
    rom[0x0D] = 0xE1; // pop hl
    rom[0x0E] = 0xFD; // push iy
    rom[0x0F] = 0xE5;
    rom[0x10] = 0xD1; // pop de
    rom[0x11] = 0xDD; // ld ix,$0040
    rom[0x12] = 0x21;
    rom[0x13] = 0x40;
    rom[0x14] = 0x00;
    rom[0x15] = 0xDD; // jp (ix)
    rom[0x16] = 0xE9;
    rom[0x40] = 0x76; // halt

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().hl() == 0x1234);
    assert(console.cpu().de() == 0x5678);
    assert(console.cpu().pc == 0x0041);
}

void test_accumulator_rotates() {
    const std::vector<u8> rom = {
        0x3E,
        0x81, // ld a,$81
        0x07, // rlca -> $03 carry
        0x17, // rla -> $07 carry clear
        0x37, // scf
        0x1F, // rra -> $83 carry
        0x0F, // rrca -> $C1 carry
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0xC1);
    assert((console.cpu().f & 0x01) != 0);
}

void test_index_cb_operations() {
    const std::vector<u8> rom = {
        0xDD, 0x21, 0x00, 0xC0, // ld ix,$c000
        0xFD, 0x21, 0x10, 0xC0, // ld iy,$c010
        0x3E, 0x80,             // ld a,$80
        0x32, 0x01, 0xC0,       // ld ($c001),a
        0xDD, 0xCB, 0x01, 0x26, // sla (ix+1)
        0xDD, 0xCB, 0x01, 0xC6, // set 0,(ix+1)
        0xDD, 0xCB, 0x01, 0x46, // bit 0,(ix+1)
        0xDD, 0xCB, 0x01, 0x86, // res 0,(ix+1)
        0x3E, 0x02,             // ld a,$02
        0x32, 0x0F, 0xC0,       // ld ($c00f),a
        0xFD, 0xCB, 0xFF, 0x0E, // rrc (iy-1)
        0x3A, 0x0F, 0xC0,       // ld a,($c00f)
        0x76,                   // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.bus().read(0xC001) == 0x00);
    assert(console.cpu().a == 0x01);
}

void test_index_displacement_loads_and_alu() {
    const std::vector<u8> rom = {
        0xDD, 0x21, 0x00, 0xC0, // ld ix,$c000
        0xFD, 0x21, 0x10, 0xC0, // ld iy,$c010
        0xDD, 0x36, 0x02, 0x11, // ld (ix+2),$11
        0xFD, 0x36, 0xFE, 0x22, // ld (iy-2),$22
        0xDD, 0x6E, 0x02,       // ld l,(ix+2)
        0xFD, 0x46, 0xFE,       // ld b,(iy-2)
        0x3E, 0x10,             // ld a,$10
        0xDD, 0x86, 0x02,       // add a,(ix+2)
        0xFD, 0xA6, 0xFE,       // and (iy-2)
        0x76,                   // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().l == 0x11);
    assert(console.cpu().b == 0x22);
    assert(console.cpu().a == 0x20);
}

void test_index_high_low_register_operations() {
    const std::vector<u8> rom = {
        0xDD, 0x21, 0x34, 0x12, // ld ix,$1234
        0xDD, 0x24,             // inc ixh -> $13
        0xDD, 0x2D,             // dec ixl -> $33
        0xDD, 0x44,             // ld b,ixh
        0xDD, 0x4D,             // ld c,ixl
        0xDD, 0x26, 0x56,       // ld ixh,$56
        0xDD, 0x2E, 0x78,       // ld ixl,$78
        0xDD, 0x7C,             // ld a,ixh
        0xDD, 0x85,             // add a,ixl -> $ce
        0xFD, 0x21, 0x00, 0xC0, // ld iy,$c000
        0xFD, 0x2E, 0xA5,       // ld iyl,$a5
        0xFD, 0x75, 0x02,       // ld (iy+2),iyl
        0x76,                   // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().b == 0x13);
    assert(console.cpu().c == 0x33);
    assert(console.cpu().a == 0xCE);
    assert(console.bus().read(0xC0A7) == 0xA5);
}

void test_ed_adc_sbc_hl() {
    const std::vector<u8> rom = {
        0x21,
        0x00,
        0x10, // ld hl,$1000
        0x11,
        0x01,
        0x00, // ld de,$0001
        0x37, // scf
        0xED,
        0x5A, // adc hl,de -> $1002
        0xED,
        0x52, // sbc hl,de -> $1001
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().hl() == 0x1001);
}

void test_daa_after_add_and_subtract() {
    const std::vector<u8> rom = {
        0x3E,
        0x15, // ld a,$15
        0xC6,
        0x27, // add a,$27
        0x27, // daa -> $42
        0xD6,
        0x27, // sub $27
        0x27, // daa -> $15
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x15);
}

void test_mapper_keeps_ram() {
    std::vector<u8> rom(0x8000, 0x00);
    rom[0x00] = 0x3E; // ld a,$5a
    rom[0x01] = 0x5A;
    rom[0x02] = 0x32; // ld ($c000),a
    rom[0x03] = 0x00;
    rom[0x04] = 0xC0;
    rom[0x05] = 0x3E; // ld a,$01
    rom[0x06] = 0x01;
    rom[0x07] = 0x32; // ld ($ffff),a
    rom[0x08] = 0xFF;
    rom[0x09] = 0xFF;
    rom[0x0A] = 0x3A; // ld a,($c000)
    rom[0x0B] = 0x00;
    rom[0x0C] = 0xC0;
    rom[0x0D] = 0x76; // halt

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x5A);
    assert(console.bus().read(0xC000) == 0x5A);
}

void test_ram_mirroring() {
    const std::vector<u8> rom = {
        0x3E,
        0x5A, // ld a,$5a
        0x32,
        0x00,
        0xC0, // ld ($c000),a
        0x3A,
        0x00,
        0xE0, // ld a,($e000)
        0x32,
        0x00,
        0xF0, // ld ($f000),a
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x5A);
    assert(console.bus().read(0xC000) == 0x5A);
    assert(console.bus().read(0xE000) == 0x5A);
    assert(console.bus().read(0xD000) == 0x5A);
    assert(console.bus().read(0xF000) == 0x5A);
}

void test_sg3000_uses_one_kibibyte_ram_mirrored_through_upper_memory() {
    const std::vector<u8> rom(0x8000, 0x00);
    Console console(ConsoleModel::SG3000);
    console.load_rom(rom);

    console.bus().write(0xC123, 0x5A);
    assert(console.bus().read(0xC123) == 0x5A);
    assert(console.bus().read(0xC523) == 0x5A);
    assert(console.bus().read(0xD123) == 0x5A);
    assert(console.bus().read(0xF923) == 0x5A);
    assert(console.bus().debug_memory()[0xC523] == 0x5A);
    assert(console.bus().debug_memory()[0xF923] == 0x5A);

    console.bus().write(0xFE00, 0xA5);
    assert(console.bus().read(0xC200) == 0xA5);
    assert(console.bus().read(0xE600) == 0xA5);

    Console sms(ConsoleModel::SMS);
    sms.load_rom(rom);
    sms.bus().write(0xC123, 0x11);
    assert(sms.bus().read(0xE123) == 0x11);
    assert(sms.bus().read(0xC523) == 0x00); // SMS keeps its distinct 8 KiB offset
}

void test_bios_overlay_boots_before_rom() {
    const std::vector<u8> bios = {
        0x3E,
        0x99, // ld a,$99
        0x76, // halt
    };
    const std::vector<u8> rom = {
        0x3E,
        0x42, // ld a,$42
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_bios(bios);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x99);
    assert(console.bus().bios_enabled());
}

void test_bios_can_disable_itself_with_memory_control_port() {
    std::vector<u8> bios(8, 0x00);
    bios[0] = 0x3E; // ld a,$08
    bios[1] = 0x08;
    bios[2] = 0xD3; // out ($3e),a
    bios[3] = 0x3E;

    std::vector<u8> rom(8, 0x00);
    rom[0] = 0x3E; // ld a,$42
    rom[1] = 0x42;
    rom[2] = 0x76; // halt
    rom[4] = 0xC3; // jp $0000, fetched after BIOS overlay is disabled
    rom[5] = 0x00;
    rom[6] = 0x00;

    Console console(ConsoleModel::SMS);
    console.load_bios(bios);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x42);
    assert(!console.bus().bios_enabled());
}

void test_work_ram_starts_cleared() {
    const std::vector<u8> rom(0x8000, 0x00);
    Console console(ConsoleModel::SMS);
    console.load_rom(rom);

    assert(console.bus().read(0xC000) == 0x00);
    assert(console.bus().read(0xDFFF) == 0x00);
    assert(console.bus().read(0xE000) == 0x00);
    assert(console.bus().read(0xFFFF) == 0x00);
}

void test_memory_control_port_maps_bios_cart_and_ram() {
    std::vector<u8> bios(0x4000, 0x00);
    bios[0] = 0x11;
    std::vector<u8> rom(0x8000, 0x00);
    rom[0] = 0x22;

    Console console(ConsoleModel::SMS);
    console.load_bios(bios);
    console.load_rom(rom);
    assert(console.bus().bios_enabled());
    assert(console.bus().read(0x0000) == 0x11);

    console.bus().output(0x3E, 0x08);
    assert(!console.bus().bios_enabled());
    assert(console.bus().read(0x0000) == 0x22);

    console.bus().output(0x3E, 0x48);
    assert(!console.bus().bios_enabled());
    assert(!console.bus().cartridge_enabled());
    assert(console.bus().read(0x0000) == 0xFF);

    console.bus().write(0xC000, 0x5A);
    assert(console.bus().read(0xE000) == 0x5A);
    console.bus().output(0x3E, 0x58);
    assert(!console.bus().work_ram_enabled());
    console.bus().write(0xC000, 0xA5);
    assert(console.bus().read(0xC000) == 0xFF);

    console.bus().output(0x3E, 0x08);
    assert(console.bus().work_ram_enabled());
    assert(console.bus().read(0xC000) == 0x5A);
}

void test_memory_control_models_expansion_card_io_and_reserved_bits() {
    const std::vector<u8> rom(0x8000, 0x00);
    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    console.joypad().set_player1(static_cast<u8>(Joypad::Left | Joypad::Button1));

    assert(console.bus().expansion_enabled());
    assert(console.bus().card_enabled());
    assert(console.bus().io_chip_enabled());
    assert(console.bus().input(0xDC) != 0xFF);

    console.bus().output(0x3E, 0xA4); // disable expansion, card, and I/O chip
    assert(console.bus().memory_control() == 0xA4);
    assert(!console.bus().expansion_enabled());
    assert(!console.bus().card_enabled());
    assert(!console.bus().io_chip_enabled());
    assert(console.bus().cartridge_enabled());
    assert(console.bus().work_ram_enabled());
    assert(console.bus().input(0xDC) == 0xFF);
    assert(console.bus().input(0xDD) == 0xFF);

    console.bus().output(0x3E, 0x03); // reserved bits are retained without disabling hardware
    const BusMapperSnapshot snapshot = console.bus().mapper_snapshot();
    assert(snapshot.memory_control == 0x03);
    assert(snapshot.expansion_enabled);
    assert(snapshot.card_enabled);
    assert(snapshot.io_chip_enabled);
    assert(console.bus().input(0xDC) != 0xFF);
}

void test_smapper_cartridge_ram_banks() {
    std::vector<u8> rom(0x10000, 0x00);
    rom[0x8000] = 0x22;

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);

    assert(console.bus().read(0x8000) == 0x22);
    console.bus().write(0xFFFC, 0x08);
    assert(console.bus().cartridge_ram_enabled());
    assert(console.bus().cartridge_ram_bank() == 0);
    console.bus().write(0x8000, 0x5A);
    assert(console.bus().read(0x8000) == 0x5A);

    console.bus().write(0xFFFC, 0x0C);
    assert(console.bus().cartridge_ram_bank() == 1);
    assert(console.bus().read(0x8000) == 0x00);
    console.bus().write(0x8000, 0xA5);

    console.bus().write(0xFFFC, 0x08);
    assert(console.bus().read(0x8000) == 0x5A);
    console.bus().write(0xFFFC, 0x00);
    assert(!console.bus().cartridge_ram_enabled());
    assert(console.bus().read(0x8000) == 0x22);
}

void test_smapper_loads_cartridge_ram() {
    std::vector<u8> rom(0x10000, 0x00);
    std::vector<u8> save(0x8000, 0x00);
    save[0x0000] = 0x11;
    save[0x4000] = 0x22;

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    console.bus().load_cartridge_ram(save);
    assert(!console.bus().cartridge_ram_dirty());

    console.bus().write(0xFFFC, 0x08);
    assert(console.bus().read(0x8000) == 0x11);
    console.bus().write(0xFFFC, 0x0C);
    assert(console.bus().read(0x8000) == 0x22);
    console.bus().write(0x8000, 0x33);
    assert(console.bus().cartridge_ram_dirty());
    assert(console.bus().debug_cartridge_ram()[0x4000] == 0x33);
}

void test_smapper_fixed_window_and_register_mirroring() {
    std::vector<u8> rom(0x10000, 0x00);
    std::fill(rom.begin(), rom.begin() + 0x4000, 0x10);
    std::fill(rom.begin() + 0xC000, rom.end(), 0x40);

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    console.bus().write(0xFFFD, 0x03); // map bank 3 into slot zero

    assert(console.bus().read(0x0000) == 0x10);
    assert(console.bus().read(0x03FF) == 0x10);
    assert(console.bus().read(0x0400) == 0x40);
    assert(console.bus().read(0xDFFD) == 0x03);
    assert(console.bus().read(0xFFFD) == 0x03);
}

void test_smapper_sram_respects_cartridge_visibility_and_active_mapper() {
    std::vector<u8> rom(0x10000, 0x00);
    rom[0x8000] = 0x22;

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    console.bus().write(0xFFFC, 0x08);
    console.bus().write(0x8000, 0xA5);
    assert(console.bus().cartridge_ram_enabled());
    assert(console.bus().debug_cartridge_ram()[0] == 0xA5);

    console.bus().output(0x3E, 0x40); // disable the cartridge slot
    assert(!console.bus().cartridge_enabled());
    assert(!console.bus().cartridge_ram_enabled());
    assert(console.bus().read(0x8000) == 0xFF);
    console.bus().write(0x8000, 0x5A);
    console.bus().write(0xFFFF, 0x03);
    assert(console.bus().debug_cartridge_ram()[0] == 0xA5);

    console.bus().output(0x3E, 0x00);
    assert(console.bus().cartridge_ram_enabled());
    assert(console.bus().read(0x8000) == 0xA5);
    assert(console.bus().mapper_snapshot().smapper_slots[2] == 0x02);

    console.bus().set_mapper(CartridgeMapper::CMapper);
    assert(!console.bus().cartridge_ram_enabled());
    console.bus().write(0x8000, 0x01);
    assert(console.bus().read(0x8000) == 0x00);
    assert(console.bus().debug_cartridge_ram()[0] == 0xA5);
}

void test_auto_mapper_locks_after_first_hardware_family_evidence() {
    std::vector<u8> rom(0x20000, 0x00);
    for (std::size_t bank = 0; bank < 8; ++bank) {
        rom[bank * 0x4000] = static_cast<u8>(0x10 + bank);
    }

    Console sega(ConsoleModel::SMS);
    sega.load_rom(rom);
    assert(sega.bus().mapper() == CartridgeMapper::SMapper);
    sega.bus().write(0xFFFC, 0x80);
    sega.bus().write(0xFFFE, 0x03);
    assert(sega.bus().read(0x4000) == 0x13);
    sega.bus().write(0x0003, 0xFF); // incidental ROM-space write must not select K8K
    assert(sega.bus().mapper() == CartridgeMapper::SMapper);
    assert(sega.bus().read(0x4000) == 0x13);

    const BusState saved = sega.bus().save_state();
    assert(saved.auto_mapper_locked);
    const ConsoleState serialized = deserialize_console_state(serialize_console_state(sega.save_state()));
    assert(serialized.bus.auto_mapper_locked);
    Console restored(ConsoleModel::SMS);
    restored.load_rom(rom);
    restored.bus().load_state(saved);
    restored.bus().write(0x0003, 0x7F);
    assert(restored.bus().mapper() == CartridgeMapper::SMapper);
    assert(restored.bus().read(0x4000) == 0x13);

    Console codemasters(ConsoleModel::SMS);
    codemasters.load_rom(rom);
    codemasters.bus().write(0x4000, 0x03);
    assert(codemasters.bus().mapper() == CartridgeMapper::CMapper);
    codemasters.bus().write(0xA000, 0x07); // later noise cannot switch to KMapper
    assert(codemasters.bus().mapper() == CartridgeMapper::CMapper);
}

void test_mapper_snapshot_reports_active_mapper_and_banks() {
    std::vector<u8> rom(0x10000, 0x00);
    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    assert(std::string(cartridge_mapper_name(console.bus().mapper())) == "smapper");

    console.bus().write(0xFFFF, 0x03);
    auto snapshot = console.bus().mapper_snapshot();
    assert(snapshot.mapper == CartridgeMapper::SMapper);
    assert(snapshot.smapper_slots[2] == 0x03);
    assert(!snapshot.cartridge_ram_enabled);

    console.bus().write(0xFFFC, 0x0C);
    snapshot = console.bus().mapper_snapshot();
    assert(snapshot.cartridge_ram_enabled);
    assert(snapshot.cartridge_ram_bank == 1);
    assert(snapshot.smapper_control == 0x0C);
    console.bus().output(0x3E, 0x58);
    snapshot = console.bus().mapper_snapshot();
    assert(snapshot.memory_control == 0x58);
    assert(!snapshot.bios_enabled);
    assert(!snapshot.cartridge_enabled);
    assert(!snapshot.work_ram_enabled);
}

void test_cmapper_banks_and_auto_detection() {
    std::vector<u8> rom(0x10000, 0x00);
    rom[0x0000] = 0x10;
    rom[0x4000] = 0x11;
    rom[0x8000] = 0x12;
    rom[0xC000] = 0x13;

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    assert(console.bus().mapper() == CartridgeMapper::SMapper);

    console.bus().write(0x4000, 0x03);
    assert(console.bus().mapper() == CartridgeMapper::CMapper);
    assert(console.bus().read(0x4000) == 0x13);

    console.bus().write(0x8000, 0x01);
    assert(console.bus().read(0x8000) == 0x11);
}

void test_cmapper_8k_cartridge_ram_window() {
    std::vector<u8> rom(0x10000, 0x00);
    rom[0x6000] = 0x16; // bank 1, offset $2000
    rom[0xA000] = 0x26; // bank 2, offset $2000
    rom[0xE000] = 0x36; // bank 3, offset $2000

    Console console(ConsoleModel::SMS);
    console.bus().set_mapper(CartridgeMapper::CMapper);
    console.load_rom(rom);
    assert(console.bus().read(0xA000) == 0x26);

    console.bus().write(0x4000, 0x83); // bank 3 plus SRAM enable
    assert(console.bus().cartridge_ram_enabled());
    assert(console.bus().read(0x4000) == 0x00); // bank selection masks the RAM-enable bit
    assert(console.bus().read(0xA000) == 0x00);
    console.bus().write(0xA000, 0x5A);
    console.bus().write(0xBFFF, 0xA5);
    assert(console.bus().cartridge_ram_dirty());
    assert(console.bus().debug_cartridge_ram()[0x0000] == 0x5A);
    assert(console.bus().debug_cartridge_ram()[0x1FFF] == 0xA5);

    const BusState saved = console.bus().save_state();
    console.bus().write(0x4000, 0x03); // disable SRAM, retain bank 3
    assert(!console.bus().cartridge_ram_enabled());
    assert(console.bus().read(0xA000) == 0x26); // slot 2 ROM remains bank 2

    console.bus().load_state(saved);
    assert(console.bus().cartridge_ram_enabled());
    assert(console.bus().read(0xA000) == 0x5A);
    assert(console.bus().read(0xBFFF) == 0xA5);
}

void test_forced_kmapper_bank_switches_slot2() {
    std::vector<u8> rom(0x10000, 0x00);
    rom[0x0000] = 0x20;
    rom[0x4000] = 0x21;
    rom[0x8000] = 0x22;
    rom[0xC000] = 0x23;

    Console console(ConsoleModel::SMS);
    console.bus().set_mapper(CartridgeMapper::KMapper);
    console.load_rom(rom);
    assert(console.bus().read(0x0000) == 0x20);
    assert(console.bus().read(0x4000) == 0x21);
    assert(console.bus().read(0x8000) == 0x22);

    console.bus().write(0xA000, 0x03);
    assert(console.bus().read(0x8000) == 0x23);
}

void test_forced_k8k_mapper_uses_8k_banks() {
    std::vector<u8> rom(0x10000, 0x00);
    for (std::size_t bank = 0; bank < 8; ++bank) {
        rom[bank * 0x2000] = static_cast<u8>(0x30 + bank);
    }

    Console console(ConsoleModel::SMS);
    console.bus().set_mapper(CartridgeMapper::K8KMapper);
    console.load_rom(rom);
    assert(console.bus().read(0x0000) == 0x30);
    assert(console.bus().read(0x2000) == 0x31);
    assert(console.bus().read(0xA000) == 0x35);

    console.bus().write(0x0003, 0x07);
    assert(console.bus().read(0x6000) == 0x37);
}

void test_plain_mapper_keeps_first_48k_linear() {
    std::vector<u8> rom(0x10000, 0x00);
    rom[0x0000] = 0x40;
    rom[0x4000] = 0x41;
    rom[0x8000] = 0x42;
    rom[0xC000] = 0x43;

    Console console(ConsoleModel::SMS);
    console.bus().set_mapper(CartridgeMapper::Plain);
    console.load_rom(rom);
    assert(console.bus().read(0x0000) == 0x40);
    assert(console.bus().read(0x4000) == 0x41);
    assert(console.bus().read(0x8000) == 0x42);

    console.bus().write(0x4000, 0x03);
    assert(console.bus().mapper() == CartridgeMapper::Plain);
    assert(console.bus().read(0x4000) == 0x41);
}

void test_auto_mapper_uses_plain_for_small_linear_roms() {
    std::vector<u8> rom(0xC000, 0x00);
    rom[0x0000] = 0x50;
    rom[0x4000] = 0x51;
    rom[0x8000] = 0x52;

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    assert(console.bus().mapper() == CartridgeMapper::Plain);
    assert(console.bus().read(0x0000) == 0x50);
    assert(console.bus().read(0x4000) == 0x51);
    assert(console.bus().read(0x8000) == 0x52);
}

void test_copier_header_is_removed() {
    std::vector<u8> rom(512 + 0x4000, 0xEE);
    rom[512] = 0x3E; // ld a,$42
    rom[513] = 0x42;
    rom[514] = 0x76; // halt

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.bus().rom_header_removed());
    assert(console.cpu().a == 0x42);
}

void test_ei_delay_and_nmi_service() {
    std::vector<u8> rom(0x80, 0x00);
    rom[0x00] = 0xFB; // ei
    rom[0x01] = 0x00; // nop
    rom[0x02] = 0x76; // halt
    rom[0x66] = 0x3E; // nmi: ld a,$66
    rom[0x67] = 0x66;
    rom[0x68] = 0x76; // halt

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);

    execute_one(console.cpu(), console.bus());
    assert(!console.cpu().iff1);
    assert(console.cpu().ei_pending);
    assert(!service_maskable_interrupt(console.cpu(), console.bus()));

    execute_one(console.cpu(), console.bus());
    assert(console.cpu().iff1);
    assert(console.cpu().iff2);

    service_non_maskable_interrupt(console.cpu(), console.bus());
    assert(!console.cpu().iff1);
    assert(console.cpu().iff2);
    run_until_halt(console);
    assert(console.cpu().a == 0x66);
}

void test_bc_de_indirect_loads() {
    const std::vector<u8> rom = {
        0x01,
        0x00,
        0xC0, // ld bc,$c000
        0x11,
        0x01,
        0xC0, // ld de,$c001
        0x3E,
        0x12, // ld a,$12
        0x02, // ld (bc),a
        0x3E,
        0x34, // ld a,$34
        0x12, // ld (de),a
        0x0A, // ld a,(bc)
        0x1A, // ld a,(de)
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.bus().read(0xC000) == 0x12);
    assert(console.bus().read(0xC001) == 0x34);
    assert(console.cpu().a == 0x34);
}

void test_hl_absolute_load_store() {
    const std::vector<u8> rom = {
        0x21,
        0x34,
        0x12, // ld hl,$1234
        0x22,
        0x00,
        0xC0, // ld ($c000),hl
        0x21,
        0x00,
        0x00, // ld hl,$0000
        0x2A,
        0x00,
        0xC0, // ld hl,($c000)
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().hl() == 0x1234);
    assert(console.bus().read(0xC000) == 0x34);
    assert(console.bus().read(0xC001) == 0x12);
}

void test_interrupt_acknowledge_state_and_cycles() {
    {
        Console console(ConsoleModel::SMS);
        console.cpu().pc = 0x1234;
        console.cpu().sp = 0xD000;
        console.cpu().r = 0xFE;
        console.cpu().iff1 = true;
        console.cpu().iff2 = true;
        console.cpu().interrupt_mode = 1;
        console.cpu().halted = true;
        assert(service_maskable_interrupt(console.cpu(), console.bus()));
        assert(console.cpu().pc == 0x0038);
        assert(console.cpu().sp == 0xCFFE);
        assert(console.bus().read(0xCFFE) == 0x34);
        assert(console.bus().read(0xCFFF) == 0x12);
        assert(console.cpu().cycles == 13);
        assert(console.cpu().r == 0xFF);
        assert(!console.cpu().halted);
        assert(!console.cpu().iff1 && !console.cpu().iff2);
    }
    {
        Console console(ConsoleModel::SMS);
        console.cpu().pc = 0x2345;
        console.cpu().sp = 0xD000;
        console.cpu().i = 0xC0;
        console.cpu().r = 0x7F;
        console.cpu().iff1 = true;
        console.cpu().iff2 = true;
        console.cpu().interrupt_mode = 2;
        console.cpu().cycles = 7;
        console.bus().write(0xC0FF, 0x78);
        console.bus().write(0xC100, 0x56);
        assert(service_maskable_interrupt(console.cpu(), console.bus()));
        assert(console.cpu().pc == 0x5678);
        assert(console.cpu().cycles == 26);
        assert(console.cpu().r == 0x00);
        assert(console.bus().read(0xCFFE) == 0x45);
        assert(console.bus().read(0xCFFF) == 0x23);
    }
    {
        Console console(ConsoleModel::SMS);
        console.cpu().pc = 0x3456;
        console.cpu().sp = 0xD000;
        console.cpu().r = 0xFF;
        console.cpu().iff1 = true;
        console.cpu().iff2 = false;
        console.cpu().ei_pending = true;
        console.cpu().halted = true;
        console.cpu().cycles = 5;
        service_non_maskable_interrupt(console.cpu(), console.bus());
        assert(console.cpu().pc == 0x0066);
        assert(console.cpu().cycles == 16);
        assert(console.cpu().r == 0x80);
        assert(!console.cpu().halted);
        assert(!console.cpu().ei_pending);
        assert(!console.cpu().iff1 && console.cpu().iff2);
        assert(console.bus().read(0xCFFE) == 0x56);
        assert(console.bus().read(0xCFFF) == 0x34);
    }
    {
        Console console(ConsoleModel::SMS);
        console.cpu().pc = 0x4567;
        console.cpu().iff1 = true;
        console.cpu().ei_pending = true;
        console.cpu().r = 0x22;
        assert(!service_maskable_interrupt(console.cpu(), console.bus()));
        assert(console.cpu().pc == 0x4567);
        assert(console.cpu().r == 0x22);
        assert(console.cpu().cycles == 0);
    }
}

void setup_host_visible_pixel(Vdp& vdp) {
    vdp.write_control(0x00);
    vdp.write_control(0x80);
    vdp.write_control(0x40);
    vdp.write_control(0x81);
    vdp.write_control(0x0E);
    vdp.write_control(0x82);

    vdp.write_control(0x01);
    vdp.write_control(0xC0);
    vdp.write_data(0x03);

    vdp.write_control(0x00);
    vdp.write_control(0x40);
    vdp.write_data(0x80);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
    vdp.write_data(0x00);

    vdp.write_control(0x00);
    vdp.write_control(0x78);
    vdp.write_data(0x00);
    vdp.write_data(0x00);
}

HostInstructionResult synthetic_hybrid_executor(Z80State& cpu, Bus& bus) {
    const u16 pc = cpu.pc;
    execute_one(cpu, bus);
    return pc == 0 ? HostInstructionResult::Recompiled : HostInstructionResult::Fallback;
}

void test_host_runtime_frame_audio_and_input() {
    const std::vector<u8> rom = {
        0xDB, 0xDC,       // in a,($dc)
        0x32, 0x00, 0xC0, // ld ($c000),a
        0x3E, 0x80,       // ld a,$80
        0xD3, 0x7F,       // out ($7f),a
        0x3E, 0x00,       // ld a,$00
        0xD3, 0x7F,       // out ($7f),a
        0x3E, 0x90,       // ld a,$90
        0xD3, 0x7F,       // out ($7f),a
        0x18, 0xFE,       // jr -2
    };

    HostRuntime host(ConsoleModel::SMS);
    host.load_rom(rom);
    setup_host_visible_pixel(host.console().vdp());

    HostInputState input;
    input.player1 = static_cast<u8>(Joypad::Up | Joypad::Button1);
    const HostFrameResult frame = host.run_frame(input);

    assert(frame.frame_index == 0);
    assert(frame.end_cycle >= host.config().cycles_per_frame());
    assert(frame.stereo_samples > 700);
    assert(frame.instructions > 0);
    assert(frame.pc_min == 0x0000);
    assert(frame.pc_max >= 0x0011);
    assert(host.audio().size() == frame.stereo_samples * 2);
    assert((host.console().bus().read(0xC000) & 0x01) == 0);
    assert((host.console().bus().read(0xC000) & 0x10) == 0);
    assert(host.framebuffer()[0] == 0xFFFF0000);
}

void test_synthetic_rom_integrates_mapper_vdp_psg_and_input() {
    std::vector<u8> rom(0xC000, 0x00);
    const std::vector<u8> program = {
        0x3E, 0x02,             // ld a,$02
        0x32, 0xFD, 0xFF,       // ld ($fffd),a: page slot zero while fixed 1 KiB keeps executing
        0x3E, 0x0E, 0xD3, 0xBF, // VDP R2 value: name table at $3800
        0x3E, 0x82, 0xD3, 0xBF, // VDP register 2
        0x3E, 0x40, 0xD3, 0xBF, // VDP R1 value: display enabled
        0x3E, 0x81, 0xD3, 0xBF, // VDP register 1
        0x3E, 0x01, 0xD3, 0xBF, // CRAM address 1
        0x3E, 0xC0, 0xD3, 0xBF, 0x3E, 0x03, 0xD3, 0xBE,                         // palette color 1: red
        0x3E, 0x20, 0xD3, 0xBF,                                                 // VRAM tile 1, row zero
        0x3E, 0x40, 0xD3, 0xBF, 0x3E, 0x80, 0xD3, 0xBE,                         // plane zero, first pixel set
        0x3E, 0x00, 0xD3, 0xBE, 0xD3, 0xBE, 0xD3, 0xBE, 0x3E, 0x00, 0xD3, 0xBF, // VRAM name table at $3800
        0x3E, 0x78, 0xD3, 0xBF, 0x3E, 0x01, 0xD3, 0xBE,                         // tile 1, low entry byte
        0x3E, 0x00, 0xD3, 0xBE,                                                 // entry attributes
        0x3E, 0x81, 0xD3, 0x7F,                                                 // PSG tone channel zero period low
        0x3E, 0x00, 0xD3, 0x7F,                                                 // PSG period high
        0x3E, 0x90, 0xD3, 0x7F,                                                 // PSG channel zero at full volume
        0xDB, 0xDC,                                                             // in a,($dc): player one
        0x32, 0x00, 0xC0,                                                       // ld ($c000),a
        0x18, 0xFE,                                                             // jr -2
    };
    std::copy(program.begin(), program.end(), rom.begin());

    HostRuntime host(ConsoleModel::SMS);
    host.console().bus().set_mapper(CartridgeMapper::SMapper);
    host.load_rom(rom);
    HostInputState input;
    input.player1 = static_cast<u8>(Joypad::Right | Joypad::Button2);
    (void)host.run_frame(input); // program configures scanline zero after its first render
    const HostFrameResult frame = host.run_frame(input);

    const BusMapperSnapshot mapper = host.console().bus().mapper_snapshot();
    assert(mapper.mapper == CartridgeMapper::SMapper);
    assert(mapper.smapper_slots[0] == 2);
    assert((host.console().bus().read(0xC000) & Joypad::Right) == 0);
    assert((host.console().bus().read(0xC000) & Joypad::Button2) == 0);
    assert(host.framebuffer()[0] == 0xFFFF0000);
    assert(frame.stereo_samples > 0);
    assert(std::any_of(host.audio().begin(), host.audio().end(), [](s16 sample) { return sample != 0; }));
}

void test_host_runtime_stereo_mixer_respects_sample_rate() {
    const std::vector<u8> rom = {
        0x3E,
        0x81, // ld a,$81: tone channel 0 period low
        0xD3,
        0x7F, // out ($7f),a
        0x3E,
        0x00, // ld a,$00: period high
        0xD3,
        0x7F, // out ($7f),a
        0x3E,
        0x90, // ld a,$90: maximum channel volume
        0xD3,
        0x7F, // out ($7f),a
        0x18,
        0xFE, // jr -2
    };

    const auto run_at_rate = [&](u32 sample_rate) {
        HostRuntimeConfig config;
        config.audio_sample_rate = sample_rate;
        HostRuntime host(ConsoleModel::SMS, config);
        host.load_rom(rom);
        const HostFrameResult frame = host.run_frame();
        const std::size_t expected_frames =
            static_cast<std::size_t>((frame.end_cycle * sample_rate) / config.cpu_clock_hz);
        assert(frame.stereo_samples == expected_frames);
        assert(host.audio().size() == expected_frames * 2);
        bool heard_audio = false;
        for (std::size_t index = 0; index < host.audio().size(); index += 2) {
            assert(host.audio()[index] == host.audio()[index + 1]);
            heard_audio = heard_audio || host.audio()[index] != 0;
        }
        assert(heard_audio);
        return frame.stereo_samples;
    };

    const std::size_t frames_22050 = run_at_rate(22050);
    const std::size_t frames_44100 = run_at_rate(44100);
    assert(frames_44100 == frames_22050 * 2 || frames_44100 == frames_22050 * 2 + 1);
}

void test_host_runtime_execution_modes_and_fallback_metrics() {
    const std::vector<u8> rom = {
        0x00, // nop: classified as recompiled by the synthetic executor
        0x3E,
        0x42, // ld a,$42: classified as fallback
        0x18,
        0xFB, // jr $0000: classified as fallback
    };

    HostRuntime interpreter(ConsoleModel::SMS);
    interpreter.load_rom(rom);
    const HostFrameResult interpreted = interpreter.run_frame();
    assert(interpreted.interpreted_instructions == interpreted.instructions);
    assert(interpreted.recompiled_instructions == 0);
    assert(interpreted.fallback_instructions == 0);

    HostRuntimeConfig hybrid_config;
    hybrid_config.execution_mode = HostExecutionMode::Hybrid;
    hybrid_config.instruction_executor = synthetic_hybrid_executor;
    HostRuntime hybrid(ConsoleModel::SMS, hybrid_config);
    hybrid.load_rom(rom);
    const HostFrameResult mixed = hybrid.run_frame();
    assert(mixed.recompiled_instructions > 0);
    assert(mixed.fallback_instructions > 0);
    assert(mixed.interpreted_instructions == 0);
    assert(mixed.recompiled_instructions + mixed.fallback_instructions == mixed.instructions);

    HostRuntimeConfig strict_config = hybrid_config;
    strict_config.execution_mode = HostExecutionMode::Recompiled;
    HostRuntime strict(ConsoleModel::SMS, strict_config);
    strict.load_rom(rom);
    bool rejected_fallback = false;
    try {
        (void)strict.run_frame();
    } catch (const std::runtime_error&) {
        rejected_fallback = true;
    }
    assert(rejected_fallback);

    bool rejected_missing_executor = false;
    try {
        HostRuntimeConfig invalid;
        invalid.execution_mode = HostExecutionMode::Hybrid;
        (void)HostRuntime(ConsoleModel::SMS, invalid);
    } catch (const std::invalid_argument&) {
        rejected_missing_executor = true;
    }
    assert(rejected_missing_executor);
}

void test_host_runtime_soft_reset_preserves_loaded_session() {
    std::vector<u8> rom(0x8000, 0x00);
    rom[0] = 0x76;
    std::vector<u8> bios(0x4000, 0x00);
    bios[0] = 0x3E;

    HostRuntime host(ConsoleModel::SMS);
    host.load_bios(bios);
    host.load_rom(rom);
    host.console().bus().set_bios_enabled(false);
    host.console().bus().write(0xC123, 0x5A);
    host.console().cpu().pc = 0x4321;
    host.console().cpu().cycles = 12345;

    host.reset();
    assert(host.console().cpu().pc == 0);
    assert(host.console().cpu().cycles == 0);
    assert(host.console().bus().read(0x0000) == 0x76);
    assert(host.console().bus().read(0xC123) == 0x5A);
    assert(!host.console().bus().bios_enabled());
}

void test_host_input_script_tracks_frame_state() {
    const HostInputScript script = parse_host_input_script("# deterministic title-screen input\n"
                                                           "frame,player1,player2,pause\n"
                                                           "0,none,none,off\n"
                                                           "10,right+button1,left,on\n"
                                                           "11,right,b2,off\n");

    assert(script.events().size() == 3);
    assert(script.state_for_frame(0).player1 == 0);
    assert(script.state_for_frame(9).player1 == 0);
    assert(script.state_for_frame(10).player1 == static_cast<u8>(Joypad::Right | Joypad::Button1));
    assert(script.state_for_frame(10).player2 == Joypad::Left);
    assert(script.state_for_frame(10).pause);
    assert(script.state_for_frame(11).player1 == Joypad::Right);
    assert(script.state_for_frame(999).player2 == Joypad::Button2);
    assert(!script.state_for_frame(999).pause);
}

void test_console_save_state_round_trip_restores_runtime_state() {
    const std::vector<u8> rom = {
        0x3E,
        0x42, // ld a,$42
        0x32,
        0x00,
        0xC0, // ld ($c000),a
        0x76, // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    console.vdp().set_timing({200, 313});
    console.bus().output(0x3E, 0x08);
    run_until_halt(console);
    assert(console.bus().read(0xC000) == 0x42);

    SaveStateMetadata metadata;
    metadata.present = true;
    metadata.model = ConsoleModel::SMS;
    metadata.rom_hash = "fnv1a64:test";
    metadata.bios_hash = "fnv1a64:bios";
    metadata.profile_fingerprint = "fnv1a64:profile";
    const auto bytes = save_console_state(console, metadata);
    assert(bytes[4] == 11 && bytes[5] == 0);
    console.bus().write(0xC000, 0x99);
    console.cpu().a = 0x11;
    assert(console.bus().read(0xC000) == 0x99);

    load_console_state(console, bytes);
    assert(console.cpu().halted);
    assert(console.cpu().a == 0x42);
    assert(console.bus().read(0xC000) == 0x42);

    const auto restored = deserialize_console_state(bytes);
    assert(restored.cpu.a == 0x42);
    assert(restored.bus.memory[0xC000] == 0x42);
    assert(restored.bus.memory_control == 0x08);
    assert(restored.vdp.timing.cpu_cycles_per_scanline == 200);
    assert(restored.vdp.timing.scanlines_per_frame == 313);
    assert(restored.vdp.video_mode == VdpVideoMode::SmsMode4);
    const auto image = deserialize_console_state_image(bytes);
    assert(image.metadata.present);
    assert(image.metadata.environment_identity_present);
    assert(image.metadata.model == ConsoleModel::SMS);
    assert(image.metadata.rom_hash == "fnv1a64:test");
    assert(image.metadata.bios_hash == "fnv1a64:bios");
    assert(image.metadata.profile_fingerprint == "fnv1a64:profile");
    validate_save_state_metadata(image.metadata, metadata);

    const auto rejects_metadata = [&](SaveStateMetadata expected) {
        try {
            validate_save_state_metadata(image.metadata, expected);
        } catch (const std::runtime_error&) {
            return true;
        }
        return false;
    };
    SaveStateMetadata wrong_bios = metadata;
    wrong_bios.bios_hash = "fnv1a64:other-bios";
    assert(rejects_metadata(wrong_bios));
    SaveStateMetadata wrong_profile = metadata;
    wrong_profile.profile_fingerprint = "fnv1a64:other-profile";
    assert(rejects_metadata(wrong_profile));

    constexpr std::size_t ym2612_fixed_state_size = 537;
    const std::size_t ym2612_state_size = ym2612_fixed_state_size + console.save_state().ym2612.core_state.size();
    std::vector<u8> legacy_v10 = bytes;
    legacy_v10.erase(legacy_v10.end() - static_cast<std::ptrdiff_t>(ym2612_state_size + 2), legacy_v10.end() - 2);
    legacy_v10[4] = 10;
    legacy_v10[5] = 0;
    const auto legacy_v10_image = deserialize_console_state_image(legacy_v10);
    assert(!legacy_v10_image.state.ym2612.enabled);

    std::vector<u8> legacy_v9 = legacy_v10;
    constexpr std::size_t cpu_state_size = 41;
    constexpr std::size_t bus_state_size = 98325;
    constexpr std::size_t vdp_arrays_before_framebuffer = 16432;
    const std::size_t v10_metadata_size =
        1 + 2 + metadata.rom_hash.size() + 2 + metadata.bios_hash.size() + 2 + metadata.profile_fingerprint.size();
    const std::size_t framebuffer_start =
        6 + v10_metadata_size + cpu_state_size + bus_state_size + vdp_arrays_before_framebuffer;
    const std::size_t legacy_framebuffer_end = framebuffer_start + Vdp::width * Vdp::height * sizeof(u32);
    const std::size_t extended_framebuffer_size = Vdp::width * (Vdp::max_height - Vdp::height) * sizeof(u32);
    legacy_v9.erase(legacy_v9.begin() + static_cast<std::ptrdiff_t>(legacy_framebuffer_end),
                    legacy_v9.begin() +
                        static_cast<std::ptrdiff_t>(legacy_framebuffer_end + extended_framebuffer_size));
    legacy_v9[4] = 9;
    legacy_v9[5] = 0;
    const auto legacy_v9_image = deserialize_console_state_image(legacy_v9);
    assert(legacy_v9_image.metadata.environment_identity_present);
    assert(legacy_v9_image.state.cpu.a == 0x42);

    std::vector<u8> legacy_v8 = legacy_v9;
    const std::size_t extended_metadata_offset = 9 + metadata.rom_hash.size();
    const std::size_t extended_metadata_size = 2 + metadata.bios_hash.size() + 2 + metadata.profile_fingerprint.size();
    legacy_v8.erase(legacy_v8.begin() + static_cast<std::ptrdiff_t>(extended_metadata_offset),
                    legacy_v8.begin() + static_cast<std::ptrdiff_t>(extended_metadata_offset + extended_metadata_size));
    legacy_v8[4] = 8;
    legacy_v8[5] = 0;
    const auto legacy_image = deserialize_console_state_image(legacy_v8);
    assert(legacy_image.metadata.present);
    assert(!legacy_image.metadata.environment_identity_present);
    assert(legacy_image.metadata.rom_hash == metadata.rom_hash);
    assert(legacy_image.state.cpu.a == 0x42);
    validate_save_state_metadata(legacy_image.metadata, wrong_bios);
}

void test_game_profile_hash_and_parse() {
    const std::vector<u8> rom = {0x00, 0x01, 0x02};
    const std::string hash = rom_hash_fnv1a64(rom);
    assert(hash == "fnv1a64:d949aa186c0c4928");

    const auto profiles = GameProfileDatabase::parse("[profile]\n"
                                                     "name = \"local test\"\n"
                                                     "hash = \"fnv1a64:d949aa186c0c4928\"\n"
                                                     "model = \"sg3000\"\n"
                                                     "mapper = \"k8k\"\n"
                                                     "mode = \"enhanced\"\n"
                                                     "disable_sprite_limit = true\n"
                                                     "enable_fm = true\n"
                                                     "enable_ym2612 = true\n"
                                                     "audio_latency_ms = 120\n"
                                                     "audio_sample_rate = 48000\n"
                                                     "video_standard = \"pal\"\n");
    const GameProfile* profile = profiles.find_by_hash(hash);
    assert(profile != nullptr);
    assert(profile->name == "local test");
    assert(profile->has_model);
    assert(profile->model == ConsoleModel::SG3000);
    assert(profile->has_mapper);
    assert(profile->mapper == CartridgeMapper::K8KMapper);
    assert(profile->has_enhancements);
    assert(profile->enhancements.mode == RuntimeMode::Enhanced);
    assert(profile->enhancements.disable_sprite_limit);
    assert(profile->enhancements.enable_fm);
    assert(profile->enhancements.enable_ym2612);
    assert(profile->has_audio_latency_ms);
    assert(profile->audio_latency_ms == 120);
    assert(profile->has_audio_sample_rate);
    assert(profile->audio_sample_rate == 48000);
    assert(profile->has_video_standard);
    assert(profile->video_standard == HostVideoStandard::Pal);
    const std::string profile_fingerprint = game_profile_fingerprint(*profile);
    assert(profile_fingerprint.rfind("fnv1a64:", 0) == 0);
    GameProfile renamed_profile = *profile;
    renamed_profile.name = "renamed local test";
    assert(game_profile_fingerprint(renamed_profile) == profile_fingerprint);
    GameProfile changed_profile = *profile;
    changed_profile.audio_sample_rate = 44100;
    assert(game_profile_fingerprint(changed_profile) != profile_fingerprint);
    const auto host_config = host_runtime_config_for_video_standard(profile->video_standard);
    assert(host_config.scanlines_per_frame == 313);

    assert(cartridge_mapper_from_name("NONE") == CartridgeMapper::Plain);
    assert(cartridge_mapper_from_name("s") == CartridgeMapper::SMapper);
    bool rejected_mapper = false;
    try {
        (void)GameProfileDatabase::parse("[profile]\n"
                                         "hash = \"fnv1a64:0000000000000000\"\n"
                                         "mapper = \"mystery\"\n");
    } catch (const std::runtime_error&) {
        rejected_mapper = true;
    }
    assert(rejected_mapper);
}

int main() {
    test_recent_games_are_local_deduplicated_and_pruned();
    test_cartridge_header_analysis();
    test_cartridge_header_generation_and_checksum_rebuild();
    test_cartridge_header_rejects_truncated_declared_size();
    test_basic_program();
    test_djnz_loop();
    test_stack_and_conditional_call();
    test_immediate_alu();
    test_exchange_and_rst();
    test_alternate_registers_and_interrupt_flipflops();
    test_cb_register_operations();
    test_cb_memory_operations();
    test_ed_interrupt_and_special_registers();
    test_ed_16bit_memory_load_store();
    test_ed_block_transfer_and_search();
    test_ed_decrementing_block_transfer_and_search();
    test_all_z80_opcode_pages_have_runtime_behavior();
    test_z80_undocumented_xy_flags();
    test_z80_refresh_register_and_prefix_cycles();
    test_z80_conditional_and_block_cycle_counts();
    test_ed_nibble_rotates();
    test_ed_block_io();
    test_ed_block_io_exact_flags();
    test_ed_port_register_io();
    test_vblank_interrupt_im1();
    test_vdp_line_interrupt_im1();
    test_vdp_data_port_clears_pending_control_latch();
    test_vdp_buffered_vram_reads_and_state_round_trip();
    test_vdp_line_irq_is_not_status_overflow_bit();
    test_vdp_sprite_overflow_status_does_not_raise_line_irq();
    test_pause_triggers_nmi();
    test_two_player_joypad_ports();
    test_vdp_mode4_background_pixel();
    test_vdp_mode4_renders_ninth_tile_bit_palette_and_flips();
    test_vdp_display_disabled_uses_backdrop_color();
    test_vdp_backdrop_uses_register7_color();
    test_sg3000_vdp_tms_graphics1_background_pixel();
    test_sg3000_vdp_tms_text_mode();
    test_sg3000_vdp_tms_graphics2_mode();
    test_sg3000_vdp_tms_multicolor_mode();
    test_sg3000_vdp_tms_sprite_pixel();
    test_sg3000_vdp_tms_large_sprite_quadrants_and_zoom();
    test_vdp_name_table_register_masks_low_bit();
    test_vdp_scroll_lock_and_left_blank();
    test_vdp_horizontal_scroll_moves_background_right();
    test_vdp_left_column_blank_masks_sprites();
    test_vdp_right_column_vertical_scroll_lock();
    test_vdp_vertical_scroll_wraps_at_224_pixels();
    test_vdp_right_column_vertical_scroll_lock_uses_screen_column();
    test_vdp_basic_sprite_pixel();
    test_vdp_sprite_shift_and_zoom();
    test_vdp_sprite_pattern_base_register();
    test_vdp_tall_sprite_uses_second_tile();
    test_vdp_sprite_y_wraps_above_top();
    test_vdp_sprite_collision_and_overflow_flags();
    test_vdp_overlapping_sprites_keep_lower_index_priority();
    test_tms_overlapping_sprites_keep_lower_index_priority();
    test_tms_sprite_overflow_reports_fifth_sprite_index();
    test_tms_enhanced_sprites_do_not_create_hardware_collision();
    test_vdp_background_priority_hides_sprite_pixel();
    test_vdp_sprite_overlays_non_priority_background_pixel();
    test_vdp_sprite_limit_enhancement();
    test_vdp_enhanced_sprites_do_not_create_hardware_collision();
    test_vdp_reduce_flicker_uses_conservative_sprite_limit();
    test_vdp_debug_tilemap_and_sprite_snapshots();
    test_bus_io_logging_records_reads_and_writes();
    test_bus_mirrors_vdp_psg_and_counter_ports();
    test_bus_memory_logging_records_ram_mapper_and_cartridge_ram();
    test_vdp_access_logging_records_register_vram_and_cram_writes();
    test_vdp_debug_snapshot_reports_timing_and_status();
    test_vdp_custom_timing_controls_scanline_wrap();
    test_vdp_extended_mode_heights_and_vblank();
    test_host_runtime_config_sets_vdp_timing();
    test_console_enhancement_config_propagates_to_runtime_devices();
    test_psg_tone_generates_sample();
    test_ym2413_audio_control_and_register_writes();
    test_ym2413_absent_audio_control_probe();
    test_ym2612_experimental_ports_audio_and_state();
    test_host_runtime_mixes_ym2612_when_enabled();
    test_misc_jumps_and_flags();
    test_v_counter_port();
    test_v_counter_uses_video_standard_timing();
    test_v_counter_tracks_extended_mode_discontinuities();
    test_index_register_basics();
    test_accumulator_rotates();
    test_index_cb_operations();
    test_index_displacement_loads_and_alu();
    test_index_high_low_register_operations();
    test_ed_adc_sbc_hl();
    test_daa_after_add_and_subtract();
    test_mapper_keeps_ram();
    test_ram_mirroring();
    test_sg3000_uses_one_kibibyte_ram_mirrored_through_upper_memory();
    test_work_ram_starts_cleared();
    test_bios_overlay_boots_before_rom();
    test_bios_can_disable_itself_with_memory_control_port();
    test_memory_control_port_maps_bios_cart_and_ram();
    test_memory_control_models_expansion_card_io_and_reserved_bits();
    test_smapper_cartridge_ram_banks();
    test_smapper_loads_cartridge_ram();
    test_smapper_fixed_window_and_register_mirroring();
    test_smapper_sram_respects_cartridge_visibility_and_active_mapper();
    test_auto_mapper_locks_after_first_hardware_family_evidence();
    test_mapper_snapshot_reports_active_mapper_and_banks();
    test_cmapper_banks_and_auto_detection();
    test_cmapper_8k_cartridge_ram_window();
    test_forced_kmapper_bank_switches_slot2();
    test_forced_k8k_mapper_uses_8k_banks();
    test_plain_mapper_keeps_first_48k_linear();
    test_auto_mapper_uses_plain_for_small_linear_roms();
    test_copier_header_is_removed();
    test_ei_delay_and_nmi_service();
    test_interrupt_acknowledge_state_and_cycles();
    test_bc_de_indirect_loads();
    test_hl_absolute_load_store();
    test_host_runtime_frame_audio_and_input();
    test_synthetic_rom_integrates_mapper_vdp_psg_and_input();
    test_host_runtime_stereo_mixer_respects_sample_rate();
    test_host_runtime_execution_modes_and_fallback_metrics();
    test_host_runtime_soft_reset_preserves_loaded_session();
    test_host_input_script_tracks_frame_state();
    test_console_save_state_round_trip_restores_runtime_state();
    test_game_profile_hash_and_parse();
    return 0;
}
