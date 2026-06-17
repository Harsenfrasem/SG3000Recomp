#pragma once

#include "sgrecomp/types.h"

#include <array>
#include <vector>

namespace sgrecomp {

struct Ym2413Write {
    u64 cycle = 0;
    u8 port = 0;
    u8 value = 0;
};

class Ym2413 {
public:
    void reset();
    void set_present(bool present);
    bool present() const { return present_; }

    void set_cycle(u64 cycle) { current_cycle_ = cycle; }
    void set_write_logging_enabled(bool enabled);
    const std::vector<Ym2413Write>& logged_writes() const { return logged_writes_; }

    void write_address(u8 value);
    void write_data(u8 value);
    void write_audio_control(u8 value);
    u8 read_audio_control() const;

    bool fm_enabled() const { return present_ && (audio_control_ == 0x01 || audio_control_ == 0x03); }
    bool psg_enabled() const { return !present_ || audio_control_ == 0x00 || audio_control_ == 0x03; }

    void tick(int cpu_cycles);
    std::array<float, 2> sample() const;

    const std::array<u8, 0x40>& debug_registers() const { return registers_; }
    u8 selected_register() const { return selected_register_; }

private:
    bool present_ = false;
    u8 selected_register_ = 0;
    u8 audio_control_ = 0;
    std::array<u8, 0x40> registers_{};
    std::array<double, 9> phase_{};
    u64 current_cycle_ = 0;
    bool write_logging_enabled_ = false;
    std::vector<Ym2413Write> logged_writes_;

    float channel_sample(int channel) const;
    double channel_frequency(int channel) const;
    float channel_amplitude(int channel) const;
    void log_write(u8 port, u8 value);
};

} // namespace sgrecomp
