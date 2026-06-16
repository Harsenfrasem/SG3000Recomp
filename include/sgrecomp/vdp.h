#pragma once

#include "sgrecomp/types.h"

#include <array>

namespace sgrecomp {

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

    const std::array<u32, width * height>& framebuffer() const { return framebuffer_; }

private:
    std::array<u8, 16 * 1024> vram_{};
    std::array<u8, 32> cram_{};
    std::array<u8, 16> registers_{};
    std::array<u32, width * height> framebuffer_{};
    u16 address_ = 0;
    u8 latch_ = 0;
    u8 code_ = 0;
    bool pending_control_ = false;
    u8 status_ = 0;
    int scanline_cycles_ = 0;
    int scanline_ = 0;
};

} // namespace sgrecomp
