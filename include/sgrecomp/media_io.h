#pragma once

#include "sgrecomp/types.h"

#include <filesystem>
#include <span>

namespace sgrecomp {

void write_bmp_image(const std::filesystem::path& path, std::span<const u32> pixels, int width, int height);
void write_pcm16_stereo_wav(const std::filesystem::path& path,
                            std::span<const s16> interleaved_samples,
                            u32 sample_rate);

} // namespace sgrecomp
