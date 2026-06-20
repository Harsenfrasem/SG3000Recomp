#include "sgrecomp/recent_games.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

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

bool same_path(const std::filesystem::path& left, const std::filesystem::path& right) {
    return left.lexically_normal() == right.lexically_normal();
}

} // namespace

std::vector<std::filesystem::path> load_recent_games(const std::filesystem::path& list_path, std::size_t limit) {
    std::vector<std::filesystem::path> games;
    std::ifstream file(list_path, std::ios::binary);
    std::string line;
    while (games.size() < limit && std::getline(file, line)) {
        std::istringstream parser(line);
        std::string encoded;
        if (!(parser >> std::quoted(encoded))) {
            continue;
        }
        const std::filesystem::path game = path_from_utf8(encoded);
        if (!std::filesystem::is_regular_file(game)) {
            continue;
        }
        if (std::none_of(games.begin(), games.end(), [&](const auto& existing) { return same_path(existing, game); })) {
            games.push_back(game);
        }
    }
    return games;
}

void save_recent_games(const std::filesystem::path& list_path,
                       std::span<const std::filesystem::path> games,
                       std::size_t limit) {
    std::filesystem::create_directories(list_path.parent_path());
    std::ofstream file(list_path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return;
    }
    std::size_t written = 0;
    for (const auto& game : games) {
        if (written >= limit || !std::filesystem::is_regular_file(game)) {
            continue;
        }
        file << std::quoted(path_to_utf8(game.lexically_normal())) << '\n';
        ++written;
    }
}

std::vector<std::filesystem::path>
touch_recent_game(std::span<const std::filesystem::path> games, const std::filesystem::path& game, std::size_t limit) {
    std::vector<std::filesystem::path> updated;
    if (limit == 0 || game.empty()) {
        return updated;
    }
    updated.push_back(game.lexically_normal());
    for (const auto& existing : games) {
        if (updated.size() >= limit) {
            break;
        }
        const bool already_present = std::any_of(
            updated.begin(), updated.end(), [&](const auto& present) { return same_path(existing, present); });
        if (!already_present && std::filesystem::is_regular_file(existing)) {
            updated.push_back(existing.lexically_normal());
        }
    }
    return updated;
}

} // namespace sgrecomp
