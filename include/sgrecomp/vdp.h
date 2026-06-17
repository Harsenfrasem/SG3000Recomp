#pragma once

#include "sgrecomp/enhancements.h"
#include "sgrecomp/types.h"

#include <array>
#include <vector>

namespace sgrecomp {

struct VdpTileEntry {
    u8 x = 0;
    u8 y = 0;
    u16 address = 0;
    u16 tile = 0;
    bool palette1 = false;
    bool flip_x = false;
    bool flip_y = false;
    bool priority = false;
};

struct VdpSpriteEntry {
    u8 index = 0;
    u8 raw_y = 0;
    int y = 0;
    int x = 0;
    u8 tile = 0;
    bool terminator = false;
};

enum class VdpAccessKind {
    Vram,
    Cram,
    Register,
};

struct VdpAccess {
    u64 cycle = 0;
    VdpAccessKind kind = VdpAccessKind::Vram;
    u16 address = 0;
    u8 value = 0;
};

class Vdp {
public:
    static constexpr int width = 256;
    static constexpr int height = 192;

    u8 read_data();
    u8 read_status();
    u8 read_v_counter() const;
    u8 read_h_counter() const;
    void write_data(u8 value);
    void write_control(u8 value);
    void tick(int cpu_cycles);
    bool irq_pending() const;
    int scanline() const { return scanline_; }
    void set_cycle(u64 cycle) { current_cycle_ = cycle; }
    void set_access_logging_enabled(bool enabled);
    const std::vector<VdpAccess>& logged_accesses() const { return logged_accesses_; }
    void set_enhancements(const EnhancementConfig& config) { enhancements_ = config; }
    const EnhancementConfig& enhancements() const { return enhancements_; }

    const std::array<u32, width * height>& framebuffer() const { return framebuffer_; }
    const std::array<u8, 16 * 1024>& debug_vram() const { return vram_; }
    const std::array<u8, 32>& debug_cram() const { return cram_; }
    const std::array<u8, 16>& debug_registers() const { return registers_; }
    std::vector<VdpTileEntry> debug_tilemap() const;
    std::vector<VdpSpriteEntry> debug_sprites() const;

private:
    std::array<u8, 16 * 1024> vram_{};
    std::array<u8, 32> cram_{};
    std::array<u8, 16> registers_{};
    std::array<u32, width * height> framebuffer_{};
    std::array<bool, width> scanline_bg_priority_{};
    u16 address_ = 0;
    u8 latch_ = 0;
    u8 code_ = 0;
    bool pending_control_ = false;
    u8 status_ = 0;
    int scanline_cycles_ = 0;
    int scanline_ = 0;
    int line_counter_ = 0;
    bool first_line_ = true;
    u64 current_cycle_ = 0;
    bool access_logging_enabled_ = false;
    std::vector<VdpAccess> logged_accesses_;
    EnhancementConfig enhancements_{};

    void advance_scanline();
    void render_scanline(int line);
    void render_sprites(int line);
    u32 cram_color(u8 index) const;
    void log_access(VdpAccessKind kind, u16 address, u8 value);
};

} // namespace sgrecomp
