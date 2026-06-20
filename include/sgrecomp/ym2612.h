#pragma once

#include "sgrecomp/types.h"

#include <array>
#include <memory>
#include <vector>

namespace sgrecomp {

struct Ym2612State {
    bool enabled = false;
    std::array<u8, 2> selected_register{};
    std::array<u8, 0x200> registers{};
    std::array<bool, 6> key_on{};
    u64 clock_accumulator = 0;
    s16 output_left = 0;
    s16 output_right = 0;
    std::vector<u8> core_state;
};

class Ym2612 {
  public:
    Ym2612();
    ~Ym2612();
    Ym2612(const Ym2612&) = delete;
    Ym2612& operator=(const Ym2612&) = delete;

    void reset();
    void set_enabled(bool enabled);
    bool enabled() const;

    void write_address(int bank, u8 value);
    void write_data(int bank, u8 value);
    u8 read_status(int bank) const;
    void tick(int cpu_cycles);
    std::array<float, 2> sample() const;

    u8 selected_register(int bank) const;
    const std::array<u8, 0x200>& debug_registers() const;
    bool channel_key_on(int channel) const;
    Ym2612State save_state() const;
    void load_state(const Ym2612State& state);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sgrecomp
