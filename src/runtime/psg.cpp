#include "sgrecomp/psg.h"

namespace sgrecomp {

void Psg::write(u8 value) {
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

void Psg::tick(int) {}

std::array<float, 2> Psg::sample() const {
    return {0.0F, 0.0F};
}

} // namespace sgrecomp
