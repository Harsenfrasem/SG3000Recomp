#include "sgrecomp/media_io.h"

#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace sgrecomp {
namespace {

void append_u16(std::vector<u8>& bytes, u16 value) {
    bytes.push_back(static_cast<u8>(value));
    bytes.push_back(static_cast<u8>(value >> 8));
}

void append_u32(std::vector<u8>& bytes, u32 value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<u8>(value >> shift));
    }
}

void write_bytes(const std::filesystem::path& path, std::span<const u8> bytes) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open media output file");
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("cannot write media output file");
    }
}

} // namespace

void write_bmp_image(const std::filesystem::path& path, std::span<const u32> pixels, int width, int height) {
    if (width <= 0 || height <= 0 || pixels.size() < static_cast<std::size_t>(width) * height) {
        throw std::invalid_argument("invalid BMP dimensions or pixel buffer");
    }
    const u64 row_size = (static_cast<u64>(width) * 3 + 3) & ~u64{3};
    const u64 image_size = row_size * static_cast<u64>(height);
    const u64 file_size = 54 + image_size;
    if (file_size > std::numeric_limits<u32>::max()) {
        throw std::invalid_argument("BMP image is too large");
    }

    std::vector<u8> bytes;
    bytes.reserve(static_cast<std::size_t>(file_size));
    bytes.push_back('B');
    bytes.push_back('M');
    append_u32(bytes, static_cast<u32>(file_size));
    append_u32(bytes, 0);
    append_u32(bytes, 54);
    append_u32(bytes, 40);
    append_u32(bytes, static_cast<u32>(width));
    append_u32(bytes, static_cast<u32>(height));
    append_u16(bytes, 1);
    append_u16(bytes, 24);
    append_u32(bytes, 0);
    append_u32(bytes, static_cast<u32>(image_size));
    append_u32(bytes, 2835);
    append_u32(bytes, 2835);
    append_u32(bytes, 0);
    append_u32(bytes, 0);

    for (int y = height - 1; y >= 0; --y) {
        const std::size_t row_start = bytes.size();
        for (int x = 0; x < width; ++x) {
            const u32 color = pixels[static_cast<std::size_t>(y) * width + x];
            bytes.push_back(static_cast<u8>(color));
            bytes.push_back(static_cast<u8>(color >> 8));
            bytes.push_back(static_cast<u8>(color >> 16));
        }
        while (bytes.size() - row_start < row_size) {
            bytes.push_back(0);
        }
    }
    write_bytes(path, bytes);
}

void write_pcm16_stereo_wav(const std::filesystem::path& path,
                            std::span<const s16> interleaved_samples,
                            u32 sample_rate) {
    if (sample_rate == 0 || sample_rate > std::numeric_limits<u32>::max() / 4 ||
        (interleaved_samples.size() & 1U) != 0) {
        throw std::invalid_argument("invalid stereo PCM audio");
    }
    const u64 data_size = interleaved_samples.size() * sizeof(s16);
    if (data_size > std::numeric_limits<u32>::max() - 36) {
        throw std::invalid_argument("WAV recording is too large");
    }

    std::vector<u8> bytes;
    bytes.reserve(static_cast<std::size_t>(44 + data_size));
    bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
    append_u32(bytes, static_cast<u32>(36 + data_size));
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    append_u32(bytes, 16);
    append_u16(bytes, 1);
    append_u16(bytes, 2);
    append_u32(bytes, sample_rate);
    append_u32(bytes, sample_rate * 4);
    append_u16(bytes, 4);
    append_u16(bytes, 16);
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    append_u32(bytes, static_cast<u32>(data_size));
    for (const s16 sample : interleaved_samples) {
        append_u16(bytes, static_cast<u16>(sample));
    }
    write_bytes(path, bytes);
}

} // namespace sgrecomp
