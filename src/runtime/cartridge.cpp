#include "sgrecomp/cartridge.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <stdexcept>
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

u8 encode_region(CartridgeHeaderRegion region) {
    switch (region) {
    case CartridgeHeaderRegion::SmsJapan: return 3;
    case CartridgeHeaderRegion::SmsExport: return 4;
    case CartridgeHeaderRegion::GameGearJapan: return 5;
    case CartridgeHeaderRegion::GameGearExport: return 6;
    case CartridgeHeaderRegion::GameGearInternational: return 7;
    case CartridgeHeaderRegion::Unknown: break;
    }
    throw std::invalid_argument("cannot encode unknown cartridge region");
}

u8 parse_hex_digit(char value) {
    if (value >= '0' && value <= '9') return static_cast<u8>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<u8>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<u8>(value - 'A' + 10);
    throw std::invalid_argument("product code must contain exactly five hexadecimal digits");
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
        info.declared_size_fits_rom = info.declared_size_available && info.declared_size_bytes <= rom.size();
        if (info.declared_size_fits_rom) {
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

u16 calculate_cartridge_checksum(std::span<const u8> rom, std::size_t checksum_size, std::size_t header_offset) {
    if (checksum_size > rom.size()) {
        throw std::invalid_argument("declared cartridge size exceeds ROM image");
    }
    if (header_offset + 16 > checksum_size) {
        throw std::invalid_argument("cartridge header lies outside checksum range");
    }
    return checksum_range(rom, checksum_size, header_offset);
}

std::size_t cartridge_standard_header_offset(std::size_t rom_size) {
    if (rom_size == 8 * 1024) return 0x1FF0;
    if (rom_size == 16 * 1024) return 0x3FF0;
    if (rom_size == 48 * 1024) return 0xBFF0;
    if (rom_size >= 32 * 1024) return 0x7FF0;
    throw std::invalid_argument("ROM size has no standard TMR SEGA header location");
}

u8 cartridge_size_code_for_bytes(std::size_t rom_size) {
    switch (rom_size) {
    case 8 * 1024: return 0xA;
    case 16 * 1024: return 0xB;
    case 32 * 1024: return 0xC;
    case 48 * 1024: return 0xD;
    case 64 * 1024: return 0xE;
    case 128 * 1024: return 0xF;
    case 256 * 1024: return 0x0;
    case 512 * 1024: return 0x1;
    case 1024 * 1024: return 0x2;
    default: throw std::invalid_argument("ROM size cannot be represented by the cartridge header");
    }
}

CartridgeHeaderInfo rebuild_cartridge_header(std::span<u8> rom, const CartridgeHeaderBuildOptions& options) {
    CartridgeHeaderInfo existing = analyze_cartridge_header(rom);
    std::size_t offset = existing.offset;

    if (options.preserve_existing) {
        if (!existing.found) {
            throw std::invalid_argument("cannot preserve cartridge header because no TMR SEGA header was found");
        }
        if (!existing.declared_size_available) {
            throw std::invalid_argument("existing cartridge header has an unknown size code");
        }
        if (!existing.declared_size_fits_rom) {
            throw std::invalid_argument("existing cartridge header declares a size larger than the ROM image");
        }
    } else {
        if (options.product_code.size() != 5) {
            throw std::invalid_argument("product code must contain exactly five hexadecimal digits");
        }
        if (options.version > 0x0F) {
            throw std::invalid_argument("cartridge version must fit in four bits");
        }
        offset = cartridge_standard_header_offset(rom.size());
        if (offset + 16 > rom.size()) {
            throw std::invalid_argument("ROM image is too small for its standard cartridge header");
        }
        static constexpr std::array<u8, 8> kMagic{'T', 'M', 'R', ' ', 'S', 'E', 'G', 'A'};
        std::copy(kMagic.begin(), kMagic.end(), rom.begin() + static_cast<std::ptrdiff_t>(offset));
        rom[offset + 8] = 0;
        rom[offset + 9] = 0;
        rom[offset + 12] = static_cast<u8>((parse_hex_digit(options.product_code[0]) << 4)
            | parse_hex_digit(options.product_code[1]));
        rom[offset + 13] = static_cast<u8>((parse_hex_digit(options.product_code[2]) << 4)
            | parse_hex_digit(options.product_code[3]));
        rom[offset + 14] = static_cast<u8>((parse_hex_digit(options.product_code[4]) << 4) | options.version);
        rom[offset + 15] = static_cast<u8>((encode_region(options.region) << 4)
            | cartridge_size_code_for_bytes(rom.size()));
    }

    const CartridgeHeaderInfo header_before_checksum = analyze_cartridge_header(rom);
    const u16 checksum = calculate_cartridge_checksum(rom, header_before_checksum.declared_size_bytes, offset);
    rom[offset + 10] = static_cast<u8>(checksum & 0xFF);
    rom[offset + 11] = static_cast<u8>(checksum >> 8);
    return analyze_cartridge_header(rom);
}

} // namespace sgrecomp
