#pragma once

namespace sgrecomp {

enum class RuntimeMode {
    Accurate,
    Hybrid,
    Enhanced,
};

struct EnhancementConfig {
    RuntimeMode mode = RuntimeMode::Accurate;
    bool disable_sprite_limit = false;
    bool reduce_flicker = false;
    int viewport_height = 0;
    bool enable_fm = false;
    bool enable_ym2612 = false;
};

} // namespace sgrecomp
