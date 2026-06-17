#include "sgrecomp/ym2413.h"

#include <algorithm>
#include <cmath>

namespace sgrecomp {
namespace {

constexpr double kCpuClock = 3579545.0;
constexpr double kTwoPi = 6.28318530717958647692;

} // namespace

void Ym2413::reset() {
    selected_register_ = 0;
    audio_control_ = 0;
    registers_.fill(0);
    phase_.fill(0.0);
    logged_writes_.clear();
}

void Ym2413::set_present(bool present) {
    present_ = present;
    if (!present_) {
        audio_control_ = 0;
    }
}

void Ym2413::set_write_logging_enabled(bool enabled) {
    write_logging_enabled_ = enabled;
    if (!enabled) {
        logged_writes_.clear();
    }
}

void Ym2413::write_address(u8 value) {
    if (!present_) {
        return;
    }
    selected_register_ = static_cast<u8>(value & 0x3F);
    log_write(0xF0, value);
}

void Ym2413::write_data(u8 value) {
    if (!present_) {
        return;
    }
    registers_[selected_register_ & 0x3F] = value;
    log_write(0xF1, value);
}

void Ym2413::write_audio_control(u8 value) {
    if (!present_) {
        return;
    }
    audio_control_ = static_cast<u8>(value & 0x03);
    log_write(0xF2, audio_control_);
}

u8 Ym2413::read_audio_control() const {
    if (!present_) {
        return 0x02;
    }
    return audio_control_;
}

void Ym2413::tick(int cpu_cycles) {
    if (!fm_enabled()) {
        return;
    }

    const double seconds = static_cast<double>(cpu_cycles) / kCpuClock;
    for (int channel = 0; channel < 9; ++channel) {
        phase_[channel] += channel_frequency(channel) * seconds;
        phase_[channel] -= std::floor(phase_[channel]);
    }
}

std::array<float, 2> Ym2413::sample() const {
    if (!fm_enabled()) {
        return {0.0F, 0.0F};
    }

    float mixed = 0.0F;
    for (int channel = 0; channel < 9; ++channel) {
        mixed += channel_sample(channel);
    }
    mixed = std::clamp(mixed / 9.0F, -1.0F, 1.0F);
    return {mixed, mixed};
}

float Ym2413::channel_sample(int channel) const {
    const u8 key_block_fnum = registers_[0x20 + channel];
    if ((key_block_fnum & 0x10) == 0) {
        return 0.0F;
    }

    const float amplitude = channel_amplitude(channel);
    const double modulator = std::sin(phase_[channel] * kTwoPi * 2.0) * 0.18;
    return static_cast<float>(std::sin((phase_[channel] + modulator) * kTwoPi) * amplitude);
}

double Ym2413::channel_frequency(int channel) const {
    const u16 fnum = static_cast<u16>(registers_[0x10 + channel] | ((registers_[0x20 + channel] & 0x01) << 8));
    const int block = (registers_[0x20 + channel] >> 1) & 0x07;
    if (fnum == 0) {
        return 0.0;
    }

    const double base = static_cast<double>(fnum) * 49716.0 / 524288.0;
    return base * static_cast<double>(1 << block);
}

float Ym2413::channel_amplitude(int channel) const {
    const u8 volume = registers_[0x30 + channel] & 0x0F;
    return static_cast<float>((15 - volume) / 15.0) * 0.65F;
}

void Ym2413::log_write(u8 port, u8 value) {
    if (write_logging_enabled_) {
        logged_writes_.push_back({current_cycle_, port, value});
    }
}

} // namespace sgrecomp
