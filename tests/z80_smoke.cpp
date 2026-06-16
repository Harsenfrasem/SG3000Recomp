#include "sgrecomp/console.h"

#include <cassert>
#include <vector>

using namespace sgrecomp;

void run_until_halt(Console& console, u64 max_cycles = 4096) {
    console.run_cycles(max_cycles);
    assert(console.cpu().halted);
}

void test_basic_program() {
    const std::vector<u8> rom = {
        0x3E, 0x12,       // ld a,$12
        0x06, 0x08,       // ld b,$08
        0x04,             // inc b
        0x05,             // dec b
        0x80,             // add a,b
        0xFE, 0x1A,       // cp $1a
        0x20, 0x02,       // jr nz,+2
        0x32, 0x00, 0xC0, // ld ($C000),a
        0x76,             // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x1A);
    assert(console.bus().read(0xC000) == 0x1A);
}

void test_djnz_loop() {
    const std::vector<u8> rom = {
        0x06, 0x03, // ld b,$03
        0x3E, 0x00, // ld a,$00
        0x3C,       // inc a
        0x10, 0xFD, // djnz -3
        0x76,       // halt
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
        0x3E, 0x10, // ld a,$10
        0xC6, 0x0F, // add a,$0f -> 1f
        0xE6, 0x1B, // and $1b -> 1b
        0xEE, 0x03, // xor $03 -> 18
        0xF6, 0x80, // or $80 -> 98
        0xD6, 0x08, // sub $08 -> 90
        0xFE, 0x90, // cp $90
        0x76,       // halt
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
        0x3E, 0x11, // ld a,$11
        0x08,       // ex af,af'
        0x3E, 0x22, // ld a,$22
        0x08,       // ex af,af'
        0x06, 0x33, // ld b,$33
        0xD9,       // exx
        0x06, 0x44, // ld b,$44
        0xD9,       // exx
        0xFB,       // ei
        0xF3,       // di
        0x76,       // halt
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
        0x06, 0x81, // ld b,$81
        0xCB, 0x00, // rlc b -> $03, carry set
        0xCB, 0x40, // bit 0,b -> z clear
        0xCB, 0x80, // res 0,b -> $02
        0xCB, 0xC0, // set 0,b -> $03
        0x78,       // ld a,b
        0x76,       // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x03);
    assert((console.cpu().f & 0x40) == 0);
}

void test_cb_memory_operations() {
    const std::vector<u8> rom = {
        0x21, 0x00, 0xC0, // ld hl,$c000
        0x36, 0x80,       // ld (hl),$80
        0xCB, 0x26,       // sla (hl) -> $00, carry set
        0xCB, 0xFE,       // set 7,(hl) -> $80
        0xCB, 0xBE,       // res 7,(hl) -> $00
        0x7E,             // ld a,(hl)
        0x76,             // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0x00);
    assert(console.bus().read(0xC000) == 0x00);
}

void test_ed_interrupt_and_special_registers() {
    const std::vector<u8> rom = {
        0x3E, 0x42, // ld a,$42
        0xED, 0x47, // ld i,a
        0x3E, 0x00, // ld a,$00
        0xFB,       // ei
        0xED, 0x57, // ld a,i
        0xED, 0x5E, // im 2
        0xED, 0x44, // neg
        0x76,       // halt
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
        0x01, 0x34, 0x12, // ld bc,$1234
        0xED, 0x43, 0x00, 0xC0, // ld ($c000),bc
        0x11, 0x00, 0x00, // ld de,$0000
        0xED, 0x5B, 0x00, 0xC0, // ld de,($c000)
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
        0x21, 0x00, 0xC0, // ld hl,$c000
        0x36, 0x12,       // ld (hl),$12
        0x3E, 0xA5,       // ld a,$a5
        0xED, 0x6F,       // rld -> a=$a1, (hl)=$25
        0xED, 0x67,       // rrd -> a=$a5, (hl)=$12
        0x76,             // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().a == 0xA5);
    assert(console.bus().read(0xC000) == 0x12);
}

void test_ed_block_io() {
    const std::vector<u8> rom = {
        0x21, 0x00, 0xC0, // ld hl,$c000
        0x01, 0x7F, 0x02, // ld bc,$027f
        0xED, 0xB3,       // otir
        0x21, 0x10, 0xC0, // ld hl,$c010
        0x01, 0xDD, 0x02, // ld bc,$02dd
        0xED, 0xB2,       // inir
        0x76,             // halt
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
        0x0E, 0x7F, // ld c,$7f
        0x16, 0x55, // ld d,$55
        0xED, 0x51, // out (c),d
        0x0E, 0xDD, // ld c,$dd
        0xED, 0x78, // in a,(c)
        0x76,       // halt
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
        0xDB, 0x7E, // in a,($7e)
        0xFE, 0xB0, // cp $b0
        0x20, 0xFA, // jr nz,-6
        0x76,       // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console, 100000);

    assert(console.cpu().a == 0xB0);
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
        0x3E, 0x81, // ld a,$81
        0x07,       // rlca -> $03 carry
        0x17,       // rla -> $07 carry clear
        0x37,       // scf
        0x1F,       // rra -> $83 carry
        0x0F,       // rrca -> $C1 carry
        0x76,       // halt
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
        0x21, 0x00, 0x10, // ld hl,$1000
        0x11, 0x01, 0x00, // ld de,$0001
        0x37,             // scf
        0xED, 0x5A,       // adc hl,de -> $1002
        0xED, 0x52,       // sbc hl,de -> $1001
        0x76,             // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().hl() == 0x1001);
}

void test_daa_after_add_and_subtract() {
    const std::vector<u8> rom = {
        0x3E, 0x15, // ld a,$15
        0xC6, 0x27, // add a,$27
        0x27,       // daa -> $42
        0xD6, 0x27, // sub $27
        0x27,       // daa -> $15
        0x76,       // halt
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
        0x3E, 0x5A,       // ld a,$5a
        0x32, 0x00, 0xC0, // ld ($c000),a
        0x3A, 0x00, 0xE0, // ld a,($e000)
        0x32, 0x00, 0xF0, // ld ($f000),a
        0x76,             // halt
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

void test_bios_overlay_boots_before_rom() {
    const std::vector<u8> bios = {
        0x3E, 0x99, // ld a,$99
        0x76,       // halt
    };
    const std::vector<u8> rom = {
        0x3E, 0x42, // ld a,$42
        0x76,       // halt
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
        0x01, 0x00, 0xC0, // ld bc,$c000
        0x11, 0x01, 0xC0, // ld de,$c001
        0x3E, 0x12,       // ld a,$12
        0x02,             // ld (bc),a
        0x3E, 0x34,       // ld a,$34
        0x12,             // ld (de),a
        0x0A,             // ld a,(bc)
        0x1A,             // ld a,(de)
        0x76,             // halt
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
        0x21, 0x34, 0x12, // ld hl,$1234
        0x22, 0x00, 0xC0, // ld ($c000),hl
        0x21, 0x00, 0x00, // ld hl,$0000
        0x2A, 0x00, 0xC0, // ld hl,($c000)
        0x76,             // halt
    };

    Console console(ConsoleModel::SMS);
    console.load_rom(rom);
    run_until_halt(console);

    assert(console.cpu().hl() == 0x1234);
    assert(console.bus().read(0xC000) == 0x34);
    assert(console.bus().read(0xC001) == 0x12);
}

int main() {
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
    test_ed_nibble_rotates();
    test_ed_block_io();
    test_ed_port_register_io();
    test_vblank_interrupt_im1();
    test_vdp_line_interrupt_im1();
    test_pause_triggers_nmi();
    test_two_player_joypad_ports();
    test_vdp_mode4_background_pixel();
    test_vdp_scroll_lock_and_left_blank();
    test_vdp_basic_sprite_pixel();
    test_vdp_sprite_collision_and_overflow_flags();
    test_psg_tone_generates_sample();
    test_misc_jumps_and_flags();
    test_v_counter_port();
    test_index_register_basics();
    test_accumulator_rotates();
    test_index_cb_operations();
    test_index_displacement_loads_and_alu();
    test_index_high_low_register_operations();
    test_ed_adc_sbc_hl();
    test_daa_after_add_and_subtract();
    test_mapper_keeps_ram();
    test_ram_mirroring();
    test_bios_overlay_boots_before_rom();
    test_bios_can_disable_itself_with_memory_control_port();
    test_smapper_cartridge_ram_banks();
    test_smapper_loads_cartridge_ram();
    test_copier_header_is_removed();
    test_ei_delay_and_nmi_service();
    test_bc_de_indirect_loads();
    test_hl_absolute_load_store();
    return 0;
}
