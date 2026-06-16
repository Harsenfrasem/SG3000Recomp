#include "sgrecomp/vdp.h"

namespace sgrecomp {
namespace {

constexpr u32 kOpaque = 0xFF000000;

} // namespace

u8 Vdp::read_data() {
    const u8 value = vram_[address_ & 0x3FFF];
    address_ = static_cast<u16>((address_ + 1) & 0x3FFF);
    return value;
}

u8 Vdp::read_status() {
    const u8 value = status_;
    status_ = 0;
    pending_control_ = false;
    return value;
}

u8 Vdp::read_v_counter() const {
    if (scanline_ <= 0xDA) {
        return static_cast<u8>(scanline_);
    }
    return static_cast<u8>(scanline_ - 6);
}

u8 Vdp::read_h_counter() const {
    return static_cast<u8>((scanline_cycles_ * 342) / 228);
}

void Vdp::write_data(u8 value) {
    if (code_ == 3) {
        cram_[address_ & 0x1F] = value;
    } else {
        vram_[address_ & 0x3FFF] = value;
    }
    address_ = static_cast<u16>((address_ + 1) & 0x3FFF);
}

void Vdp::write_control(u8 value) {
    if (!pending_control_) {
        latch_ = value;
        pending_control_ = true;
        return;
    }

    const u8 code = static_cast<u8>((value >> 6) & 0x03);
    code_ = code;
    address_ = static_cast<u16>(((value & 0x3F) << 8) | latch_);
    if (code == 2) {
        registers_[value & 0x0F] = latch_;
        if ((value & 0x0F) == 10) {
            line_counter_ = latch_;
        }
    }
    pending_control_ = false;
}

void Vdp::tick(int cpu_cycles) {
    scanline_cycles_ += cpu_cycles;
    while (scanline_cycles_ >= 228) {
        scanline_cycles_ -= 228;
        advance_scanline();
    }
}

bool Vdp::irq_pending() const {
    const bool frame_irq = (status_ & 0x80) != 0 && (registers_[1] & 0x20) != 0;
    const bool line_irq = (status_ & 0x40) != 0 && (registers_[0] & 0x10) != 0;
    return frame_irq || line_irq;
}

void Vdp::advance_scanline() {
    if (scanline_ < height) {
        render_scanline(scanline_);
        if (first_line_) {
            line_counter_ = registers_[10];
            first_line_ = false;
        } else if (line_counter_ == 0) {
            line_counter_ = registers_[10];
            status_ |= 0x40;
        } else {
            --line_counter_;
        }
    }

    ++scanline_;

    if (scanline_ == height) {
        status_ |= 0x80;
        line_counter_ = registers_[10];
    }

    if (scanline_ >= 262) {
        scanline_ = 0;
        first_line_ = true;
    }
}

void Vdp::render_scanline(int line) {
    if ((registers_[1] & 0x40) == 0) {
        return;
    }

    const u16 name_base = static_cast<u16>((registers_[2] & 0x0E) << 10);
    const u16 pattern_base = static_cast<u16>((registers_[4] & 0x04) << 11);
    const int y = (line + registers_[9]) & 0xFF;
    const int tile_y = (y / 8) & 0x1F;
    const int row = y & 0x07;

    for (int x = 0; x < width; ++x) {
        const int scrolled_x = (x + registers_[8]) & 0xFF;
        const int tile_x = scrolled_x / 8;
        const int bit = 7 - (scrolled_x & 0x07);
        const u16 entry_address = static_cast<u16>((name_base + ((tile_y * 32 + tile_x) * 2)) & 0x3FFF);
        const u16 entry = make_u16(vram_[entry_address], vram_[(entry_address + 1) & 0x3FFF]);
        const u16 tile = static_cast<u16>(entry & 0x01FF);
        const bool palette1 = (entry & 0x0800) != 0;
        const bool flip_x = (entry & 0x0200) != 0;
        const bool flip_y = (entry & 0x0400) != 0;
        const int tile_row = flip_y ? (7 - row) : row;
        const int tile_bit = flip_x ? (7 - bit) : bit;
        const u16 pattern = static_cast<u16>((pattern_base + tile * 32 + tile_row * 4) & 0x3FFF);
        const u8 color = static_cast<u8>(
            (((vram_[pattern] >> tile_bit) & 0x01) << 0) |
            (((vram_[(pattern + 1) & 0x3FFF] >> tile_bit) & 0x01) << 1) |
            (((vram_[(pattern + 2) & 0x3FFF] >> tile_bit) & 0x01) << 2) |
            (((vram_[(pattern + 3) & 0x3FFF] >> tile_bit) & 0x01) << 3));
        const u8 palette_index = static_cast<u8>((palette1 ? 16 : 0) + color);
        framebuffer_[line * width + x] = cram_color(palette_index);
    }

    render_sprites(line);
}

void Vdp::render_sprites(int line) {
    const u16 sprite_base = static_cast<u16>((registers_[5] & 0x7E) << 7);
    const bool tall_sprites = (registers_[1] & 0x02) != 0;
    const int sprite_height = tall_sprites ? 16 : 8;
    std::array<bool, width> sprite_pixels{};
    int visible_sprites = 0;

    for (int sprite = 0; sprite < 64; ++sprite) {
        const u8 raw_y = vram_[(sprite_base + sprite) & 0x3FFF];
        if (raw_y == 0xD0) {
            break;
        }

        const int sprite_y = static_cast<u8>(raw_y + 1);
        if (line < sprite_y || line >= sprite_y + sprite_height) {
            continue;
        }
        ++visible_sprites;
        if (visible_sprites > 8) {
            status_ |= 0x40;
        }

        const u16 attribute = static_cast<u16>((sprite_base + 0x80 + sprite * 2) & 0x3FFF);
        const int sprite_x = vram_[attribute];
        u8 tile = vram_[(attribute + 1) & 0x3FFF];
        if (tall_sprites) {
            tile = static_cast<u8>(tile & 0xFE);
        }

        const int row = line - sprite_y;
        const u16 pattern = static_cast<u16>((tile * 32 + row * 4) & 0x3FFF);
        for (int px = 0; px < 8; ++px) {
            const int x = sprite_x + px;
            if (x < 0 || x >= width) {
                continue;
            }
            const int bit = 7 - px;
            const u8 color = static_cast<u8>(
                (((vram_[pattern] >> bit) & 0x01) << 0) |
                (((vram_[(pattern + 1) & 0x3FFF] >> bit) & 0x01) << 1) |
                (((vram_[(pattern + 2) & 0x3FFF] >> bit) & 0x01) << 2) |
                (((vram_[(pattern + 3) & 0x3FFF] >> bit) & 0x01) << 3));
            if (color == 0) {
                continue;
            }
            if (sprite_pixels[static_cast<std::size_t>(x)]) {
                status_ |= 0x20;
            }
            sprite_pixels[static_cast<std::size_t>(x)] = true;
            framebuffer_[line * width + x] = cram_color(static_cast<u8>(16 + color));
        }
    }
}

u32 Vdp::cram_color(u8 index) const {
    const u8 raw = cram_[index & 0x1F];
    const u8 r = static_cast<u8>((raw & 0x03) * 85);
    const u8 g = static_cast<u8>(((raw >> 2) & 0x03) * 85);
    const u8 b = static_cast<u8>(((raw >> 4) & 0x03) * 85);
    return kOpaque | (static_cast<u32>(r) << 16) | (static_cast<u32>(g) << 8) | b;
}

} // namespace sgrecomp
