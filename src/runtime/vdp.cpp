#include "sgrecomp/vdp.h"

namespace sgrecomp {

u8 Vdp::read_data() {
    const u8 value = vram_[address_ & 0x3FFF];
    address_ = static_cast<u16>((address_ + 1) & 0x3FFF);
    return value;
}

u8 Vdp::read_status() {
    const u8 value = status_;
    status_ = 0;
    pending_control_ = false;
    return value;
}

u8 Vdp::read_v_counter() const {
    if (scanline_ <= 0xDA) {
        return static_cast<u8>(scanline_);
    }
    return static_cast<u8>(scanline_ - 6);
}

u8 Vdp::read_h_counter() const {
    return static_cast<u8>((scanline_cycles_ * 342) / 228);
}

void Vdp::write_data(u8 value) {
    if (code_ == 3) {
        cram_[address_ & 0x1F] = value;
    } else {
        vram_[address_ & 0x3FFF] = value;
    }
    address_ = static_cast<u16>((address_ + 1) & 0x3FFF);
}

void Vdp::write_control(u8 value) {
    if (!pending_control_) {
        latch_ = value;
        pending_control_ = true;
        return;
    }

    const u8 code = static_cast<u8>((value >> 6) & 0x03);
    code_ = code;
    address_ = static_cast<u16>(((value & 0x3F) << 8) | latch_);
    if (code == 2) {
        registers_[value & 0x0F] = latch_;
        if ((value & 0x0F) == 10) {
            line_counter_ = latch_;
        }
    }
    pending_control_ = false;
}

void Vdp::tick(int cpu_cycles) {
    scanline_cycles_ += cpu_cycles;
    while (scanline_cycles_ >= 228) {
        scanline_cycles_ -= 228;
        advance_scanline();
    }
}

bool Vdp::irq_pending() const {
    const bool frame_irq = (status_ & 0x80) != 0 && (registers_[1] & 0x20) != 0;
    const bool line_irq = (status_ & 0x40) != 0 && (registers_[0] & 0x10) != 0;
    return frame_irq || line_irq;
}

void Vdp::advance_scanline() {
    ++scanline_;

    if (scanline_ < height) {
        if (first_line_) {
            line_counter_ = registers_[10];
            first_line_ = false;
        } else if (line_counter_ == 0) {
            line_counter_ = registers_[10];
            status_ |= 0x40;
        } else {
            --line_counter_;
        }
    }

    if (scanline_ == height) {
        status_ |= 0x80;
        line_counter_ = registers_[10];
    }

    if (scanline_ >= 262) {
        scanline_ = 0;
        first_line_ = true;
    }
}

} // namespace sgrecomp
