#pragma once

#include "sgrecomp/types.h"

#include <cstddef>
#include <span>
#include <string>

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
    bool checksum_matches_declared_size = false;
    CartridgeHeaderRegion region = CartridgeHeaderRegion::Unknown;
};

CartridgeHeaderInfo analyze_cartridge_header(std::span<const u8> rom);
const char* cartridge_region_name(CartridgeHeaderRegion region);
CartridgePlatform cartridge_header_platform(const CartridgeHeaderInfo& header);
const char* cartridge_platform_name(CartridgePlatform platform);
const char* cartridge_size_code_name(u8 region_size);
std::size_t cartridge_declared_size_bytes(u8 region_size);
bool cartridge_header_is_game_gear(const CartridgeHeaderInfo& header);

} // namespace sgrecomp
