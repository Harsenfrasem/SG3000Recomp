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
    const auto first =
        std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last =
        std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
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
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
    if (lowered == "gamegear" || lowered == "game-gear" || lowered == "gg") {
        return ConsoleModel::GameGear;
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

const char* runtime_mode_name(RuntimeMode mode) {
    switch (mode) {
    case RuntimeMode::Accurate:
        return "accurate";
    case RuntimeMode::Hybrid:
        return "hybrid";
    case RuntimeMode::Enhanced:
        return "enhanced";
    }
    return "accurate";
}

std::string safe_profile_name(std::string value) {
    std::replace_if(
        value.begin(),
        value.end(),
        [](char character) { return character == '"' || character == '#' || character == '\r' || character == '\n'; },
        '_');
    return value;
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

std::string game_profile_fingerprint(const GameProfile& profile) {
    std::ostringstream canonical;
    canonical << "hash=" << profile.hash << ";model=" << profile.has_model << ':' << static_cast<int>(profile.model)
              << ";mapper=" << profile.has_mapper << ':' << static_cast<int>(profile.mapper)
              << ";enhancements=" << profile.has_enhancements << ':' << static_cast<int>(profile.enhancements.mode)
              << ':' << profile.enhancements.disable_sprite_limit << ':' << profile.enhancements.reduce_flicker << ':'
              << profile.enhancements.enable_fm << ':' << profile.enhancements.enable_ym2612
              << ";audio_latency=" << profile.has_audio_latency_ms << ':' << profile.audio_latency_ms
              << ";audio_rate=" << profile.has_audio_sample_rate << ':' << profile.audio_sample_rate
              << ";video_standard=" << profile.has_video_standard << ':' << static_cast<int>(profile.video_standard);
    const std::string text = canonical.str();
    return rom_hash_fnv1a64(std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size()));
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
        } else if (key == "mapper") {
            current.mapper = cartridge_mapper_from_name(lower_ascii(strip_quotes(value)));
            current.has_mapper = true;
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
        } else if (key == "enable_ym2612") {
            current.enhancements.enable_ym2612 = parse_bool(value);
            if (current.enhancements.enable_ym2612) {
                current.enhancements.mode = RuntimeMode::Enhanced;
            }
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
    const auto it = std::find_if(
        profiles_.begin(), profiles_.end(), [&](const GameProfile& profile) { return profile.hash == lowered; });
    return it == profiles_.end() ? nullptr : &*it;
}

std::string serialize_game_profiles(std::span<const GameProfile> profiles) {
    std::ostringstream out;
    out << "# Local SG3000Recomp profiles. Managed by the GUI or editable as text.\n";
    for (const auto& profile : profiles) {
        if (profile.hash.empty()) {
            throw std::invalid_argument("cannot serialize a profile without hash");
        }
        out << "\n[profile]\n";
        if (!profile.name.empty()) {
            out << "name = \"" << safe_profile_name(profile.name) << "\"\n";
        }
        out << "hash = \"" << profile.hash << "\"\n";
        if (profile.has_model) {
            const char* model = profile.model == ConsoleModel::SG3000
                                    ? "sg3000"
                                    : (profile.model == ConsoleModel::GameGear ? "gamegear" : "sms");
            out << "model = \"" << model << "\"\n";
        }
        if (profile.has_mapper) {
            out << "mapper = \"" << cartridge_mapper_name(profile.mapper) << "\"\n";
        }
        if (profile.has_enhancements) {
            out << "mode = \"" << runtime_mode_name(profile.enhancements.mode) << "\"\n"
                << "disable_sprite_limit = " << (profile.enhancements.disable_sprite_limit ? "true" : "false")
                << "\nreduce_flicker = " << (profile.enhancements.reduce_flicker ? "true" : "false")
                << "\nenable_fm = " << (profile.enhancements.enable_fm ? "true" : "false")
                << "\nenable_ym2612 = " << (profile.enhancements.enable_ym2612 ? "true" : "false") << "\n";
        }
        if (profile.has_audio_latency_ms) {
            out << "audio_latency_ms = " << profile.audio_latency_ms << "\n";
        }
        if (profile.has_audio_sample_rate) {
            out << "audio_sample_rate = " << profile.audio_sample_rate << "\n";
        }
        if (profile.has_video_standard) {
            out << "video_standard = \"" << host_video_standard_name(profile.video_standard) << "\"\n";
        }
    }
    return out.str();
}

void save_game_profiles(const std::filesystem::path& path, std::span<const GameProfile> profiles) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        throw std::runtime_error("cannot save profile file");
    }
    file << serialize_game_profiles(profiles);
    if (!file) {
        throw std::runtime_error("cannot write profile file");
    }
}

} // namespace sgrecomp
