#pragma once

#include "sgrecomp/enhancements.h"
#include "sgrecomp/types.h"

#include <array>

namespace sgrecomp {

class Psg {
public:
    void write(u8 value);
    void tick(int cpu_cycles);
    std::array<float, 2> sample() const;
    void set_enhancements(const EnhancementConfig& config) { enhancements_ = config; }
    const EnhancementConfig& enhancements() const { return enhancements_; }

private:
    std::array<u16, 4> tone_{};
    std::array<u8, 4> volume_{{0x0F, 0x0F, 0x0F, 0x0F}};
    std::array<int, 4> counters_{};
    std::array<bool, 4> output_{{true, true, true, true}};
    u16 noise_lfsr_ = 0x4000;
    u8 latched_channel_ = 0;
    bool latched_volume_ = false;
    EnhancementConfig enhancements_{};

    int period(u8 channel) const;
    float volume_amplitude(u8 channel) const;
};

} // namespace sgrecomp
