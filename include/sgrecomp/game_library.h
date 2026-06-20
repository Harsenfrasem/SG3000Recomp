#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgrecomp {

struct GameLibraryEntry {
    std::filesystem::path path;
    std::string hash;
    std::string alias;
    std::string platform;
    std::string region;
    std::string product_code;
};

std::vector<GameLibraryEntry> load_game_library(const std::filesystem::path& path, std::size_t limit = 50);
void save_game_library(const std::filesystem::path& path,
                       std::span<const GameLibraryEntry> entries,
                       std::size_t limit = 50);
std::vector<GameLibraryEntry>
touch_game_library(std::span<const GameLibraryEntry> entries, GameLibraryEntry game, std::size_t limit = 50);
bool set_game_library_alias(std::vector<GameLibraryEntry>& entries, std::string_view hash, std::string alias);

} // namespace sgrecomp
