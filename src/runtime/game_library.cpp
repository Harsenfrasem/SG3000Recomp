#include "sgrecomp/game_library.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sgrecomp {
namespace {

std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::filesystem::path path_from_utf8(const std::string& encoded) {
    const auto* begin = reinterpret_cast<const char8_t*>(encoded.data());
    return std::filesystem::path(std::u8string(begin, begin + encoded.size()));
}

std::string single_line(std::string value) {
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    return value;
}

} // namespace

std::vector<GameLibraryEntry> load_game_library(const std::filesystem::path& path, std::size_t limit) {
    std::vector<GameLibraryEntry> entries;
    std::ifstream file(path, std::ios::binary);
    std::string line;
    while (entries.size() < limit && std::getline(file, line)) {
        std::istringstream parser(line);
        std::string encoded_path;
        GameLibraryEntry entry;
        if (!(parser >> std::quoted(encoded_path) >> std::quoted(entry.hash) >> std::quoted(entry.alias) >>
              std::quoted(entry.platform) >> std::quoted(entry.region) >> std::quoted(entry.product_code))) {
            continue;
        }
        entry.path = path_from_utf8(encoded_path);
        if (entry.hash.empty() || !std::filesystem::is_regular_file(entry.path)) {
            continue;
        }
        if (std::none_of(
                entries.begin(), entries.end(), [&](const auto& existing) { return existing.hash == entry.hash; })) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

void save_game_library(const std::filesystem::path& path,
                       std::span<const GameLibraryEntry> entries,
                       std::size_t limit) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("cannot save game library");
    }
    std::size_t written = 0;
    for (const auto& entry : entries) {
        if (written >= limit || entry.hash.empty() || !std::filesystem::is_regular_file(entry.path)) {
            continue;
        }
        file << std::quoted(path_to_utf8(entry.path.lexically_normal())) << ' ' << std::quoted(entry.hash) << ' '
             << std::quoted(single_line(entry.alias)) << ' ' << std::quoted(single_line(entry.platform)) << ' '
             << std::quoted(single_line(entry.region)) << ' ' << std::quoted(single_line(entry.product_code)) << '\n';
        ++written;
    }
    if (!file) {
        throw std::runtime_error("cannot write game library");
    }
}

std::vector<GameLibraryEntry>
touch_game_library(std::span<const GameLibraryEntry> entries, GameLibraryEntry game, std::size_t limit) {
    std::vector<GameLibraryEntry> updated;
    if (limit == 0 || game.hash.empty() || game.path.empty()) {
        return updated;
    }
    const auto previous = std::find_if(
        entries.begin(), entries.end(), [&](const GameLibraryEntry& entry) { return entry.hash == game.hash; });
    if (previous != entries.end() && game.alias.empty()) {
        game.alias = previous->alias;
    }
    updated.push_back(std::move(game));
    for (const auto& entry : entries) {
        if (updated.size() >= limit) {
            break;
        }
        if (entry.hash != updated.front().hash && std::filesystem::is_regular_file(entry.path)) {
            updated.push_back(entry);
        }
    }
    return updated;
}

bool set_game_library_alias(std::vector<GameLibraryEntry>& entries, std::string_view hash, std::string alias) {
    const auto entry = std::find_if(
        entries.begin(), entries.end(), [&](const GameLibraryEntry& candidate) { return candidate.hash == hash; });
    if (entry == entries.end()) {
        return false;
    }
    entry->alias = single_line(std::move(alias));
    return true;
}

} // namespace sgrecomp
