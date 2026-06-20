#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace sgrecomp {

std::vector<std::filesystem::path> load_recent_games(const std::filesystem::path& list_path, std::size_t limit = 10);
void save_recent_games(const std::filesystem::path& list_path,
                       std::span<const std::filesystem::path> games,
                       std::size_t limit = 10);
std::vector<std::filesystem::path> touch_recent_game(std::span<const std::filesystem::path> games,
                                                     const std::filesystem::path& game,
                                                     std::size_t limit = 10);

} // namespace sgrecomp
