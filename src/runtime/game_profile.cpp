#include "sgrecomp/game_profile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sgrecomp {
namespace {

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return {first, last};
}

std::string strip_quotes(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool parse_bool(const std::string& value) {
    const std::string lowered = lower_ascii(strip_quotes(value));
    if (lowered == "true" || lowered == "yes" || lowered == "on" || lowered == "1") {
        return true;
    }
    if (lowered == "false" || lowered == "no" || lowered == "off" || lowered == "0") {
        return false;
    }
    throw std::runtime_error("invalid boolean value in profile: " + value);
}

ConsoleModel parse_model(const std::string& value) {
    const std::string lowered = lower_ascii(strip_quotes(value));
    if (lowered == "sms") {
        return ConsoleModel::SMS;
    }
    if (lowered == "sg3000" || lowered == "sg-3000") {
        return ConsoleModel::SG3000;
    }
    throw std::runtime_error("invalid model in profile: " + value);
}

RuntimeMode parse_runtime_mode(const std::string& value) {
    const std::string lowered = lower_ascii(strip_quotes(value));
    if (lowered == "accurate") {
        return RuntimeMode::Accurate;
    }
    if (lowered == "hybrid") {
        return RuntimeMode::Hybrid;
    }
    if (lowered == "enhanced") {
        return RuntimeMode::Enhanced;
    }
    throw std::runtime_error("invalid runtime mode in profile: " + value);
}

void finish_profile(std::vector<GameProfile>& profiles, GameProfile& current, bool& in_profile) {
    if (!in_profile) {
        return;
    }
    if (current.hash.empty()) {
        throw std::runtime_error("profile missing hash");
    }
    profiles.push_back(current);
    current = {};
    in_profile = false;
}

} // namespace

std::string rom_hash_fnv1a64(std::span<const u8> rom) {
    constexpr u64 offset_basis = 14695981039346656037ULL;
    constexpr u64 prime = 1099511628211ULL;
    u64 hash = offset_basis;
    for (const u8 byte : rom) {
        hash ^= byte;
        hash *= prime;
    }

    std::ostringstream out;
    out << "fnv1a64:" << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

GameProfileDatabase GameProfileDatabase::parse(std::string_view text) {
    GameProfileDatabase database;
    GameProfile current;
    bool in_profile = false;
    std::istringstream input(std::string{text});
    std::string line;

    while (std::getline(input, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(std::move(line));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            finish_profile(database.profiles_, current, in_profile);
            if (line == "[profile]" || line == "[[profile]]") {
                in_profile = true;
                continue;
            }
            throw std::runtime_error("unknown profile section: " + line);
        }

        if (!in_profile) {
            throw std::runtime_error("profile key outside [profile] section");
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("invalid profile line: " + line);
        }
        const std::string key = lower_ascii(trim(line.substr(0, equals)));
        const std::string value = trim(line.substr(equals + 1));

        if (key == "name") {
            current.name = strip_quotes(value);
        } else if (key == "hash") {
            current.hash = lower_ascii(strip_quotes(value));
        } else if (key == "model") {
            current.model = parse_model(value);
            current.has_model = true;
        } else if (key == "mode") {
            current.enhancements.mode = parse_runtime_mode(value);
            current.has_enhancements = true;
        } else if (key == "disable_sprite_limit") {
            current.enhancements.disable_sprite_limit = parse_bool(value);
            if (current.enhancements.disable_sprite_limit) {
                current.enhancements.mode = RuntimeMode::Enhanced;
            }
            current.has_enhancements = true;
        } else if (key == "reduce_flicker") {
            current.enhancements.reduce_flicker = parse_bool(value);
            if (current.enhancements.reduce_flicker) {
                current.enhancements.mode = RuntimeMode::Enhanced;
            }
            current.has_enhancements = true;
        } else if (key == "enable_fm") {
            current.enhancements.enable_fm = parse_bool(value);
            current.has_enhancements = true;
        } else if (key == "audio_latency_ms") {
            current.audio_latency_ms = std::clamp(std::stoi(strip_quotes(value)), 10, 300);
            current.has_audio_latency_ms = true;
        } else if (key == "audio_sample_rate") {
            current.audio_sample_rate = static_cast<u32>(std::clamp(std::stoi(strip_quotes(value)), 8000, 96000));
            current.has_audio_sample_rate = true;
        } else if (key == "video_standard" || key == "region") {
            const std::string standard = lower_ascii(strip_quotes(value));
            if (standard != "ntsc" && standard != "pal") {
                throw std::runtime_error("invalid video standard in profile: " + value);
            }
            current.video_standard = host_video_standard_from_name(standard);
            current.has_video_standard = true;
        } else {
            throw std::runtime_error("unknown profile key: " + key);
        }
    }
    finish_profile(database.profiles_, current, in_profile);
    return database;
}

GameProfileDatabase GameProfileDatabase::load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open profile file");
    }
    std::ostringstream text;
    text << file.rdbuf();
    return parse(text.str());
}

const GameProfile* GameProfileDatabase::find_by_hash(std::string_view hash) const {
    const std::string lowered = lower_ascii(std::string{hash});
    const auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const GameProfile& profile) {
        return profile.hash == lowered;
    });
    return it == profiles_.end() ? nullptr : &*it;
}

} // namespace sgrecomp
