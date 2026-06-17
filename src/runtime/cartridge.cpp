#include "sgrecomp/cartridge.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>

namespace sgrecomp {
namespace {

u16 read_le16_at(std::span<const u8> bytes, std::size_t offset) {
    if (offset + 1 >= bytes.size()) {
        return 0;
    }
    return make_u16(bytes[offset], bytes[offset + 1]);
}

u16 diagnostic_rom_checksum(std::span<const u8> rom, std::size_t header_offset) {
    u32 sum = 0;
    for (std::size_t i = 0; i < rom.size(); ++i) {
        if (i >= header_offset && i < header_offset + 16) {
            continue;
        }
        sum += rom[i];
    }
    return static_cast<u16>(sum & 0xFFFF);
}

u16 checksum_range(std::span<const u8> rom, std::size_t size, std::size_t header_offset) {
    const std::size_t limit = std::min<std::size_t>(rom.size(), size);
    u32 sum = 0;
    for (std::size_t i = 0; i < limit; ++i) {
        if (i >= header_offset && i < header_offset + 16) {
            continue;
        }
        sum += rom[i];
    }
    return static_cast<u16>(sum & 0xFFFF);
}

std::string decode_product_code(std::span<const u8> rom, std::size_t header_offset) {
    if (header_offset + 14 >= rom.size()) {
        return "unknown";
    }
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0')
        << std::setw(2) << static_cast<int>(rom[header_offset + 12])
        << std::setw(2) << static_cast<int>(rom[header_offset + 13])
        << std::setw(1) << static_cast<int>((rom[header_offset + 14] >> 4) & 0x0F);
    return out.str();
}

CartridgeHeaderRegion decode_region(u8 region_size) {
    switch ((region_size >> 4) & 0x0F) {
    case 3: return CartridgeHeaderRegion::SmsJapan;
    case 4: return CartridgeHeaderRegion::SmsExport;
    case 5: return CartridgeHeaderRegion::GameGearJapan;
    case 6: return CartridgeHeaderRegion::GameGearExport;
    case 7: return CartridgeHeaderRegion::GameGearInternational;
    default: return CartridgeHeaderRegion::Unknown;
    }
}

} // namespace

CartridgeHeaderInfo analyze_cartridge_header(std::span<const u8> rom) {
    static constexpr std::array<std::size_t, 4> kHeaderOffsets{0x1FF0, 0x3FF0, 0x7FF0, 0xBFF0};
    static constexpr std::array<u8, 8> kMagic{'T', 'M', 'R', ' ', 'S', 'E', 'G', 'A'};

    for (const std::size_t offset : kHeaderOffsets) {
        if (offset + kMagic.size() > rom.size()) {
            continue;
        }
        if (!std::equal(kMagic.begin(), kMagic.end(), rom.begin() + static_cast<std::ptrdiff_t>(offset))) {
            continue;
        }

        CartridgeHeaderInfo info;
        info.found = true;
        info.offset = offset;
        info.stored_checksum = read_le16_at(rom, offset + 10);
        info.diagnostic_checksum = diagnostic_rom_checksum(rom, offset);
        info.product_code = decode_product_code(rom, offset);
        info.version = static_cast<u8>(rom[offset + 14] & 0x0F);
        info.region_size = rom[offset + 15];
        info.region = decode_region(info.region_size);
        info.declared_size_bytes = cartridge_declared_size_bytes(info.region_size);
        info.declared_size_available = info.declared_size_bytes != 0;
        if (info.declared_size_available) {
            info.declared_size_checksum = checksum_range(rom, info.declared_size_bytes, offset);
            info.checksum_matches_declared_size = info.declared_size_checksum == info.stored_checksum;
        }
        return info;
    }

    return {};
}

const char* cartridge_region_name(CartridgeHeaderRegion region) {
    switch (region) {
    case CartridgeHeaderRegion::SmsJapan: return "SMS Japan";
    case CartridgeHeaderRegion::SmsExport: return "SMS export";
    case CartridgeHeaderRegion::GameGearJapan: return "Game Gear Japan";
    case CartridgeHeaderRegion::GameGearExport: return "Game Gear export";
    case CartridgeHeaderRegion::GameGearInternational: return "Game Gear international";
    case CartridgeHeaderRegion::Unknown: return "unknown";
    }
    return "unknown";
}

CartridgePlatform cartridge_header_platform(const CartridgeHeaderInfo& header) {
    switch (header.region) {
    case CartridgeHeaderRegion::SmsJapan:
    case CartridgeHeaderRegion::SmsExport:
        return CartridgePlatform::MasterSystem;
    case CartridgeHeaderRegion::GameGearJapan:
    case CartridgeHeaderRegion::GameGearExport:
    case CartridgeHeaderRegion::GameGearInternational:
        return CartridgePlatform::GameGear;
    case CartridgeHeaderRegion::Unknown:
        return CartridgePlatform::Unknown;
    }
    return CartridgePlatform::Unknown;
}

const char* cartridge_platform_name(CartridgePlatform platform) {
    switch (platform) {
    case CartridgePlatform::MasterSystem: return "Master System";
    case CartridgePlatform::GameGear: return "Game Gear";
    case CartridgePlatform::Unknown: return "unknown";
    }
    return "unknown";
}

const char* cartridge_size_code_name(u8 region_size) {
    switch (region_size & 0x0F) {
    case 0xA: return "8 KiB";
    case 0xB: return "16 KiB";
    case 0xC: return "32 KiB";
    case 0xD: return "48 KiB";
    case 0xE: return "64 KiB";
    case 0xF: return "128 KiB";
    case 0x0: return "256 KiB";
    case 0x1: return "512 KiB";
    case 0x2: return "1 MiB";
    default: return "unknown";
    }
}

std::size_t cartridge_declared_size_bytes(u8 region_size) {
    switch (region_size & 0x0F) {
    case 0xA: return 8 * 1024;
    case 0xB: return 16 * 1024;
    case 0xC: return 32 * 1024;
    case 0xD: return 48 * 1024;
    case 0xE: return 64 * 1024;
    case 0xF: return 128 * 1024;
    case 0x0: return 256 * 1024;
    case 0x1: return 512 * 1024;
    case 0x2: return 1024 * 1024;
    default: return 0;
    }
}

bool cartridge_header_is_game_gear(const CartridgeHeaderInfo& header) {
    return cartridge_header_platform(header) == CartridgePlatform::GameGear;
}

} // namespace sgrecomp
