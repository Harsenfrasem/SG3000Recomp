#pragma once

#include "sgrecomp/bus.h"
#include "sgrecomp/enhancements.h"
#include "sgrecomp/types.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sgrecomp {

struct GameProfile {
    std::string name;
    std::string hash;
    bool has_model = false;
    ConsoleModel model = ConsoleModel::SMS;
    bool has_enhancements = false;
    EnhancementConfig enhancements;
    bool has_audio_latency_ms = false;
    int audio_latency_ms = 0;
};

class GameProfileDatabase {
public:
    static GameProfileDatabase parse(std::string_view text);
    static GameProfileDatabase load(const std::filesystem::path& path);

    const std::vector<GameProfile>& profiles() const { return profiles_; }
    const GameProfile* find_by_hash(std::string_view hash) const;

private:
    std::vector<GameProfile> profiles_;
};

std::string rom_hash_fnv1a64(std::span<const u8> rom);

} // namespace sgrecomp
