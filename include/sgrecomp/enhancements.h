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
    bool enable_fm = false;
};

} // namespace sgrecomp
