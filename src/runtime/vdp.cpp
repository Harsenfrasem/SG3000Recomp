#include "sgrecomp/vdp.h"

namespace sgrecomp {
namespace {

constexpr u32 kOpaque = 0xFF000000;
constexpr u32 kTmsPalette[16] = {
    0xFF000000, // transparent maps to black for the framebuffer
    0xFF000000,
    0xFF21C842,
    0xFF5EDC78,
    0xFF5455ED,
    0xFF7D76FC,
    0xFFD4524D,
    0xFF42EBF5,
    0xFFFC5554,
    0xFFFF7978,
    0xFFD4C154,
    0xFFE6CE80,
    0xFF21B03B,
    0xFFC95BBA,
    0xFFCCCCCC,
    0xFFFFFFFF,
};

} // namespace

u8 Vdp::read_data() {
    const u8 value = read_buffer_;
    read_buffer_ = vram_[address_ & 0x3FFF];
    address_ = static_cast<u16>((address_ + 1) & 0x3FFF);
    pending_control_ = false;
    return value;
}

u8 Vdp::read_status() {
    const u8 value = status_;
    status_ = 0;
    line_irq_pending_ = false;
    pending_control_ = false;
    return value;
}

u8 Vdp::read_v_counter() const {
    if (timing_.scanlines_per_frame >= 300) {
        if (scanline_ <= 0xF2) {
            return static_cast<u8>(scanline_);
        }
        return static_cast<u8>(scanline_ - 0x39);
    }
    if (scanline_ <= 0xDA) {
        return static_cast<u8>(scanline_);
    }
    return static_cast<u8>(scanline_ - 6);
}

u8 Vdp::read_h_counter() const {
    return static_cast<u8>((scanline_cycles_ * 342) / timing_.cpu_cycles_per_scanline);
}

void Vdp::write_data(u8 value) {
    pending_control_ = false;
    if (code_ == 3) {
        cram_[address_ & 0x1F] = value;
        log_access(VdpAccessKind::Cram, static_cast<u16>(address_ & 0x1F), value);
    } else {
        vram_[address_ & 0x3FFF] = value;
        log_access(VdpAccessKind::Vram, static_cast<u16>(address_ & 0x3FFF), value);
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
    if (code == 0) {
        read_buffer_ = vram_[address_ & 0x3FFF];
        address_ = static_cast<u16>((address_ + 1) & 0x3FFF);
    }
    if (code == 2) {
        const u8 reg = static_cast<u8>(value & 0x0F);
        registers_[reg] = latch_;
        log_access(VdpAccessKind::Register, reg, latch_);
        if (reg == 10) {
            line_counter_ = latch_;
        }
        if (is_tms_mode() && (reg == 0 || reg == 1)) {
            update_tms_video_mode();
        }
    }
    pending_control_ = false;
}

void Vdp::tick(int cpu_cycles) {
    scanline_cycles_ += cpu_cycles;
    while (scanline_cycles_ >= timing_.cpu_cycles_per_scanline) {
        scanline_cycles_ -= timing_.cpu_cycles_per_scanline;
        advance_scanline();
    }
}

bool Vdp::irq_pending() const {
    const bool frame_irq = (status_ & 0x80) != 0 && (registers_[1] & 0x20) != 0;
    const bool line_irq = line_irq_pending_ && (registers_[0] & 0x10) != 0;
    return frame_irq || line_irq;
}

void Vdp::set_access_logging_enabled(bool enabled) {
    access_logging_enabled_ = enabled;
    if (!enabled) {
        logged_accesses_.clear();
    }
}

void Vdp::set_timing(const VdpTimingConfig& timing) {
    timing_.cpu_cycles_per_scanline = timing.cpu_cycles_per_scanline > 0 ? timing.cpu_cycles_per_scanline : 228;
    timing_.scanlines_per_frame = timing.scanlines_per_frame > height ? timing.scanlines_per_frame : 262;
    if (scanline_ >= timing_.scanlines_per_frame) {
        scanline_ = 0;
        first_line_ = true;
    }
    if (scanline_cycles_ >= timing_.cpu_cycles_per_scanline) {
        scanline_cycles_ %= timing_.cpu_cycles_per_scanline;
    }
}

void Vdp::advance_scanline() {
    if (scanline_ < height) {
        render_scanline(scanline_);
        if (first_line_) {
            line_counter_ = registers_[10];
            first_line_ = false;
        } else if (line_counter_ == 0) {
            line_counter_ = registers_[10];
            line_irq_pending_ = true;
        } else {
            --line_counter_;
        }
    }

    ++scanline_;

    if (scanline_ == height) {
        status_ |= 0x80;
        line_counter_ = registers_[10];
    }

    if (scanline_ >= timing_.scanlines_per_frame) {
        scanline_ = 0;
        first_line_ = true;
    }
}

void Vdp::render_scanline(int line) {
    scanline_bg_priority_.fill(false);
    if ((registers_[1] & 0x40) == 0) {
        for (int x = 0; x < width; ++x) {
            framebuffer_[line * width + x] = is_tms_mode()
                ? tms_color(registers_[7] & 0x0F)
                : backdrop_color();
        }
        return;
    }

    if (video_mode_ == VdpVideoMode::TmsGraphics1) {
        render_tms_graphics1_scanline(line);
        return;
    }
    if (video_mode_ == VdpVideoMode::TmsText) {
        render_tms_text_scanline(line);
        return;
    }
    if (video_mode_ == VdpVideoMode::TmsGraphics2) {
        render_tms_graphics2_scanline(line);
        return;
    }
    if (video_mode_ == VdpVideoMode::TmsMulticolor) {
        render_tms_multicolor_scanline(line);
        return;
    }

    render_mode4_scanline(line);
}

void Vdp::render_mode4_scanline(int line) {
    const u16 name_base = name_table_base();
    const u16 pattern_base = background_pattern_base();
    const bool lock_top_horizontal_scroll = (registers_[0] & 0x40) != 0 && line < 16;
    const bool lock_right_vertical_scroll = (registers_[0] & 0x80) != 0;
    const bool blank_left_column = left_column_blank_enabled();
    const int horizontal_scroll = lock_top_horizontal_scroll ? 0 : registers_[8];

    for (int x = 0; x < width; ++x) {
        if (blank_left_column && x < 8) {
            framebuffer_[line * width + x] = backdrop_color();
            continue;
        }

        const int scrolled_x = (x - horizontal_scroll) & 0xFF;
        const int tile_x = scrolled_x / 8;
        const int vertical_scroll = (lock_right_vertical_scroll && x >= 24 * 8) ? 0 : registers_[9];
        const int y = (line + vertical_scroll) % (28 * 8);
        const int tile_y = (y / 8) & 0x1F;
        const int row = y & 0x07;
        const int bit = 7 - (scrolled_x & 0x07);
        const u16 entry_address = static_cast<u16>((name_base + ((tile_y * 32 + tile_x) * 2)) & 0x3FFF);
        const u16 entry = make_u16(vram_[entry_address], vram_[(entry_address + 1) & 0x3FFF]);
        const u16 tile = static_cast<u16>(entry & 0x01FF);
        const bool palette1 = (entry & 0x0800) != 0;
        const bool flip_x = (entry & 0x0200) != 0;
        const bool flip_y = (entry & 0x0400) != 0;
        const bool priority = (entry & 0x1000) != 0;
        const int tile_row = flip_y ? (7 - row) : row;
        const int tile_bit = flip_x ? (7 - bit) : bit;
        const u16 pattern = static_cast<u16>((pattern_base + tile * 32 + tile_row * 4) & 0x3FFF);
        const u8 color = background_color_index(pattern, tile_bit);
        const u8 palette_index = static_cast<u8>((palette1 ? 16 : 0) + color);
        framebuffer_[line * width + x] = cram_color(palette_index);
        scanline_bg_priority_[static_cast<std::size_t>(x)] = priority && color != 0;
    }

    render_sprites(line);
}

void Vdp::render_tms_graphics1_scanline(int line) {
    const u16 name_base = static_cast<u16>((registers_[2] & 0x0F) << 10);
    const u16 color_base = static_cast<u16>(registers_[3] << 6);
    const u16 pattern_base = static_cast<u16>((registers_[4] & 0x07) << 11);
    const int tile_y = line / 8;
    const int row = line & 0x07;

    for (int x = 0; x < width; ++x) {
        const int tile_x = x / 8;
        const int bit = 7 - (x & 0x07);
        const u16 name_address = static_cast<u16>((name_base + tile_y * 32 + tile_x) & 0x3FFF);
        const u8 tile = vram_[name_address];
        const u16 pattern = static_cast<u16>((pattern_base + tile * 8 + row) & 0x3FFF);
        const u8 colors = vram_[(color_base + (tile / 8)) & 0x3FFF];
        const u8 foreground = static_cast<u8>(colors >> 4);
        const u8 background = static_cast<u8>(colors & 0x0F);
        const bool set = ((vram_[pattern] >> bit) & 0x01) != 0;
        framebuffer_[line * width + x] = tms_display_color(set ? foreground : background);
    }

    render_tms_sprites(line);
}

void Vdp::render_tms_text_scanline(int line) {
    const u16 name_base = static_cast<u16>((registers_[2] & 0x0F) << 10);
    const u16 pattern_base = static_cast<u16>((registers_[4] & 0x07) << 11);
    const u8 foreground = static_cast<u8>(registers_[7] >> 4);
    const u8 background = static_cast<u8>(registers_[7] & 0x0F);
    const int row = line & 0x07;
    const int character_y = line / 8;

    for (int x = 0; x < width; ++x) {
        if (x < 8 || x >= 248) {
            framebuffer_[line * width + x] = tms_color(background);
            continue;
        }
        const int text_x = x - 8;
        const int character_x = text_x / 6;
        const int bit = 7 - (text_x % 6);
        const u16 name_address = static_cast<u16>((name_base + character_y * 40 + character_x) & 0x3FFF);
        const u8 tile = vram_[name_address];
        const u16 pattern = static_cast<u16>((pattern_base + tile * 8 + row) & 0x3FFF);
        const bool set = ((vram_[pattern] >> bit) & 0x01) != 0;
        framebuffer_[line * width + x] = tms_display_color(set ? foreground : background);
    }
}

void Vdp::render_tms_graphics2_scanline(int line) {
    const u16 name_base = static_cast<u16>((registers_[2] & 0x0F) << 10);
    const u16 pattern_base = static_cast<u16>((registers_[4] & 0x04) << 11);
    const u16 color_base = static_cast<u16>((registers_[3] & 0x80) << 6);
    const int tile_y = line / 8;
    const int row = line & 0x07;
    const u16 pattern_name_mask = static_cast<u16>(((registers_[4] & 0x03) << 8) | 0x00FF);
    const u16 color_byte_mask = static_cast<u16>(((registers_[3] & 0x7F) << 6) | 0x003F);

    for (int x = 0; x < width; ++x) {
        const int tile_x = x / 8;
        const int bit = 7 - (x & 0x07);
        const u16 name_address = static_cast<u16>((name_base + tile_y * 32 + tile_x) & 0x3FFF);
        const u8 tile = vram_[name_address];
        const u16 extended_tile = static_cast<u16>((tile_y / 8) * 0x100 + tile);
        const u16 pattern_offset = static_cast<u16>(((extended_tile & pattern_name_mask) * 8 + row) & 0x1FFF);
        const u16 color_offset = static_cast<u16>((extended_tile * 8 + row) & color_byte_mask);
        const u8 pattern = vram_[(pattern_base + pattern_offset) & 0x3FFF];
        const u8 colors = vram_[(color_base + color_offset) & 0x3FFF];
        const u8 color = static_cast<u8>(((pattern >> bit) & 1) != 0 ? colors >> 4 : colors & 0x0F);
        framebuffer_[line * width + x] = tms_display_color(color);
    }

    render_tms_sprites(line);
}

void Vdp::render_tms_multicolor_scanline(int line) {
    const u16 name_base = static_cast<u16>((registers_[2] & 0x0F) << 10);
    const u16 pattern_base = static_cast<u16>((registers_[4] & 0x07) << 11);
    const int tile_y = line / 8;

    for (int x = 0; x < width; ++x) {
        const int tile_x = x / 8;
        const u16 name_address = static_cast<u16>((name_base + tile_y * 32 + tile_x) & 0x3FFF);
        const u8 tile = vram_[name_address];
        const u16 pattern = static_cast<u16>((pattern_base + tile * 8 + ((line / 4) & 0x07)) & 0x3FFF);
        const u8 colors = vram_[pattern];
        const u8 color = static_cast<u8>((x & 0x07) < 4 ? colors >> 4 : colors & 0x0F);
        framebuffer_[line * width + x] = tms_display_color(color);
    }

    render_tms_sprites(line);
}

void Vdp::render_sprites(int line) {
    const u16 sprite_base = static_cast<u16>((registers_[5] & 0x7E) << 7);
    const u16 sprite_pattern = sprite_pattern_base();
    const bool tall_sprites = (registers_[1] & 0x02) != 0;
    const bool zoomed_sprites = (registers_[1] & 0x01) != 0;
    const bool shift_sprites_left = (registers_[0] & 0x08) != 0;
    const int base_sprite_height = tall_sprites ? 16 : 8;
    const int sprite_height = zoomed_sprites ? base_sprite_height * 2 : base_sprite_height;
    const int sprite_render_limit = enhancements_.disable_sprite_limit
        ? 64
        : (enhancements_.reduce_flicker ? 16 : 8);
    std::array<bool, width> sprite_pixels{};
    int visible_sprites = 0;

    for (int sprite = 0; sprite < 64; ++sprite) {
        const u8 raw_y = vram_[(sprite_base + sprite) & 0x3FFF];
        if (raw_y == 0xD0) {
            break;
        }

        int sprite_y = static_cast<int>(raw_y) + 1;
        if (sprite_y >= 0xE0) {
            sprite_y -= 0x100;
        }
        if (line < sprite_y || line >= sprite_y + sprite_height) {
            continue;
        }
        ++visible_sprites;
        if (visible_sprites > 8) {
            status_ |= 0x40;
            if (visible_sprites > sprite_render_limit) {
                continue;
            }
        }

        const u16 attribute = static_cast<u16>((sprite_base + 0x80 + sprite * 2) & 0x3FFF);
        const int sprite_x = static_cast<int>(vram_[attribute]) - (shift_sprites_left ? 8 : 0);
        u8 tile = vram_[(attribute + 1) & 0x3FFF];
        if (tall_sprites) {
            tile = static_cast<u8>(tile & 0xFE);
        }

        int row = (line - sprite_y) / (zoomed_sprites ? 2 : 1);
        if (tall_sprites && row >= 8) {
            tile = static_cast<u8>(tile + 1);
            row -= 8;
        }
        const u16 pattern = static_cast<u16>((sprite_pattern + tile * 32 + row * 4) & 0x3FFF);
        for (int px = 0; px < 8; ++px) {
            const int bit = 7 - px;
            const u8 color = static_cast<u8>(
                (((vram_[pattern] >> bit) & 0x01) << 0) |
                (((vram_[(pattern + 1) & 0x3FFF] >> bit) & 0x01) << 1) |
                (((vram_[(pattern + 2) & 0x3FFF] >> bit) & 0x01) << 2) |
                (((vram_[(pattern + 3) & 0x3FFF] >> bit) & 0x01) << 3));
            if (color == 0) {
                continue;
            }
            const int pixel_width = zoomed_sprites ? 2 : 1;
            for (int zx = 0; zx < pixel_width; ++zx) {
                const int x = sprite_x + px * pixel_width + zx;
                if (x < 0 || x >= width) {
                    continue;
                }
                if (left_column_blank_enabled() && x < 8) {
                    continue;
                }
                if (sprite_pixels[static_cast<std::size_t>(x)]) {
                    if (visible_sprites <= 8) {
                        status_ |= 0x20;
                    }
                    continue;
                }
                sprite_pixels[static_cast<std::size_t>(x)] = true;
                if (scanline_bg_priority_[static_cast<std::size_t>(x)]) {
                    continue;
                }
                framebuffer_[line * width + x] = cram_color(static_cast<u8>(16 + color));
            }
        }
    }
}

void Vdp::render_tms_sprites(int line) {
    const u16 sprite_base = static_cast<u16>((registers_[5] & 0x7F) << 7);
    const u16 sprite_pattern = static_cast<u16>((registers_[6] & 0x07) << 11);
    const bool large_sprites = (registers_[1] & 0x02) != 0;
    const bool zoomed_sprites = (registers_[1] & 0x01) != 0;
    const int base_sprite_size = large_sprites ? 16 : 8;
    const int sprite_height = zoomed_sprites ? base_sprite_size * 2 : base_sprite_size;
    const int sprite_limit = enhancements_.disable_sprite_limit
        ? 32
        : (enhancements_.reduce_flicker ? 8 : 4);
    std::array<bool, width> sprite_pixels{};
    int visible_sprites = 0;

    for (int sprite = 0; sprite < 32; ++sprite) {
        const u16 attribute = static_cast<u16>((sprite_base + sprite * 4) & 0x3FFF);
        const u8 raw_y = vram_[attribute];
        if (raw_y == 0xD0) {
            break;
        }

        int sprite_y = static_cast<int>(raw_y) + 1;
        if (sprite_y >= 0xE0) {
            sprite_y -= 0x100;
        }
        if (line < sprite_y || line >= sprite_y + sprite_height) {
            continue;
        }
        ++visible_sprites;
        if (visible_sprites > 4) {
            if (visible_sprites == 5) {
                status_ = static_cast<u8>((status_ & 0xE0) | 0x40 | (sprite & 0x1F));
            }
            if (visible_sprites > sprite_limit) {
                continue;
            }
        }

        int sprite_x = vram_[(attribute + 1) & 0x3FFF];
        u8 tile = vram_[(attribute + 2) & 0x3FFF];
        const u8 color_byte = vram_[(attribute + 3) & 0x3FFF];
        const u8 color = static_cast<u8>(color_byte & 0x0F);
        if (color == 0) {
            continue;
        }
        if ((color_byte & 0x80) != 0) {
            sprite_x -= 32;
        }
        if (large_sprites) {
            tile = static_cast<u8>(tile & 0xFC);
        }

        int row = (line - sprite_y) / (zoomed_sprites ? 2 : 1);
        const int pattern_row = row & 0x07;
        const int tile_row = large_sprites ? row / 8 : 0;
        for (int px = 0; px < base_sprite_size; ++px) {
            const int tile_column = large_sprites ? px / 8 : 0;
            const u8 pattern_tile = static_cast<u8>(tile + tile_row * 2 + tile_column);
            const u16 pattern = static_cast<u16>((sprite_pattern + pattern_tile * 8 + pattern_row) & 0x3FFF);
            const int bit = 7 - (px & 0x07);
            if (((vram_[pattern] >> bit) & 0x01) == 0) {
                continue;
            }
            const int pixel_width = zoomed_sprites ? 2 : 1;
            for (int zx = 0; zx < pixel_width; ++zx) {
                const int x = sprite_x + px * pixel_width + zx;
                if (x < 0 || x >= width) {
                    continue;
                }
                if (sprite_pixels[static_cast<std::size_t>(x)]) {
                    if (visible_sprites <= 4) {
                        status_ |= 0x20;
                    }
                    continue;
                }
                sprite_pixels[static_cast<std::size_t>(x)] = true;
                framebuffer_[line * width + x] = tms_color(color);
            }
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

u32 Vdp::tms_color(u8 index) const {
    return kTmsPalette[index & 0x0F];
}

u32 Vdp::tms_display_color(u8 index) const {
    return tms_color((index & 0x0F) == 0 ? registers_[7] & 0x0F : index);
}

u8 Vdp::background_color_index(u16 pattern, int bit) const {
    return static_cast<u8>(
        (((vram_[pattern] >> bit) & 0x01) << 0) |
        (((vram_[(pattern + 1) & 0x3FFF] >> bit) & 0x01) << 1) |
        (((vram_[(pattern + 2) & 0x3FFF] >> bit) & 0x01) << 2) |
        (((vram_[(pattern + 3) & 0x3FFF] >> bit) & 0x01) << 3));
}

u8 Vdp::backdrop_color_index() const {
    return static_cast<u8>(16 + (registers_[7] & 0x0F));
}

u32 Vdp::backdrop_color() const {
    return cram_color(backdrop_color_index());
}

bool Vdp::left_column_blank_enabled() const {
    return (registers_[0] & 0x20) != 0;
}

u16 Vdp::name_table_base() const {
    return static_cast<u16>((registers_[2] & 0x0E) << 10);
}

u16 Vdp::background_pattern_base() const {
    return 0;
}

u16 Vdp::sprite_pattern_base() const {
    return static_cast<u16>((registers_[6] & 0x04) << 11);
}

bool Vdp::is_tms_mode() const {
    return video_mode_ != VdpVideoMode::SmsMode4;
}

void Vdp::update_tms_video_mode() {
    const bool m1 = (registers_[1] & 0x10) != 0;
    const bool m2 = (registers_[1] & 0x08) != 0;
    const bool m3 = (registers_[0] & 0x02) != 0;
    if (m1 && !m2 && !m3) {
        video_mode_ = VdpVideoMode::TmsText;
    } else if (!m1 && m2 && !m3) {
        video_mode_ = VdpVideoMode::TmsGraphics2;
    } else if (!m1 && !m2 && m3) {
        video_mode_ = VdpVideoMode::TmsMulticolor;
    } else {
        video_mode_ = VdpVideoMode::TmsGraphics1;
    }
}

VdpDebugSnapshot Vdp::debug_snapshot() const {
    return {
        scanline_,
        scanline_cycles_,
        timing_.cpu_cycles_per_scanline,
        timing_.scanlines_per_frame,
        line_counter_,
        status_,
        (registers_[1] & 0x40) != 0,
        (registers_[1] & 0x20) != 0,
        (registers_[0] & 0x10) != 0,
        (status_ & 0x80) != 0,
        line_irq_pending_,
        (status_ & 0x40) != 0,
        (status_ & 0x20) != 0,
    };
}

std::vector<VdpTileEntry> Vdp::debug_tilemap() const {
    const u16 name_base = name_table_base();
    std::vector<VdpTileEntry> entries;
    entries.reserve(32 * 32);

    for (u8 y = 0; y < 32; ++y) {
        for (u8 x = 0; x < 32; ++x) {
            const u16 address = static_cast<u16>((name_base + ((y * 32 + x) * 2)) & 0x3FFF);
            const u16 entry = make_u16(vram_[address], vram_[(address + 1) & 0x3FFF]);
            entries.push_back({
                x,
                y,
                address,
                static_cast<u16>(entry & 0x01FF),
                (entry & 0x0800) != 0,
                (entry & 0x0200) != 0,
                (entry & 0x0400) != 0,
                (entry & 0x1000) != 0,
            });
        }
    }

    return entries;
}

std::vector<VdpSpriteEntry> Vdp::debug_sprites() const {
    const u16 sprite_base = static_cast<u16>((registers_[5] & 0x7E) << 7);
    const bool shift_sprites_left = (registers_[0] & 0x08) != 0;
    std::vector<VdpSpriteEntry> entries;
    entries.reserve(64);

    for (u8 sprite = 0; sprite < 64; ++sprite) {
        const u8 raw_y = vram_[(sprite_base + sprite) & 0x3FFF];
        const bool terminator = raw_y == 0xD0;
        const u16 attribute = static_cast<u16>((sprite_base + 0x80 + sprite * 2) & 0x3FFF);
        int sprite_y = static_cast<int>(raw_y) + 1;
        if (sprite_y >= 0xE0) {
            sprite_y -= 0x100;
        }
        entries.push_back({
            sprite,
            raw_y,
            sprite_y,
            static_cast<int>(vram_[attribute]) - (shift_sprites_left ? 8 : 0),
            vram_[(attribute + 1) & 0x3FFF],
            terminator,
        });
        if (terminator) {
            break;
        }
    }

    return entries;
}

VdpState Vdp::save_state() const {
    return {
        vram_,
        cram_,
        registers_,
        framebuffer_,
        scanline_bg_priority_,
        address_,
        latch_,
        code_,
        pending_control_,
        status_,
        scanline_cycles_,
        scanline_,
        line_counter_,
        first_line_,
        line_irq_pending_,
        timing_,
        video_mode_,
        read_buffer_,
    };
}

void Vdp::load_state(const VdpState& state) {
    vram_ = state.vram;
    cram_ = state.cram;
    registers_ = state.registers;
    framebuffer_ = state.framebuffer;
    scanline_bg_priority_ = state.scanline_bg_priority;
    address_ = static_cast<u16>(state.address & 0x3FFF);
    latch_ = state.latch;
    code_ = static_cast<u8>(state.code & 0x03);
    pending_control_ = state.pending_control;
    status_ = state.status;
    scanline_cycles_ = state.scanline_cycles;
    scanline_ = state.scanline;
    line_counter_ = state.line_counter;
    first_line_ = state.first_line;
    line_irq_pending_ = state.line_irq_pending;
    set_timing(state.timing);
    video_mode_ = state.video_mode;
    read_buffer_ = state.read_buffer;
}

void Vdp::log_access(VdpAccessKind kind, u16 address, u8 value) {
    if (access_logging_enabled_) {
        logged_accesses_.push_back({current_cycle_, kind, address, value});
    }
}

} // namespace sgrecomp
