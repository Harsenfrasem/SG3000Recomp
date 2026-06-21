#pragma once

#include "sgrecomp/types.h"

#include <array>
#include <span>
#include <vector>

struct __OPLL;

namespace sgrecomp {

struct Ym2413Write {
    u64 cycle = 0;
    u8 port = 0;
    u8 value = 0;
};

struct Ym2413State {
    bool present = false;
    u8 selected_register = 0;
    u8 audio_control = 0;
    std::array<u8, 0x40> registers{};
    // Retained to load save-state versions 1-13 written by the former diagnostic synthesizer.
    std::array<double, 9> phase{};
    u64 clock_accumulator = 0;
    s16 output = 0;
    std::vector<u8> core_state;
};

class Ym2413 {
  public:
    Ym2413();
    ~Ym2413();
    Ym2413(const Ym2413&) = delete;
    Ym2413& operator=(const Ym2413&) = delete;

    void reset();
    void set_present(bool present);
    bool present() const {
        return present_;
    }

    void set_cycle(u64 cycle) {
        current_cycle_ = cycle;
    }
    void set_write_logging_enabled(bool enabled);
    const std::vector<Ym2413Write>& logged_writes() const {
        return logged_writes_;
    }

    void write_address(u8 value);
    void write_data(u8 value);
    void write_audio_control(u8 value);
    u8 read_audio_control() const;

    bool fm_enabled() const {
        return present_ && (audio_control_ == 0x01 || audio_control_ == 0x03);
    }
    bool psg_enabled() const {
        return !present_ || audio_control_ == 0x00 || audio_control_ == 0x03;
    }

    void tick(int cpu_cycles);
    std::array<float, 2> sample() const;

    const std::array<u8, 0x40>& debug_registers() const {
        return registers_;
    }
    u8 selected_register() const {
        return selected_register_;
    }
    Ym2413State save_state() const;
    void load_state(const Ym2413State& state);

  private:
    bool present_ = false;
    u8 selected_register_ = 0;
    u8 audio_control_ = 0;
    std::array<u8, 0x40> registers_{};
    u64 clock_accumulator_ = 0;
    s16 output_ = 0;
    __OPLL* core_ = nullptr;
    u64 current_cycle_ = 0;
    bool write_logging_enabled_ = false;
    std::vector<Ym2413Write> logged_writes_;

    void log_write(u8 port, u8 value);
    std::vector<u8> serialize_core() const;
    bool restore_core(std::span<const u8> bytes);
    void replay_registers();
};

} // namespace sgrecomp
