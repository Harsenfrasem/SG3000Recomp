#include "sgrecomp/psg.h"

namespace sgrecomp {
namespace {

constexpr float kVolumeTable[16] = {
    1.0000F, 0.7943F, 0.6310F, 0.5012F,
    0.3981F, 0.3162F, 0.2512F, 0.1995F,
    0.1585F, 0.1259F, 0.1000F, 0.0794F,
    0.0631F, 0.0501F, 0.0398F, 0.0000F,
};

} // namespace

void Psg::write(u8 value) {
    if (write_logging_enabled_) {
        logged_writes_.push_back({current_cycle_, value});
    }

    if (value & 0x80) {
        latched_channel_ = static_cast<u8>((value >> 5) & 0x03);
        latched_volume_ = (value & 0x10) != 0;
        if (latched_volume_) {
            volume_[latched_channel_] = static_cast<u8>(value & 0x0F);
        } else {
            tone_[latched_channel_] = static_cast<u16>((tone_[latched_channel_] & 0x3F0) | (value & 0x0F));
        }
    } else if (latched_volume_) {
        volume_[latched_channel_] = static_cast<u8>(value & 0x0F);
    } else {
        tone_[latched_channel_] = static_cast<u16>((tone_[latched_channel_] & 0x00F) | ((value & 0x3F) << 4));
    }
}

void Psg::set_write_logging_enabled(bool enabled) {
    write_logging_enabled_ = enabled;
    if (!enabled) {
        logged_writes_.clear();
    }
}

void Psg::tick(int cpu_cycles) {
    for (u8 channel = 0; channel < 3; ++channel) {
        counters_[channel] -= cpu_cycles;
        const int reload = period(channel);
        while (counters_[channel] <= 0) {
            counters_[channel] += reload;
            output_[channel] = !output_[channel];
        }
    }

    counters_[3] -= cpu_cycles;
    const int noise_reload = period(3);
    while (counters_[3] <= 0) {
        counters_[3] += noise_reload;
        const bool white_noise = (tone_[3] & 0x04) != 0;
        const u16 feedback = white_noise
            ? static_cast<u16>((noise_lfsr_ ^ (noise_lfsr_ >> 3)) & 0x01)
            : static_cast<u16>(noise_lfsr_ & 0x01);
        noise_lfsr_ = static_cast<u16>((noise_lfsr_ >> 1) | (feedback << 14));
        output_[3] = (noise_lfsr_ & 0x01) != 0;
    }
}

std::array<float, 2> Psg::sample() const {
    float mixed = 0.0F;
    for (u8 channel = 0; channel < 4; ++channel) {
        mixed += (output_[channel] ? 1.0F : -1.0F) * volume_amplitude(channel);
    }
    mixed *= 0.25F;
    return {mixed, mixed};
}

int Psg::period(u8 channel) const {
    if (channel < 3) {
        return static_cast<int>((tone_[channel] == 0 ? 1 : tone_[channel]) * 16);
    }

    switch (tone_[3] & 0x03) {
    case 0: return 16;
    case 1: return 32;
    case 2: return 64;
    default: return period(2);
    }
}

float Psg::volume_amplitude(u8 channel) const {
    return kVolumeTable[volume_[channel] & 0x0F];
}

} // namespace sgrecomp
