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

enum class VdpVideoMode {
    SmsMode4,
    TmsGraphics1,
    TmsText,
    TmsGraphics2,
    TmsMulticolor,
};

struct VdpAccess {
    u64 cycle = 0;
    VdpAccessKind kind = VdpAccessKind::Vram;
    u16 address = 0;
    u8 value = 0;
};

struct VdpTimingConfig {
    int cpu_cycles_per_scanline = 228;
    int scanlines_per_frame = 262;
};

struct VdpDebugSnapshot {
    int scanline = 0;
    int scanline_cycles = 0;
    int cpu_cycles_per_scanline = 228;
    int scanlines_per_frame = 262;
    int active_height = 192;
    int line_counter = 0;
    u8 status = 0;
    bool display_enabled = false;
    bool frame_irq_enabled = false;
    bool line_irq_enabled = false;
    bool frame_irq_pending = false;
    bool line_irq_pending = false;
    bool sprite_overflow = false;
    bool sprite_collision = false;
};

struct VdpState;

class Vdp {
  public:
    static constexpr int width = 256;
    static constexpr int height = 192;
    static constexpr int max_height = 240;
    using Framebuffer = std::array<u32, width * max_height>;

    u8 read_data();
    u8 read_status();
    u8 read_v_counter() const;
    u8 read_h_counter() const;
    void write_data(u8 value);
    void write_control(u8 value);
    void tick(int cpu_cycles);
    bool irq_pending() const;
    int scanline() const {
        return scanline_;
    }
    void set_timing(const VdpTimingConfig& timing);
    const VdpTimingConfig& timing() const {
        return timing_;
    }
    void set_video_mode(VdpVideoMode mode) {
        video_mode_ = mode;
    }
    void set_game_gear(bool enabled) {
        game_gear_ = enabled;
    }
    bool game_gear() const {
        return game_gear_;
    }
    VdpVideoMode video_mode() const {
        return video_mode_;
    }
    int active_height() const;
    int viewport_x() const {
        return game_gear_ ? 48 : 0;
    }
    int viewport_y() const {
        return game_gear_ ? 24 : 0;
    }
    int viewport_width() const {
        return game_gear_ ? 160 : width;
    }
    int viewport_height() const;
    bool enhanced_output_active() const;
    void set_cycle(u64 cycle) {
        current_cycle_ = cycle;
    }
    void set_access_logging_enabled(bool enabled);
    const std::vector<VdpAccess>& logged_accesses() const {
        return logged_accesses_;
    }
    void set_enhancements(const EnhancementConfig& config) {
        enhancements_ = config;
    }
    const EnhancementConfig& enhancements() const {
        return enhancements_;
    }

    const Framebuffer& framebuffer() const {
        return framebuffer_;
    }
    const Framebuffer& enhanced_framebuffer() const {
        return enhanced_framebuffer_;
    }
    const Framebuffer& display_framebuffer() const {
        return enhanced_output_active() ? enhanced_framebuffer_ : framebuffer_;
    }
    const std::array<u8, 16 * 1024>& debug_vram() const {
        return vram_;
    }
    const std::array<u8, 32>& debug_cram() const {
        return cram_;
    }
    const std::array<u8, 64>& debug_game_gear_cram() const {
        return game_gear_cram_;
    }
    const std::array<u8, 16>& debug_registers() const {
        return registers_;
    }
    VdpDebugSnapshot debug_snapshot() const;
    std::vector<VdpTileEntry> debug_tilemap() const;
    std::vector<VdpSpriteEntry> debug_sprites() const;
    VdpState save_state() const;
    void load_state(const VdpState& state);

  private:
    std::array<u8, 16 * 1024> vram_{};
    std::array<u8, 32> cram_{};
    std::array<u8, 64> game_gear_cram_{};
    std::array<u8, 16> registers_{};
    Framebuffer framebuffer_{};
    Framebuffer enhanced_framebuffer_{};
    Framebuffer* render_target_ = nullptr;
    std::array<bool, width> scanline_bg_priority_{};
    u16 address_ = 0;
    u8 latch_ = 0;
    u8 code_ = 0;
    u8 read_buffer_ = 0;
    bool pending_control_ = false;
    u8 status_ = 0;
    int scanline_cycles_ = 0;
    int scanline_ = 0;
    int line_counter_ = 0;
    bool first_line_ = true;
    bool line_irq_pending_ = false;
    VdpTimingConfig timing_{};
    VdpVideoMode video_mode_ = VdpVideoMode::SmsMode4;
    bool game_gear_ = false;
    u64 current_cycle_ = 0;
    bool access_logging_enabled_ = false;
    std::vector<VdpAccess> logged_accesses_;
    EnhancementConfig enhancements_{};

    void advance_scanline();
    void render_scanline(int line);
    void render_scanline_pass(int line);
    void render_enhanced_scanline(int line);
    void render_mode4_scanline(int line);
    void render_tms_graphics1_scanline(int line);
    void render_tms_text_scanline(int line);
    void render_tms_graphics2_scanline(int line);
    void render_tms_multicolor_scanline(int line);
    void render_sprites(int line);
    void render_tms_sprites(int line);
    u8 background_color_index(u16 pattern, int bit) const;
    u32 cram_color(u8 index) const;
    u32 tms_color(u8 index) const;
    u32 tms_display_color(u8 index) const;
    u8 backdrop_color_index() const;
    u32 backdrop_color() const;
    bool left_column_blank_enabled() const;
    u16 name_table_base() const;
    u16 background_pattern_base() const;
    u16 sprite_pattern_base() const;
    bool is_tms_mode() const;
    void update_tms_video_mode();
    void log_access(VdpAccessKind kind, u16 address, u8 value);
};

struct VdpState {
    std::array<u8, 16 * 1024> vram{};
    std::array<u8, 32> cram{};
    std::array<u8, 64> game_gear_cram{};
    std::array<u8, 16> registers{};
    Vdp::Framebuffer framebuffer{};
    std::array<bool, Vdp::width> scanline_bg_priority{};
    u16 address = 0;
    u8 latch = 0;
    u8 code = 0;
    bool pending_control = false;
    u8 status = 0;
    int scanline_cycles = 0;
    int scanline = 0;
    int line_counter = 0;
    bool first_line = true;
    bool line_irq_pending = false;
    VdpTimingConfig timing{};
    VdpVideoMode video_mode = VdpVideoMode::SmsMode4;
    bool game_gear = false;
    u8 read_buffer = 0;
};

} // namespace sgrecomp
