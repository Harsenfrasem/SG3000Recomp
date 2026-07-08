#pragma once

#include "sgrecomp/types.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace sgrecomp {

enum class CartridgeHeaderRegion {
    Unknown,
    SmsJapan,
    SmsExport,
    GameGearJapan,
    GameGearExport,
    GameGearInternational,
};

enum class CartridgePlatform {
    Unknown,
    MasterSystem,
    GameGear,
};

enum class CartridgeImageModelHint {
    Unknown,
    MasterSystem,
    SG3000,
    GameGear,
};

struct CartridgeHeaderInfo {
    bool found = false;
    std::size_t offset = 0;
    u16 stored_checksum = 0;
    u16 diagnostic_checksum = 0;
    u16 declared_size_checksum = 0;
    std::string product_code;
    u8 version = 0;
    u8 region_size = 0;
    std::size_t declared_size_bytes = 0;
    bool declared_size_available = false;
    bool declared_size_fits_rom = false;
    bool checksum_matches_declared_size = false;
    CartridgeHeaderRegion region = CartridgeHeaderRegion::Unknown;
};

struct CartridgeImageHeuristics {
    CartridgeImageModelHint model = CartridgeImageModelHint::Unknown;
    CartridgeHeaderRegion region = CartridgeHeaderRegion::Unknown;
    bool header_based = false;
    bool bios_like = false;
    std::string reason;
};

struct CartridgeHeaderBuildOptions {
    bool preserve_existing = true;
    CartridgeHeaderRegion region = CartridgeHeaderRegion::SmsExport;
    std::string product_code = "00000";
    u8 version = 0;
};

CartridgeHeaderInfo analyze_cartridge_header(std::span<const u8> rom);
CartridgeImageHeuristics analyze_cartridge_image(std::span<const u8> rom, std::string_view filename_or_extension = {});
const char* cartridge_region_name(CartridgeHeaderRegion region);
CartridgePlatform cartridge_header_platform(const CartridgeHeaderInfo& header);
const char* cartridge_platform_name(CartridgePlatform platform);
const char* cartridge_model_hint_name(CartridgeImageModelHint model);
const char* cartridge_size_code_name(u8 region_size);
std::size_t cartridge_declared_size_bytes(u8 region_size);
bool cartridge_header_is_game_gear(const CartridgeHeaderInfo& header);
u16 calculate_cartridge_checksum(std::span<const u8> rom, std::size_t checksum_size, std::size_t header_offset);
std::size_t cartridge_standard_header_offset(std::size_t rom_size);
u8 cartridge_size_code_for_bytes(std::size_t rom_size);
CartridgeHeaderInfo rebuild_cartridge_header(std::span<u8> rom, const CartridgeHeaderBuildOptions& options = {});

} // namespace sgrecomp
