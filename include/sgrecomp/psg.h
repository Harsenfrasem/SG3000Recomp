#pragma once

#include "sgrecomp/enhancements.h"
#include "sgrecomp/types.h"

#include <array>
#include <vector>

namespace sgrecomp {

struct PsgWrite {
    u64 cycle = 0;
    u8 value = 0;
};

struct PsgState {
    std::array<u16, 4> tone{};
    std::array<u8, 4> volume{};
    std::array<int, 4> counters{};
    std::array<bool, 4> output{};
    u16 noise_lfsr = 0;
    u8 latched_channel = 0;
    bool latched_volume = false;
};

class Psg {
public:
    void write(u8 value);
    void tick(int cpu_cycles);
    std::array<float, 2> sample() const;
    void set_enhancements(const EnhancementConfig& config) { enhancements_ = config; }
    const EnhancementConfig& enhancements() const { return enhancements_; }
    void set_cycle(u64 cycle) { current_cycle_ = cycle; }
    void set_write_logging_enabled(bool enabled);
    const std::vector<PsgWrite>& logged_writes() const { return logged_writes_; }
    PsgState save_state() const;
    void load_state(const PsgState& state);

private:
    std::array<u16, 4> tone_{};
    std::array<u8, 4> volume_{{0x0F, 0x0F, 0x0F, 0x0F}};
    std::array<int, 4> counters_{};
    std::array<bool, 4> output_{{true, true, true, true}};
    u16 noise_lfsr_ = 0x4000;
    u8 latched_channel_ = 0;
    bool latched_volume_ = false;
    EnhancementConfig enhancements_{};
    u64 current_cycle_ = 0;
    bool write_logging_enabled_ = false;
    std::vector<PsgWrite> logged_writes_;

    int period(u8 channel) const;
    float volume_amplitude(u8 channel) const;
};

} // namespace sgrecomp
