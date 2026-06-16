#pragma once

#include <cstdint>

namespace sgrecomp {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;

constexpr u16 make_u16(u8 lo, u8 hi) {
    return static_cast<u16>(lo | (static_cast<u16>(hi) << 8));
}

} // namespace sgrecomp
