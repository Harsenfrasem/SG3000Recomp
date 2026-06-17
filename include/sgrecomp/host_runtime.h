#pragma once

#include "sgrecomp/console.h"
#include "sgrecomp/types.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace sgrecomp {

enum class HostVideoStandard {
    Ntsc,
    Pal,
};

inline HostVideoStandard host_video_standard_from_name(std::string_view name) {
    if (name == "ntsc" || name == "NTSC") {
        return HostVideoStandard::Ntsc;
    }
    if (name == "pal" || name == "PAL") {
        return HostVideoStandard::Pal;
    }
    return HostVideoStandard::Ntsc;
}

inline const char* host_video_standard_name(HostVideoStandard standard) {
    switch (standard) {
    case HostVideoStandard::Ntsc: return "ntsc";
    case HostVideoStandard::Pal: return "pal";
    }
    return "ntsc";
}

struct HostRuntimeConfig {
    u32 cpu_clock_hz = 3579545;
    u32 audio_sample_rate = 44100;
    int cpu_cycles_per_scanline = 228;
    int scanlines_per_frame = 262;

    u64 cycles_per_frame() const {
        return static_cast<u64>(cpu_cycles_per_scanline) * static_cast<u64>(scanlines_per_frame);
    }
};

inline HostRuntimeConfig host_runtime_config_for_video_standard(HostVideoStandard standard) {
    HostRuntimeConfig config;
    if (standard == HostVideoStandard::Pal) {
        config.scanlines_per_frame = 313;
    }
    return config;
}

struct HostInputState {
    u8 player1 = 0;
    u8 player2 = 0;
    bool pause = false;
};

struct HostFrameResult {
    u64 frame_index = 0;
    u64 start_cycle = 0;
    u64 end_cycle = 0;
    std::size_t stereo_samples = 0;
    bool halted = false;
};

struct HostRuntimeState {
    ConsoleState console;
    u64 frame_index = 0;
    u64 audio_cycle_accumulator = 0;
    bool previous_pause = false;
};

class HostRuntime {
public:
    explicit HostRuntime(ConsoleModel model, HostRuntimeConfig config = {});
    HostRuntime(ConsoleModel model, const EnhancementConfig& enhancements, HostRuntimeConfig config = {});

    void load_rom(std::span<const u8> rom);
    void load_bios(std::span<const u8> bios);
    void reset();
    HostRuntimeState save_state() const;
    void load_state(const HostRuntimeState& state);

    HostFrameResult run_frame(const HostInputState& input = {});
    void clear_audio();

    Console& console() { return console_; }
    const Console& console() const { return console_; }
    const HostRuntimeConfig& config() const { return config_; }
    const std::array<u32, Vdp::width * Vdp::height>& framebuffer() const;
    const std::vector<s16>& audio() const { return audio_; }

private:
    Console console_;
    HostRuntimeConfig config_{};
    std::vector<s16> audio_;
    u64 frame_index_ = 0;
    u64 audio_cycle_accumulator_ = 0;
    bool previous_pause_ = false;

    void apply_input(const HostInputState& input);
    void run_until_cycle(u64 target_cycle);
    void tick_devices(int elapsed_cycles);
    void append_audio_samples(int elapsed_cycles);
};

} // namespace sgrecomp
