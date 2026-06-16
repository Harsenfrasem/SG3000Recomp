#pragma once

#include "sgrecomp/types.h"

#include <array>

namespace sgrecomp {

class Psg {
public:
    void write(u8 value);
    void tick(int cpu_cycles);
    std::array<float, 2> sample() const;

private:
    std::array<u16, 4> tone_{};
    std::array<u8, 4> volume_{};
    u8 latched_channel_ = 0;
    bool latched_volume_ = false;
};

} // namespace sgrecomp
