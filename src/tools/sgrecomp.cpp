#include "sgrecomp/bus.h"
#include "sgrecomp/cartridge.h"
#include "sgrecomp/game_profile.h"
#include "sgrecomp/host_runtime.h"
#include "sgrecomp/joypad.h"
#include "sgrecomp/input_script.h"
#include "sgrecomp/psg.h"
#include "sgrecomp/save_state.h"
#include "sgrecomp/vdp.h"
#include "sgrecomp/ym2413.h"
#include "sgrecomp/z80.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <bitset>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace sgrecomp;

struct AddressRange {
    u32 first = 0;
    u32 last = 0;
};

enum class HeaderWriteMode {
    None,
    Rebuild,
    Generate,
};

struct Options {
    std::filesystem::path input;
    std::filesystem::path input_script;
    std::filesystem::path dump_frame_log;
    std::filesystem::path header_output;
    std::filesystem::path bios;
    std::filesystem::path dump_frame;
    std::filesystem::path dump_frame_bmp;
    std::filesystem::path dump_audio;
    std::filesystem::path dump_vgm;
    std::filesystem::path dump_fm_log;
    std::filesystem::path dump_io_log;
    std::filesystem::path dump_memory_log;
    std::filesystem::path dump_vdp_log;
    std::filesystem::path dump_vram;
    std::filesystem::path dump_cram;
    std::filesystem::path dump_tilemap;
    std::filesystem::path dump_sprites;
    std::filesystem::path dump_sram;
    std::filesystem::path dump_coverage;
    std::filesystem::path dump_analysis;
    std::filesystem::path load_sram;
    std::filesystem::path save_sram;
    std::filesystem::path load_state;
    std::filesystem::path save_state;
    std::filesystem::path output = "recompiled_rom.cpp";
    ConsoleModel model = ConsoleModel::SMS;
    CartridgeMapper mapper = CartridgeMapper::Auto;
    EnhancementConfig enhancements;
    HostVideoStandard video_standard = HostVideoStandard::Ntsc;
    std::vector<AddressRange> memory_filters;
    std::vector<AddressRange> vdp_filters;
    std::vector<u8> io_port_filters;
    bool disassemble_only = false;
    bool run_smoke = false;
    bool run_host = false;
    bool trace = false;
    bool force_state = false;
    std::size_t max_steps = 200000;
    std::size_t host_frames = 1;
    std::optional<std::size_t> max_static_bytes;
    u32 audio_sample_rate = 44100;
    HeaderWriteMode header_write_mode = HeaderWriteMode::None;
    CartridgeHeaderRegion header_region = CartridgeHeaderRegion::SmsExport;
    std::string header_product_code = "00000";
    u8 header_version = 0;
};

std::string trim_ascii(std::string value) {
    const auto first =
        std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last =
        std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (first >= last) {
        return {};
    }
    return {first, last};
}

std::string lower_ascii(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string strip_quotes(std::string value) {
    value = trim_ascii(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

u32 parse_number(std::string text) {
    text = strip_quotes(std::move(text));
    std::size_t consumed = 0;
    const unsigned long value = std::stoul(text, &consumed, 0);
    if (consumed != text.size()) {
        throw std::runtime_error("invalid number: " + text);
    }
    return static_cast<u32>(value);
}

AddressRange parse_range(const std::string& text) {
    const auto dash = text.find('-');
    if (dash == std::string::npos) {
        const u32 value = parse_number(text);
        return {value, value};
    }
    AddressRange range{parse_number(text.substr(0, dash)), parse_number(text.substr(dash + 1))};
    if (range.first > range.last) {
        std::swap(range.first, range.last);
    }
    return range;
}

CartridgeMapper parse_mapper(std::string text) {
    return cartridge_mapper_from_name(lower_ascii(strip_quotes(std::move(text))));
}

HostVideoStandard parse_video_standard(std::string text) {
    text = lower_ascii(strip_quotes(std::move(text)));
    if (text == "ntsc") {
        return HostVideoStandard::Ntsc;
    }
    if (text == "pal") {
        return HostVideoStandard::Pal;
    }
    throw std::runtime_error("unknown video standard: " + text);
}

CartridgeHeaderRegion parse_header_region(std::string text) {
    text = lower_ascii(strip_quotes(std::move(text)));
    if (text == "sms-japan")
        return CartridgeHeaderRegion::SmsJapan;
    if (text == "sms-export")
        return CartridgeHeaderRegion::SmsExport;
    if (text == "gg-japan")
        return CartridgeHeaderRegion::GameGearJapan;
    if (text == "gg-export")
        return CartridgeHeaderRegion::GameGearExport;
    if (text == "gg-international")
        return CartridgeHeaderRegion::GameGearInternational;
    throw std::runtime_error("unknown cartridge header region: " + text);
}

ConsoleModel parse_console_model(std::string text) {
    text = lower_ascii(strip_quotes(std::move(text)));
    if (text == "sms") {
        return ConsoleModel::SMS;
    }
    if (text == "sg3000" || text == "sg-3000") {
        return ConsoleModel::SG3000;
    }
    throw std::runtime_error("unknown model: " + text);
}

bool parse_config_bool(std::string text) {
    text = lower_ascii(strip_quotes(std::move(text)));
    if (text == "true" || text == "yes" || text == "on" || text == "1") {
        return true;
    }
    if (text == "false" || text == "no" || text == "off" || text == "0") {
        return false;
    }
    throw std::runtime_error("invalid boolean in config: " + text);
}

bool range_matches(const std::vector<AddressRange>& filters, u32 value) {
    if (filters.empty()) {
        return true;
    }
    for (const auto& filter : filters) {
        if (value >= filter.first && value <= filter.last) {
            return true;
        }
    }
    return false;
}

bool port_matches(const std::vector<u8>& filters, u8 port) {
    if (filters.empty()) {
        return true;
    }
    return std::find(filters.begin(), filters.end(), port) != filters.end();
}

std::vector<u8> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open input file");
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string read_text_file(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    return {bytes.begin(), bytes.end()};
}

void write_binary_file(const std::filesystem::path& path, std::span<const u8> bytes) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open binary output file");
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("cannot write binary output file");
    }
}

std::vector<u8> normalize_rom_payload(std::vector<u8> rom) {
    if (rom.size() > 512 && (rom.size() % 0x4000) == 512) {
        rom.erase(rom.begin(), rom.begin() + 512);
    }
    return rom;
}

void apply_config_file(Options& opts, const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open config file");
    }

    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim_ascii(std::move(line));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = lower_ascii(trim_ascii(line.substr(1, line.size() - 2)));
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("invalid config line: " + line);
        }
        const std::string key = lower_ascii(trim_ascii(line.substr(0, equals)));
        const std::string value = trim_ascii(line.substr(equals + 1));

        if (section == "target") {
            if (key == "model") {
                opts.model = parse_console_model(value);
            } else if (key == "mapper") {
                opts.mapper = parse_mapper(value);
            } else if (key == "name" || key == "entry") {
                continue;
            } else {
                throw std::runtime_error("unknown target config key: " + key);
            }
        } else if (section == "recompiler") {
            if (key == "max_static_bytes") {
                opts.max_static_bytes = parse_number(value);
            } else if (key == "fallback" || key == "emit_disassembly_comments") {
                (void)parse_config_bool(value);
            } else {
                throw std::runtime_error("unknown recompiler config key: " + key);
            }
        } else if (section == "runtime") {
            if (key == "region" || key == "video_standard") {
                opts.video_standard = parse_video_standard(value);
            } else if (key == "audio_sample_rate") {
                opts.audio_sample_rate =
                    static_cast<u32>(std::clamp(static_cast<int>(parse_number(value)), 8000, 96000));
            } else if (key == "enable_fm") {
                opts.enhancements.enable_fm = parse_config_bool(value);
            } else if (key == "disable_sprite_limit") {
                opts.enhancements.disable_sprite_limit = parse_config_bool(value);
                if (opts.enhancements.disable_sprite_limit) {
                    opts.enhancements.mode = RuntimeMode::Enhanced;
                }
            } else if (key == "reduce_flicker") {
                opts.enhancements.reduce_flicker = parse_config_bool(value);
                if (opts.enhancements.reduce_flicker) {
                    opts.enhancements.mode = RuntimeMode::Enhanced;
                }
            } else {
                throw std::runtime_error("unknown runtime config key: " + key);
            }
        } else {
            throw std::runtime_error("config key outside supported section: " + key);
        }
    }
}

void print_usage() {
    std::cout << "usage: sgrecomp <rom.sms|rom.sg> [--config config.toml] [-o generated.cpp] [--model sms|sg3000] "
                 "[--mapper auto|plain|smapper|cmapper|kmapper|k8k] [--disasm] [--bios bios.sms]\n"
              << "       sgrecomp <rom.sms|rom.sg> --run-smoke [--steps n] [--trace] [--bios bios.sms] [--mapper "
                 "auto|plain|smapper|cmapper|kmapper|k8k]\n"
              << "                [--dump-frame frame.ppm] [--dump-frame-bmp frame.bmp]\n"
              << "                [--dump-audio audio.wav] [--dump-vgm audio.vgm] [--dump-fm-log fm.csv]\n"
              << "                [--dump-io-log io.csv] [--dump-memory-log memory.csv] [--dump-vdp-log vdp.csv]\n"
              << "                [--io-port port] [--watch addr|start-end] [--watch-vdp addr|start-end]\n"
              << "                [--dump-vram vram.bin] [--dump-cram cram.bin]\n"
              << "                [--dump-tilemap tilemap.csv] [--dump-sprites sprites.csv]\n"
              << "                [--load-sram save.sav] [--save-sram save.sav] [--dump-sram sram.bin]\n"
              << "                [--load-state state.sgstate] [--save-state state.sgstate] [--force-state]\n"
              << "                [--dump-coverage pcs.csv] [--disable-sprite-limit] [--reduce-flicker] [--enable-fm]\n"
              << "       sgrecomp <rom.sms|rom.sg> --run-host [--frames n] [--input-script input.csv] [--bios "
                 "bios.sms] [--video-standard ntsc|pal] [--audio-sample-rate hz]\n"
              << "                [--dump-frame frame.ppm] [--dump-frame-bmp frame.bmp] [--dump-audio audio.wav] "
                 "[--dump-frame-log frames.csv]\n"
              << "                [--dump-vgm audio.vgm] [--dump-fm-log fm.csv] [--dump-io-log io.csv]\n"
              << "                [--dump-memory-log memory.csv] [--dump-vdp-log vdp.csv]\n"
              << "                [--dump-vram vram.bin] [--dump-cram cram.bin] [--dump-tilemap tilemap.csv] "
                 "[--dump-sprites sprites.csv]\n"
              << "       sgrecomp <rom.sms|rom.sg> [-o generated.cpp] [--dump-analysis analysis.txt]\n"
              << "       sgrecomp <rom.sms> --rebuild-header output.sms\n"
              << "       sgrecomp <rom.sms> --generate-header output.sms [--header-region "
                 "sms-japan|sms-export|gg-japan|gg-export|gg-international]\n"
              << "                [--product-code 00000] [--header-version 0]\n";
}

Options parse_args(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            std::exit(0);
        }
        if (arg == "-o" && i + 1 < argc) {
            opts.output = argv[++i];
            continue;
        }
        if (arg == "--config" && i + 1 < argc) {
            apply_config_file(opts, argv[++i]);
            continue;
        }
        if (arg == "--bios" && i + 1 < argc) {
            opts.bios = argv[++i];
            continue;
        }
        if ((arg == "--rebuild-header" || arg == "--generate-header") && i + 1 < argc) {
            if (opts.header_write_mode != HeaderWriteMode::None) {
                throw std::runtime_error("choose only one cartridge header write mode");
            }
            opts.header_write_mode = arg == "--generate-header" ? HeaderWriteMode::Generate : HeaderWriteMode::Rebuild;
            opts.header_output = argv[++i];
            continue;
        }
        if (arg == "--header-region" && i + 1 < argc) {
            opts.header_region = parse_header_region(argv[++i]);
            continue;
        }
        if (arg == "--product-code" && i + 1 < argc) {
            opts.header_product_code = argv[++i];
            continue;
        }
        if (arg == "--header-version" && i + 1 < argc) {
            const unsigned version = static_cast<unsigned>(std::stoul(argv[++i], nullptr, 0));
            if (version > 15) {
                throw std::runtime_error("header version must be between 0 and 15");
            }
            opts.header_version = static_cast<u8>(version);
            continue;
        }
        if (arg == "--mapper" && i + 1 < argc) {
            opts.mapper = parse_mapper(argv[++i]);
            continue;
        }
        if (arg == "--dump-frame" && i + 1 < argc) {
            opts.dump_frame = argv[++i];
            continue;
        }
        if (arg == "--dump-frame-bmp" && i + 1 < argc) {
            opts.dump_frame_bmp = argv[++i];
            continue;
        }
        if (arg == "--dump-audio" && i + 1 < argc) {
            opts.dump_audio = argv[++i];
            continue;
        }
        if (arg == "--dump-vgm" && i + 1 < argc) {
            opts.dump_vgm = argv[++i];
            continue;
        }
        if (arg == "--dump-fm-log" && i + 1 < argc) {
            opts.dump_fm_log = argv[++i];
            continue;
        }
        if (arg == "--dump-io-log" && i + 1 < argc) {
            opts.dump_io_log = argv[++i];
            continue;
        }
        if (arg == "--dump-memory-log" && i + 1 < argc) {
            opts.dump_memory_log = argv[++i];
            continue;
        }
        if (arg == "--dump-vdp-log" && i + 1 < argc) {
            opts.dump_vdp_log = argv[++i];
            continue;
        }
        if (arg == "--io-port" && i + 1 < argc) {
            opts.io_port_filters.push_back(static_cast<u8>(parse_number(argv[++i]) & 0xFF));
            continue;
        }
        if (arg == "--watch" && i + 1 < argc) {
            opts.memory_filters.push_back(parse_range(argv[++i]));
            continue;
        }
        if (arg == "--watch-vdp" && i + 1 < argc) {
            opts.vdp_filters.push_back(parse_range(argv[++i]));
            continue;
        }
        if (arg == "--dump-vram" && i + 1 < argc) {
            opts.dump_vram = argv[++i];
            continue;
        }
        if (arg == "--dump-cram" && i + 1 < argc) {
            opts.dump_cram = argv[++i];
            continue;
        }
        if (arg == "--dump-tilemap" && i + 1 < argc) {
            opts.dump_tilemap = argv[++i];
            continue;
        }
        if (arg == "--dump-sprites" && i + 1 < argc) {
            opts.dump_sprites = argv[++i];
            continue;
        }
        if (arg == "--dump-sram" && i + 1 < argc) {
            opts.dump_sram = argv[++i];
            continue;
        }
        if (arg == "--dump-coverage" && i + 1 < argc) {
            opts.dump_coverage = argv[++i];
            continue;
        }
        if (arg == "--dump-analysis" && i + 1 < argc) {
            opts.dump_analysis = argv[++i];
            continue;
        }
        if (arg == "--load-sram" && i + 1 < argc) {
            opts.load_sram = argv[++i];
            continue;
        }
        if (arg == "--save-sram" && i + 1 < argc) {
            opts.save_sram = argv[++i];
            continue;
        }
        if (arg == "--load-state" && i + 1 < argc) {
            opts.load_state = argv[++i];
            continue;
        }
        if (arg == "--save-state" && i + 1 < argc) {
            opts.save_state = argv[++i];
            continue;
        }
        if (arg == "--force-state") {
            opts.force_state = true;
            continue;
        }
        if (arg == "--model" && i + 1 < argc) {
            opts.model = parse_console_model(argv[++i]);
            continue;
        }
        if (arg == "--disasm") {
            opts.disassemble_only = true;
            continue;
        }
        if (arg == "--run-smoke") {
            opts.run_smoke = true;
            continue;
        }
        if (arg == "--run-host") {
            opts.run_host = true;
            continue;
        }
        if (arg == "--video-standard" && i + 1 < argc) {
            opts.video_standard = parse_video_standard(argv[++i]);
            continue;
        }
        if (arg == "--frames" && i + 1 < argc) {
            opts.host_frames = static_cast<std::size_t>(std::stoull(argv[++i]));
            continue;
        }
        if (arg == "--input-script" && i + 1 < argc) {
            opts.input_script = argv[++i];
            continue;
        }
        if (arg == "--dump-frame-log" && i + 1 < argc) {
            opts.dump_frame_log = argv[++i];
            continue;
        }
        if (arg == "--audio-sample-rate" && i + 1 < argc) {
            opts.audio_sample_rate = static_cast<u32>(std::clamp(std::stoi(argv[++i]), 8000, 96000));
            continue;
        }
        if (arg == "--trace") {
            opts.trace = true;
            continue;
        }
        if (arg == "--disable-sprite-limit") {
            opts.enhancements.mode = RuntimeMode::Enhanced;
            opts.enhancements.disable_sprite_limit = true;
            continue;
        }
        if (arg == "--reduce-flicker") {
            opts.enhancements.mode = RuntimeMode::Enhanced;
            opts.enhancements.reduce_flicker = true;
            continue;
        }
        if (arg == "--enable-fm") {
            opts.enhancements.enable_fm = true;
            continue;
        }
        if (arg == "--steps" && i + 1 < argc) {
            opts.max_steps = static_cast<std::size_t>(std::stoull(argv[++i]));
            continue;
        }
        if (opts.input.empty()) {
            opts.input = arg;
            continue;
        }
        throw std::runtime_error("unexpected argument: " + arg);
    }
    if (opts.input.empty()) {
        throw std::runtime_error("missing input ROM");
    }
    return opts;
}

std::array<u8, 0x10000> image_for_decode(ConsoleModel model,
                                         CartridgeMapper mapper,
                                         const std::vector<u8>& rom,
                                         const std::vector<u8>* bios = nullptr) {
    Vdp vdp;
    Psg psg;
    Ym2413 ym2413;
    Joypad joypad;
    Bus bus(model, vdp, psg, ym2413, joypad);
    bus.set_mapper(mapper);
    if (bios != nullptr) {
        bus.load_bios(*bios);
    }
    bus.load_rom(rom);
    return bus.debug_memory();
}

void disassemble(const std::array<u8, 0x10000>& image, std::size_t limit) {
    u16 pc = 0;
    while (pc < limit && pc < 0xC000) {
        const auto insn = decode_z80(image, pc);
        std::cout << std::hex << std::setw(4) << std::setfill('0') << insn.pc << "  " << std::setw(2)
                  << static_cast<int>(insn.opcode) << "  " << insn.mnemonic << "\n";
        pc = static_cast<u16>(pc + insn.size);
    }
}

void write_frame_ppm(const std::filesystem::path& path, const std::array<u32, Vdp::width * Vdp::height>& framebuffer) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open frame output file");
    }

    out << "P6\n" << Vdp::width << " " << Vdp::height << "\n255\n";
    for (const u32 pixel : framebuffer) {
        const char rgb[3] = {
            static_cast<char>((pixel >> 16) & 0xFF),
            static_cast<char>((pixel >> 8) & 0xFF),
            static_cast<char>(pixel & 0xFF),
        };
        out.write(rgb, sizeof(rgb));
    }
}

void write_u16_le(std::ostream& out, u16 value) {
    const char bytes[2] = {
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
    };
    out.write(bytes, sizeof(bytes));
}

void write_u32_le(std::ostream& out, u32 value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF),
        static_cast<char>((value >> 24) & 0xFF),
    };
    out.write(bytes, sizeof(bytes));
}

void write_frame_bmp(const std::filesystem::path& path, const std::array<u32, Vdp::width * Vdp::height>& framebuffer) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open BMP frame output file");
    }

    constexpr u32 width = Vdp::width;
    constexpr u32 height = Vdp::height;
    constexpr u32 file_header_size = 14;
    constexpr u32 dib_header_size = 40;
    constexpr u32 data_offset = file_header_size + dib_header_size;
    constexpr u32 row_stride = ((width * 3 + 3) / 4) * 4;
    constexpr u32 image_size = row_stride * height;
    constexpr u32 file_size = data_offset + image_size;

    out.put('B');
    out.put('M');
    write_u32_le(out, file_size);
    write_u16_le(out, 0);
    write_u16_le(out, 0);
    write_u32_le(out, data_offset);

    write_u32_le(out, dib_header_size);
    write_u32_le(out, width);
    write_u32_le(out, height);
    write_u16_le(out, 1);
    write_u16_le(out, 24);
    write_u32_le(out, 0);
    write_u32_le(out, image_size);
    write_u32_le(out, 2835);
    write_u32_le(out, 2835);
    write_u32_le(out, 0);
    write_u32_le(out, 0);

    std::array<char, row_stride> row{};
    for (int y = static_cast<int>(height) - 1; y >= 0; --y) {
        row.fill(0);
        for (u32 x = 0; x < width; ++x) {
            const u32 pixel = framebuffer[static_cast<std::size_t>(y) * width + x];
            row[x * 3 + 0] = static_cast<char>(pixel & 0xFF);
            row[x * 3 + 1] = static_cast<char>((pixel >> 8) & 0xFF);
            row[x * 3 + 2] = static_cast<char>((pixel >> 16) & 0xFF);
        }
        out.write(row.data(), row.size());
    }
}

void write_audio_wav(const std::filesystem::path& path, const std::vector<s16>& stereo_samples, u32 sample_rate) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open audio output file");
    }

    const u16 channels = 2;
    const u16 bits_per_sample = 16;
    const u16 block_align = static_cast<u16>(channels * bits_per_sample / 8);
    const u32 byte_rate = sample_rate * block_align;
    const u32 data_size = static_cast<u32>(stereo_samples.size() * sizeof(s16));

    out.write("RIFF", 4);
    write_u32_le(out, 36 + data_size);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    write_u32_le(out, 16);
    write_u16_le(out, 1);
    write_u16_le(out, channels);
    write_u32_le(out, sample_rate);
    write_u32_le(out, byte_rate);
    write_u16_le(out, block_align);
    write_u16_le(out, bits_per_sample);
    out.write("data", 4);
    write_u32_le(out, data_size);
    out.write(reinterpret_cast<const char*>(stereo_samples.data()), static_cast<std::streamsize>(data_size));
}

void put_u32_le(std::vector<u8>& bytes, std::size_t offset, u32 value) {
    bytes[offset + 0] = static_cast<u8>(value & 0xFF);
    bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xFF);
    bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xFF);
    bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xFF);
}

void append_u16_le(std::vector<u8>& bytes, u16 value) {
    bytes.push_back(static_cast<u8>(value & 0xFF));
    bytes.push_back(static_cast<u8>((value >> 8) & 0xFF));
}

void append_vgm_wait(std::vector<u8>& commands, u32 samples) {
    while (samples > 0) {
        const u16 chunk = static_cast<u16>(std::min<u32>(samples, 0xFFFF));
        commands.push_back(0x61);
        append_u16_le(commands, chunk);
        samples -= chunk;
    }
}

void write_psg_vgm(const std::filesystem::path& path, const std::vector<PsgWrite>& writes, u64 final_cycle) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    constexpr u32 psg_clock = 3579545;
    constexpr u32 sample_rate = 44100;
    std::vector<u8> commands;
    u64 last_cycle = 0;
    u64 emitted_samples = 0;

    for (const auto& write : writes) {
        const u64 target_samples = (write.cycle * sample_rate) / psg_clock;
        const u32 wait = static_cast<u32>(target_samples > emitted_samples ? target_samples - emitted_samples : 0);
        append_vgm_wait(commands, wait);
        emitted_samples += wait;
        commands.push_back(0x50);
        commands.push_back(write.value);
        last_cycle = write.cycle;
    }

    const u64 end_cycle = std::max(final_cycle, last_cycle);
    const u64 total_samples = (end_cycle * sample_rate) / psg_clock;
    if (total_samples > emitted_samples) {
        append_vgm_wait(commands, static_cast<u32>(std::min<u64>(total_samples - emitted_samples, 0xFFFFFFFFULL)));
        emitted_samples = total_samples;
    }
    commands.push_back(0x66);

    std::vector<u8> file(0x100, 0);
    file[0] = 'V';
    file[1] = 'g';
    file[2] = 'm';
    file[3] = ' ';
    put_u32_le(file, 0x08, 0x00000150);
    put_u32_le(file, 0x0C, psg_clock);
    put_u32_le(file, 0x18, static_cast<u32>(std::min<u64>(emitted_samples, 0xFFFFFFFFULL)));
    put_u32_le(file, 0x34, 0x000000CC);
    file.insert(file.end(), commands.begin(), commands.end());
    put_u32_le(file, 0x04, static_cast<u32>(file.size() - 4));

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open VGM output file");
    }
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
}

void write_fm_log_csv(const std::filesystem::path& path, const std::vector<Ym2413Write>& writes) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open FM log output file");
    }

    out << "cycle,port,value\n";
    for (const auto& write : writes) {
        out << write.cycle << ",0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(write.port)
            << ",0x" << std::setw(2) << static_cast<int>(write.value) << std::dec << std::setfill(' ') << "\n";
    }
}

void write_io_log_csv(const std::filesystem::path& path,
                      const std::vector<BusIoAccess>& accesses,
                      const std::vector<u8>& port_filters) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open I/O log output file");
    }

    out << "cycle,direction,port,value\n";
    for (const auto& access : accesses) {
        if (!port_matches(port_filters, access.port)) {
            continue;
        }
        out << access.cycle << "," << (access.write ? "write" : "read") << ",0x" << std::hex << std::setw(2)
            << std::setfill('0') << static_cast<int>(access.port) << ",0x" << std::setw(2)
            << static_cast<int>(access.value) << std::dec << std::setfill(' ') << "\n";
    }
}

const char* memory_kind_name(BusMemoryAccessKind kind) {
    switch (kind) {
    case BusMemoryAccessKind::Ram:
        return "ram";
    case BusMemoryAccessKind::CartridgeRam:
        return "cartridge_ram";
    case BusMemoryAccessKind::Mapper:
        return "mapper";
    }
    return "unknown";
}

void write_memory_log_csv(const std::filesystem::path& path,
                          const std::vector<BusMemoryAccess>& accesses,
                          const std::vector<AddressRange>& filters) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open memory log output file");
    }

    out << "cycle,kind,address,physical,value\n";
    for (const auto& access : accesses) {
        if (!range_matches(filters, access.address) && !range_matches(filters, access.physical)) {
            continue;
        }
        out << access.cycle << "," << memory_kind_name(access.kind) << ",0x" << std::hex << std::setw(4)
            << std::setfill('0') << access.address << ",0x" << std::setw(5) << access.physical << ",0x" << std::setw(2)
            << static_cast<int>(access.value) << std::dec << std::setfill(' ') << "\n";
    }
}

const char* vdp_kind_name(VdpAccessKind kind) {
    switch (kind) {
    case VdpAccessKind::Vram:
        return "vram";
    case VdpAccessKind::Cram:
        return "cram";
    case VdpAccessKind::Register:
        return "register";
    }
    return "unknown";
}

void write_vdp_log_csv(const std::filesystem::path& path,
                       const std::vector<VdpAccess>& accesses,
                       const std::vector<AddressRange>& filters) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open VDP log output file");
    }

    out << "cycle,kind,address,value\n";
    for (const auto& access : accesses) {
        if (!range_matches(filters, access.address)) {
            continue;
        }
        out << access.cycle << "," << vdp_kind_name(access.kind) << ",0x" << std::hex << std::setw(4)
            << std::setfill('0') << access.address << ",0x" << std::setw(2) << static_cast<int>(access.value)
            << std::dec << std::setfill(' ') << "\n";
    }
}

void write_tilemap_csv(const std::filesystem::path& path, const std::vector<VdpTileEntry>& entries) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open tilemap output file");
    }

    out << "x,y,address,tile,palette,flip_x,flip_y,priority\n";
    for (const auto& entry : entries) {
        out << static_cast<int>(entry.x) << "," << static_cast<int>(entry.y) << ",0x" << std::hex << std::setw(4)
            << std::setfill('0') << entry.address << ",0x" << std::setw(3) << entry.tile << std::dec << ","
            << (entry.palette1 ? 1 : 0) << "," << (entry.flip_x ? 1 : 0) << "," << (entry.flip_y ? 1 : 0) << ","
            << (entry.priority ? 1 : 0) << std::setfill(' ') << "\n";
    }
}

void write_sprites_csv(const std::filesystem::path& path, const std::vector<VdpSpriteEntry>& entries) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open sprite output file");
    }

    out << "index,raw_y,x,y,tile,terminator\n";
    for (const auto& entry : entries) {
        out << static_cast<int>(entry.index) << ",0x" << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(entry.raw_y) << std::dec << "," << entry.x << "," << entry.y << ",0x" << std::hex
            << std::setw(2) << static_cast<int>(entry.tile) << std::dec << "," << (entry.terminator ? 1 : 0)
            << std::setfill(' ') << "\n";
    }
}

template <typename Container> void write_binary_dump(const std::filesystem::path& path, const Container& bytes) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open dump output file");
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void write_coverage_csv(const std::filesystem::path& path,
                        const std::array<u32, 0x10000>& pc_counts,
                        const std::array<u8, 0x10000>& image) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open coverage output file");
    }

    out << "pc,count,opcode,mnemonic\n";
    for (std::size_t pc = 0; pc < pc_counts.size(); ++pc) {
        if (pc_counts[pc] == 0) {
            continue;
        }
        const auto decoded = decode_z80(image, static_cast<u16>(pc));
        out << "0x" << std::hex << std::setw(4) << std::setfill('0') << pc << std::dec << "," << pc_counts[pc] << ",0x"
            << std::hex << std::setw(2) << static_cast<int>(decoded.opcode) << std::dec << "," << decoded.mnemonic
            << "\n";
    }
}

struct InstructionFlow {
    bool ends_block = false;
    bool indirect = false;
    std::vector<u16> successors;
};

struct BlockInstruction {
    DecodedInstruction decoded;
    bool direct_emit = false;
};

struct BasicBlock {
    u16 start = 0;
    u16 end = 0;
    bool indirect_exit = false;
    std::vector<BlockInstruction> instructions;
    std::vector<u16> successors;
};

struct PointerTable {
    u16 address = 0;
    std::vector<u16> targets;
};

struct StaticHardwareAccess {
    u16 pc = 0;
    std::string kind;
    u16 target = 0;
    std::string note;
};

u16 read_u16_from_image(const std::array<u8, 0x10000>& image, u16 pc) {
    return make_u16(image[static_cast<u16>(pc + 1)], image[static_cast<u16>(pc + 2)]);
}

u16 relative_target(u16 pc, u8 displacement) {
    return static_cast<u16>(pc + 2 + static_cast<s8>(displacement));
}

bool in_code_window(u16 pc, std::size_t limit) {
    return pc < limit && pc < 0xC000;
}

bool is_direct_emit_supported(u8 opcode) {
    switch (opcode) {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x06:
    case 0x08:
    case 0x0A:
    case 0x0E:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1A:
    case 0x1E:
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x26:
    case 0x28:
    case 0x2A:
    case 0x2E:
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x36:
    case 0x38:
    case 0x3A:
    case 0x3E:
    case 0x76:
    case 0xC3:
    case 0xC6:
    case 0xD3:
    case 0xCE:
    case 0xD6:
    case 0xD9:
    case 0xDB:
    case 0xDE:
    case 0xE6:
    case 0xC9:
    case 0xCD:
    case 0xEE:
    case 0xE9:
    case 0xF6:
    case 0xF3:
    case 0xFB:
    case 0xFE:
        return true;
    default:
        if ((opcode & 0xC0) == 0x40 && opcode != 0x76) {
            return true;
        }
        if ((opcode & 0xC7) == 0xC0 || (opcode & 0xC7) == 0xC2 || (opcode & 0xC7) == 0xC4) {
            return true;
        }
        if ((opcode & 0xCF) == 0xC1 || (opcode & 0xCF) == 0xC5) {
            return true;
        }
        if ((opcode & 0xCF) == 0x03 || (opcode & 0xCF) == 0x0B) {
            return true;
        }
        if ((opcode & 0xCF) == 0x09) {
            return true;
        }
        if ((opcode & 0xC7) == 0x04 || (opcode & 0xC7) == 0x05 || (opcode & 0xC7) == 0x06) {
            return true;
        }
        if ((opcode & 0xC0) == 0x80) {
            return true;
        }
        if ((opcode & 0xC7) == 0xC7) {
            return true;
        }
        return false;
    }
}

bool is_direct_emit_supported(const std::array<u8, 0x10000>& image, u16 pc) {
    const u8 opcode = image[pc];
    if (opcode != 0xED) {
        if ((opcode == 0xDD || opcode == 0xFD) &&
            (image[static_cast<u16>(pc + 1)] == 0xE1 || image[static_cast<u16>(pc + 1)] == 0xE5)) {
            return true;
        }
        return is_direct_emit_supported(opcode);
    }

    switch (image[static_cast<u16>(pc + 1)]) {
    case 0x43:
    case 0x45:
    case 0x4D:
    case 0x4B:
    case 0x46:
    case 0x53:
    case 0x5B:
    case 0x56:
    case 0x5E:
    case 0x63:
    case 0x6B:
    case 0x66:
    case 0x73:
    case 0x7B:
    case 0x76:
    case 0x7E:
    case 0xA0:
    case 0xB0:
        return true;
    default:
        return false;
    }
}

InstructionFlow
classify_flow(const std::array<u8, 0x10000>& image, u16 pc, const DecodedInstruction& insn, std::size_t limit) {
    const u8 opcode = insn.opcode;
    const u16 next = static_cast<u16>(pc + insn.size);
    InstructionFlow flow;

    const auto add_successor = [&](u16 target) {
        if (in_code_window(target, limit) &&
            std::find(flow.successors.begin(), flow.successors.end(), target) == flow.successors.end()) {
            flow.successors.push_back(target);
        }
    };

    switch (opcode) {
    case 0x10:
        flow.ends_block = true;
        add_successor(relative_target(pc, image[static_cast<u16>(pc + 1)]));
        add_successor(next);
        break;
    case 0x18:
        flow.ends_block = true;
        add_successor(relative_target(pc, image[static_cast<u16>(pc + 1)]));
        break;
    case 0x20:
    case 0x28:
    case 0x30:
    case 0x38:
        flow.ends_block = true;
        add_successor(relative_target(pc, image[static_cast<u16>(pc + 1)]));
        add_successor(next);
        break;
    case 0xC3:
        flow.ends_block = true;
        add_successor(read_u16_from_image(image, pc));
        break;
    case 0xC9:
    case 0x76:
        flow.ends_block = true;
        break;
    case 0xCD:
        flow.ends_block = true;
        add_successor(read_u16_from_image(image, pc));
        add_successor(next);
        break;
    case 0xE9:
        flow.ends_block = true;
        flow.indirect = true;
        break;
    default:
        if ((opcode & 0xC7) == 0xC0) {
            flow.ends_block = true;
            add_successor(next);
        } else if ((opcode & 0xC7) == 0xC2) {
            flow.ends_block = true;
            add_successor(read_u16_from_image(image, pc));
            add_successor(next);
        } else if ((opcode & 0xC7) == 0xC4) {
            flow.ends_block = true;
            add_successor(read_u16_from_image(image, pc));
            add_successor(next);
        } else if ((opcode & 0xC7) == 0xC7) {
            flow.ends_block = true;
            add_successor(static_cast<u16>(opcode & 0x38));
            add_successor(next);
        }
        break;
    }

    return flow;
}

std::vector<u16> default_entry_points(std::size_t limit) {
    std::vector<u16> entries;
    for (const u16 entry : {static_cast<u16>(0x0000), static_cast<u16>(0x0038), static_cast<u16>(0x0066)}) {
        if (entry < limit) {
            entries.push_back(entry);
        }
    }
    return entries;
}

std::vector<BasicBlock>
discover_basic_blocks(const std::array<u8, 0x10000>& image, std::size_t limit, std::span<const u16> entry_points) {
    std::vector<BasicBlock> blocks;
    std::set<u16> queued_or_done;
    std::deque<u16> worklist;

    if (limit == 0) {
        return blocks;
    }

    for (const u16 entry : entry_points) {
        if (in_code_window(entry, limit) && queued_or_done.insert(entry).second) {
            worklist.push_back(entry);
        }
    }

    while (!worklist.empty()) {
        const u16 start = worklist.front();
        worklist.pop_front();
        if (!in_code_window(start, limit)) {
            continue;
        }

        BasicBlock block;
        block.start = start;
        u16 pc = start;
        std::set<u16> seen_inside_block;

        while (in_code_window(pc, limit) && seen_inside_block.insert(pc).second) {
            const auto decoded = decode_z80(image, pc);
            block.instructions.push_back({decoded, is_direct_emit_supported(image, pc)});
            const auto flow = classify_flow(image, pc, decoded, limit);
            block.end = static_cast<u16>(pc + decoded.size);
            block.indirect_exit = flow.indirect;
            block.successors = flow.successors;

            if (flow.ends_block) {
                break;
            }
            pc = static_cast<u16>(pc + decoded.size);
        }

        for (const u16 successor : block.successors) {
            if (queued_or_done.insert(successor).second) {
                worklist.push_back(successor);
            }
        }
        blocks.push_back(std::move(block));
    }

    std::sort(blocks.begin(), blocks.end(), [](const BasicBlock& lhs, const BasicBlock& rhs) {
        return lhs.start < rhs.start;
    });
    return blocks;
}

std::vector<PointerTable>
discover_pointer_tables(const std::array<u8, 0x10000>& image, std::size_t limit, std::span<const BasicBlock> blocks) {
    std::set<u16> block_starts;
    for (const auto& block : blocks) {
        block_starts.insert(block.start);
    }

    std::vector<PointerTable> tables;
    constexpr std::size_t kMaxPointerTableEntries = 256;
    for (std::size_t offset = 0; offset + 1 < limit && offset + 1 < 0xC000;) {
        const u16 address = static_cast<u16>(offset);
        if (block_starts.find(address) != block_starts.end()) {
            offset += 2;
            continue;
        }

        std::vector<u16> targets;
        std::size_t cursor = offset;
        while (cursor + 1 < limit && cursor + 1 < 0xC000 && targets.size() < kMaxPointerTableEntries) {
            if (block_starts.find(static_cast<u16>(cursor)) != block_starts.end() ||
                block_starts.find(static_cast<u16>(cursor + 1)) != block_starts.end()) {
                break;
            }
            const u16 target = make_u16(image[static_cast<u16>(cursor)], image[static_cast<u16>(cursor + 1)]);
            if (!in_code_window(target, limit)) {
                break;
            }
            targets.push_back(target);
            cursor += 2;
        }

        while (!targets.empty() && targets.back() == 0x0000) {
            targets.pop_back();
            cursor -= 2;
        }

        const std::set<u16> distinct_targets(targets.begin(), targets.end());
        const bool has_nonzero_target =
            std::any_of(targets.begin(), targets.end(), [](u16 target) { return target != 0; });
        std::size_t known_block_targets = 0;
        for (const u16 target : distinct_targets) {
            if (block_starts.find(target) != block_starts.end()) {
                ++known_block_targets;
            }
        }
        if (targets.size() >= 3 && distinct_targets.size() >= 2 && has_nonzero_target && known_block_targets >= 2) {
            tables.push_back({address, std::move(targets)});
            offset = cursor;
        } else {
            offset += 2;
        }
    }
    return tables;
}

const char* static_port_note(u8 port) {
    if (port == 0xF0 || port == 0xF1 || port == 0xF2) {
        return "fm";
    }
    if ((port & 0xC0) == 0x80) {
        return (port & 0x01) == 0 ? "vdp_data" : "vdp_control";
    }
    if ((port & 0xC0) == 0x40) {
        return (port & 0x01) == 0 ? "v_counter_or_psg" : "h_counter_or_psg";
    }
    if (port == 0xDC || port == 0xDD || port == 0xC0 || port == 0xC1) {
        return "joypad";
    }
    if (port == 0x3E || port == 0xDE || port == 0xDF) {
        return "memory_control";
    }
    return "io";
}

std::vector<StaticHardwareAccess> discover_static_hardware_accesses(const std::array<u8, 0x10000>& image,
                                                                    std::span<const BasicBlock> blocks) {
    std::vector<StaticHardwareAccess> accesses;
    for (const auto& block : blocks) {
        for (const auto& instruction : block.instructions) {
            const u16 pc = instruction.decoded.pc;
            const u8 opcode = instruction.decoded.opcode;
            if (opcode == 0xD3) {
                const u8 port = image[static_cast<u16>(pc + 1)];
                accesses.push_back({pc, "out", port, static_port_note(port)});
            } else if (opcode == 0xDB) {
                const u8 port = image[static_cast<u16>(pc + 1)];
                accesses.push_back({pc, "in", port, static_port_note(port)});
            } else if (opcode == 0x32) {
                const u16 target = read_u16_from_image(image, pc);
                if (target >= 0xFFFC) {
                    accesses.push_back({pc, "mapper_write", target, "mapper_register"});
                }
            }
        }
    }
    return accesses;
}

void write_analysis_report(const std::filesystem::path& path,
                           const std::array<u8, 0x10000>& image,
                           std::span<const u8> rom,
                           std::size_t limit,
                           std::span<const u16> entry_points,
                           const std::vector<BasicBlock>& blocks) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open analysis output file");
    }

    std::size_t instruction_count = 0;
    std::size_t direct_count = 0;
    std::size_t indirect_blocks = 0;
    std::array<std::size_t, 0x100> fallback_opcodes{};
    std::array<std::string, 0x100> fallback_mnemonics{};

    for (const auto& block : blocks) {
        if (block.indirect_exit) {
            ++indirect_blocks;
        }
        for (const auto& instruction : block.instructions) {
            ++instruction_count;
            if (instruction.direct_emit) {
                ++direct_count;
            } else {
                ++fallback_opcodes[instruction.decoded.opcode];
                if (fallback_mnemonics[instruction.decoded.opcode].empty()) {
                    fallback_mnemonics[instruction.decoded.opcode] = instruction.decoded.mnemonic;
                }
            }
        }
    }

    const std::size_t fallback_count = instruction_count - direct_count;
    const CartridgeHeaderInfo header = analyze_cartridge_header(rom);
    const std::vector<PointerTable> pointer_tables = discover_pointer_tables(image, limit, blocks);
    const std::vector<StaticHardwareAccess> hardware_accesses = discover_static_hardware_accesses(image, blocks);
    out << "SG3000Recomp static analysis\n";
    out << "rom_scan_limit: 0x" << std::hex << std::setw(4) << std::setfill('0') << std::min<std::size_t>(limit, 0xC000)
        << std::dec << "\n";
    out << "rom_size: " << rom.size() << "\n";
    out << "entry_points:";
    for (const u16 entry : entry_points) {
        out << " 0x" << std::hex << std::setw(4) << std::setfill('0') << entry;
    }
    out << std::dec << "\n";
    out << "header_found: " << (header.found ? "yes" : "no") << "\n";
    if (header.found) {
        out << "header_offset: 0x" << std::hex << std::setw(4) << std::setfill('0') << header.offset << std::dec
            << "\n";
        out << "header_platform: " << cartridge_platform_name(cartridge_header_platform(header)) << "\n";
        out << "header_region: " << cartridge_region_name(header.region) << "\n";
        out << "header_size_code: " << cartridge_size_code_name(header.region_size) << "\n";
        out << "header_product_code: " << header.product_code << "\n";
        out << "header_version: " << static_cast<int>(header.version) << "\n";
        out << "header_checksum_stored: 0x" << std::hex << std::setw(4) << std::setfill('0') << header.stored_checksum
            << "\n";
        out << "header_checksum_diagnostic: 0x" << std::setw(4) << header.diagnostic_checksum << std::dec << "\n";
        out << "header_declared_size_bytes: " << header.declared_size_bytes << "\n";
        out << "header_declared_size_fits_rom: " << (header.declared_size_fits_rom ? "yes" : "no") << "\n";
        if (header.declared_size_fits_rom) {
            out << "header_checksum_declared_size: 0x" << std::hex << std::setw(4) << std::setfill('0')
                << header.declared_size_checksum << std::dec << "\n";
            out << "header_checksum_matches_declared_size: " << (header.checksum_matches_declared_size ? "yes" : "no")
                << "\n";
        } else {
            out << "header_checksum_declared_size: unknown\n";
            out << "header_checksum_matches_declared_size: unknown\n";
        }
    }
    out << "basic_blocks: " << blocks.size() << "\n";
    out << "instructions: " << instruction_count << "\n";
    out << "direct_emit_instructions: " << direct_count << "\n";
    out << "fallback_instructions: " << fallback_count << "\n";
    out << "indirect_exit_blocks: " << indirect_blocks << "\n\n";
    out << "pointer_tables: " << pointer_tables.size() << "\n\n";
    out << "static_hardware_accesses: " << hardware_accesses.size() << "\n\n";

    out << "blocks\n";
    for (const auto& block : blocks) {
        out << "block 0x" << std::hex << std::setw(4) << std::setfill('0') << block.start << "-0x" << std::setw(4)
            << block.end << std::dec << " instructions=" << block.instructions.size();
        if (block.indirect_exit) {
            out << " indirect_exit";
        }
        out << " successors=";
        if (block.successors.empty()) {
            out << "none";
        } else {
            for (std::size_t i = 0; i < block.successors.size(); ++i) {
                if (i != 0) {
                    out << ",";
                }
                out << "0x" << std::hex << std::setw(4) << std::setfill('0') << block.successors[i] << std::dec;
            }
        }
        out << "\n";
        for (const auto& instruction : block.instructions) {
            out << "  0x" << std::hex << std::setw(4) << std::setfill('0') << instruction.decoded.pc << "  0x"
                << std::setw(2) << static_cast<int>(instruction.decoded.opcode) << std::dec << "  "
                << (instruction.direct_emit ? "direct  " : "fallback") << "  " << instruction.decoded.mnemonic << "\n";
        }
    }

    out << "\nfallback_opcodes\n";
    bool any_fallback = false;
    for (std::size_t opcode = 0; opcode < fallback_opcodes.size(); ++opcode) {
        if (fallback_opcodes[opcode] == 0) {
            continue;
        }
        any_fallback = true;
        out << "0x" << std::hex << std::setw(2) << std::setfill('0') << opcode << std::dec
            << " count=" << fallback_opcodes[opcode] << " mnemonic=" << fallback_mnemonics[opcode] << "\n";
    }
    if (!any_fallback) {
        out << "none\n";
    }

    out << "\npointer_tables_detail\n";
    if (pointer_tables.empty()) {
        out << "none\n";
    } else {
        for (const auto& table : pointer_tables) {
            out << "table 0x" << std::hex << std::setw(4) << std::setfill('0') << table.address << std::dec
                << " entries=" << table.targets.size() << " targets=";
            const std::size_t shown = std::min<std::size_t>(table.targets.size(), 16);
            for (std::size_t i = 0; i < shown; ++i) {
                if (i != 0) {
                    out << ",";
                }
                out << "0x" << std::hex << std::setw(4) << std::setfill('0') << table.targets[i] << std::dec;
            }
            if (shown < table.targets.size()) {
                out << ",...";
            }
            out << "\n";
        }
    }

    out << "\nstatic_hardware_accesses_detail\n";
    if (hardware_accesses.empty()) {
        out << "none\n";
    } else {
        for (const auto& access : hardware_accesses) {
            out << "0x" << std::hex << std::setw(4) << std::setfill('0') << access.pc << std::dec << " " << access.kind
                << " 0x" << std::hex << std::setw(access.target > 0xFF ? 4 : 2) << std::setfill('0') << access.target
                << std::dec << " " << access.note << "\n";
        }
    }
}

void run_smoke(ConsoleModel model,
               const std::vector<u8>& rom,
               const std::vector<u8>* bios,
               std::size_t max_steps,
               bool trace,
               const Options& opts) {
    Vdp vdp;
    Psg psg;
    Ym2413 ym2413;
    Joypad joypad;
    Bus bus(model, vdp, psg, ym2413, joypad);
    Z80State cpu;
    bus.set_mapper(opts.mapper);
    vdp.set_enhancements(opts.enhancements);
    psg.set_enhancements(opts.enhancements);
    psg.set_write_logging_enabled(!opts.dump_vgm.empty());
    bus.set_fm_present(opts.enhancements.enable_fm || !opts.dump_fm_log.empty());
    bus.set_io_logging_enabled(!opts.dump_io_log.empty());
    bus.set_memory_logging_enabled(!opts.dump_memory_log.empty());
    vdp.set_access_logging_enabled(!opts.dump_vdp_log.empty());
    ym2413.set_write_logging_enabled(!opts.dump_fm_log.empty());
    if (bios != nullptr) {
        bus.load_bios(*bios);
    }
    bus.load_rom(rom);
    if (!opts.load_sram.empty()) {
        bus.load_cartridge_ram(read_file(opts.load_sram));
    }
    SaveStateMetadata expected_state_metadata;
    expected_state_metadata.present = true;
    expected_state_metadata.model = model;
    expected_state_metadata.rom_hash = rom_hash_fnv1a64(rom);
    expected_state_metadata.bios_hash = bios != nullptr ? rom_hash_fnv1a64(*bios) : std::string{};
    if (!opts.load_state.empty()) {
        const SaveStateImage image = deserialize_console_state_image(read_file(opts.load_state));
        if (!opts.force_state) {
            validate_save_state_metadata(image.metadata, expected_state_metadata);
        }
        const ConsoleState& state = image.state;
        cpu = state.cpu;
        bus.load_state(state.bus);
        vdp.load_state(state.vdp);
        psg.load_state(state.psg);
        ym2413.load_state(state.ym2413);
        joypad.set_player1(state.joypad_player1);
        joypad.set_player2(state.joypad_player2);
    }

    const auto& image = bus.debug_memory();
    std::bitset<0x10000> visited_pc;
    std::array<u32, 0x10000> pc_counts{};
    std::vector<s16> audio_samples;
    constexpr u32 audio_sample_rate = 44100;
    constexpr int cpu_cycles_per_audio_sample = 81;
    int audio_cycle_accumulator = 0;
    const auto tick_devices = [&](int elapsed) {
        vdp.tick(elapsed);
        psg.tick(elapsed);
        ym2413.tick(elapsed);
        if (!opts.dump_audio.empty()) {
            audio_cycle_accumulator += elapsed;
            while (audio_cycle_accumulator >= cpu_cycles_per_audio_sample) {
                audio_cycle_accumulator -= cpu_cycles_per_audio_sample;
                const auto psg_sample = ym2413.psg_enabled() ? psg.sample() : std::array<float, 2>{0.0F, 0.0F};
                const auto fm_sample = ym2413.sample();
                const std::array<float, 2> sample{psg_sample[0] + fm_sample[0], psg_sample[1] + fm_sample[1]};
                for (float channel : sample) {
                    const float clipped = std::clamp(channel, -1.0F, 1.0F);
                    audio_samples.push_back(static_cast<s16>(clipped * 32767.0F));
                }
            }
        }
    };
    const auto print_runtime_summary = [&]() {
        const auto& framebuffer = vdp.framebuffer();
        const auto lit_pixels =
            std::count_if(framebuffer.begin(), framebuffer.end(), [](u32 pixel) { return (pixel & 0x00FFFFFF) != 0; });
        const auto audio = psg.sample();
        const auto fm_audio = ym2413.sample();
        std::cout << "visited pcs: " << visited_pc.count() << "\nframebuffer lit pixels: " << lit_pixels
                  << "\npsg sample: " << std::fixed << std::setprecision(4) << audio[0] << "," << audio[1]
                  << "\nfm present: " << (ym2413.present() ? "yes" : "no")
                  << ", fm enabled: " << (ym2413.fm_enabled() ? "yes" : "no") << ", fm sample: " << fm_audio[0] << ","
                  << fm_audio[1] << "\nenhancements: mode="
                  << (opts.enhancements.mode == RuntimeMode::Enhanced ? "enhanced"
                      : opts.enhancements.mode == RuntimeMode::Hybrid ? "hybrid"
                                                                      : "accurate")
                  << ", disable_sprite_limit=" << (opts.enhancements.disable_sprite_limit ? "on" : "off")
                  << ", reduce_flicker=" << (opts.enhancements.reduce_flicker ? "on" : "off")
                  << ", enable_fm=" << (opts.enhancements.enable_fm ? "on" : "off") << "\n";
        if (!opts.dump_frame.empty()) {
            write_frame_ppm(opts.dump_frame, framebuffer);
            std::cout << "frame dumped: " << opts.dump_frame.string() << "\n";
        }
        if (!opts.dump_frame_bmp.empty()) {
            write_frame_bmp(opts.dump_frame_bmp, framebuffer);
            std::cout << "bmp frame dumped: " << opts.dump_frame_bmp.string() << "\n";
        }
        if (!opts.dump_audio.empty()) {
            write_audio_wav(opts.dump_audio, audio_samples, audio_sample_rate);
            std::cout << "audio dumped: " << opts.dump_audio.string() << " (" << (audio_samples.size() / 2)
                      << " stereo samples)\n";
        }
        if (!opts.dump_vgm.empty()) {
            write_psg_vgm(opts.dump_vgm, psg.logged_writes(), cpu.cycles);
            std::cout << "vgm dumped: " << opts.dump_vgm.string() << " (" << psg.logged_writes().size()
                      << " psg writes)\n";
        }
        if (!opts.dump_fm_log.empty()) {
            write_fm_log_csv(opts.dump_fm_log, ym2413.logged_writes());
            std::cout << "fm log dumped: " << opts.dump_fm_log.string() << " (" << ym2413.logged_writes().size()
                      << " writes)\n";
        }
        if (!opts.dump_io_log.empty()) {
            write_io_log_csv(opts.dump_io_log, bus.logged_io(), opts.io_port_filters);
            std::cout << "io log dumped: " << opts.dump_io_log.string() << " (" << bus.logged_io().size()
                      << " accesses)\n";
        }
        if (!opts.dump_memory_log.empty()) {
            write_memory_log_csv(opts.dump_memory_log, bus.logged_memory(), opts.memory_filters);
            std::cout << "memory log dumped: " << opts.dump_memory_log.string() << " (" << bus.logged_memory().size()
                      << " writes)\n";
        }
        if (!opts.dump_vdp_log.empty()) {
            write_vdp_log_csv(opts.dump_vdp_log, vdp.logged_accesses(), opts.vdp_filters);
            std::cout << "vdp log dumped: " << opts.dump_vdp_log.string() << " (" << vdp.logged_accesses().size()
                      << " writes)\n";
        }
        if (!opts.dump_vram.empty()) {
            write_binary_dump(opts.dump_vram, vdp.debug_vram());
            std::cout << "vram dumped: " << opts.dump_vram.string() << "\n";
        }
        if (!opts.dump_cram.empty()) {
            write_binary_dump(opts.dump_cram, vdp.debug_cram());
            std::cout << "cram dumped: " << opts.dump_cram.string() << "\n";
        }
        if (!opts.dump_tilemap.empty()) {
            const auto tilemap = vdp.debug_tilemap();
            write_tilemap_csv(opts.dump_tilemap, tilemap);
            std::cout << "tilemap dumped: " << opts.dump_tilemap.string() << " (" << tilemap.size() << " entries)\n";
        }
        if (!opts.dump_sprites.empty()) {
            const auto sprites = vdp.debug_sprites();
            write_sprites_csv(opts.dump_sprites, sprites);
            std::cout << "sprites dumped: " << opts.dump_sprites.string() << " (" << sprites.size() << " entries)\n";
        }
        if (!opts.dump_sram.empty()) {
            write_binary_dump(opts.dump_sram, bus.debug_cartridge_ram());
            std::cout << "sram dumped: " << opts.dump_sram.string() << "\n";
        }
        if (!opts.save_sram.empty()) {
            write_binary_dump(opts.save_sram, bus.debug_cartridge_ram());
            std::cout << "sram saved: " << opts.save_sram.string()
                      << (bus.cartridge_ram_dirty() ? " (dirty)" : " (unchanged)") << "\n";
        }
        if (!opts.save_state.empty()) {
            const ConsoleState state{
                cpu,
                bus.save_state(),
                vdp.save_state(),
                psg.save_state(),
                ym2413.save_state(),
                joypad.player1(),
                joypad.player2(),
            };
            write_binary_dump(opts.save_state, serialize_console_state(state, expected_state_metadata));
            std::cout << "state saved: " << opts.save_state.string() << "\n";
        }
        if (!opts.dump_coverage.empty()) {
            write_coverage_csv(opts.dump_coverage, pc_counts, image);
            std::cout << "coverage dumped: " << opts.dump_coverage.string() << "\n";
        }
    };
    for (std::size_t step = 0; step < max_steps; ++step) {
        const u16 pc_before = cpu.pc;
        visited_pc.set(pc_before);
        ++pc_counts[pc_before];
        const auto insn = decode_z80(image, pc_before);
        if (trace) {
            std::cout << std::dec << std::setw(8) << step << "  ";
            dump_z80_state(std::cout, cpu);
            std::cout << "  " << insn.mnemonic << "\n";
        }
        try {
            const u64 cycles_before = cpu.cycles;
            bus.set_cycle(cycles_before);
            vdp.set_cycle(cycles_before);
            psg.set_cycle(cycles_before);
            ym2413.set_cycle(cycles_before);
            execute_one(cpu, bus);
            tick_devices(static_cast<int>(cpu.cycles - cycles_before));
            if (vdp.irq_pending()) {
                const u64 irq_before = cpu.cycles;
                bus.set_cycle(irq_before);
                vdp.set_cycle(irq_before);
                psg.set_cycle(irq_before);
                ym2413.set_cycle(irq_before);
                if (service_maskable_interrupt(cpu, bus)) {
                    tick_devices(static_cast<int>(cpu.cycles - irq_before));
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "execution stopped at step " << step << ": " << e.what() << "\n";
            std::cerr << "instruction: ";
            std::cerr << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << pc_before << "  "
                      << std::setw(2) << static_cast<int>(insn.opcode) << "  " << insn.mnemonic << "\n";
            dump_z80_state(std::cerr, cpu);
            std::cerr << "\n";
            return;
        }
        if (cpu.halted) {
            std::cout << "halted after " << (step + 1) << " steps\n";
            dump_z80_state(std::cout, cpu);
            std::cout << "\n";
            print_runtime_summary();
            return;
        }
    }

    std::cout << "step limit reached: " << max_steps << "\n";
    dump_z80_state(std::cout, cpu);
    std::cout << "\n";
    print_runtime_summary();
}

void run_host(ConsoleModel model, const std::vector<u8>& rom, const std::vector<u8>* bios, const Options& opts) {
    const HostRuntimeConfig host_config = host_runtime_config_for_video_standard(opts.video_standard);
    HostRuntimeConfig runtime_config = host_config;
    runtime_config.audio_sample_rate = opts.audio_sample_rate;
    HostRuntime host(model, opts.enhancements, runtime_config);
    host.console().bus().set_mapper(opts.mapper);
    host.console().psg().set_write_logging_enabled(!opts.dump_vgm.empty());
    host.console().bus().set_fm_present(opts.enhancements.enable_fm || !opts.dump_fm_log.empty());
    host.console().bus().set_io_logging_enabled(!opts.dump_io_log.empty());
    host.console().bus().set_memory_logging_enabled(!opts.dump_memory_log.empty());
    host.console().vdp().set_access_logging_enabled(!opts.dump_vdp_log.empty());
    host.console().ym2413().set_write_logging_enabled(!opts.dump_fm_log.empty());
    if (bios != nullptr) {
        host.load_bios(*bios);
    }
    host.load_rom(rom);
    if (!opts.load_sram.empty()) {
        host.console().bus().load_cartridge_ram(read_file(opts.load_sram));
    }
    SaveStateMetadata expected_state_metadata;
    expected_state_metadata.present = true;
    expected_state_metadata.model = model;
    expected_state_metadata.rom_hash = rom_hash_fnv1a64(rom);
    expected_state_metadata.bios_hash = bios != nullptr ? rom_hash_fnv1a64(*bios) : std::string{};
    if (!opts.load_state.empty()) {
        const auto state_bytes = read_file(opts.load_state);
        if (!opts.force_state) {
            validate_save_state_metadata(read_save_state_metadata(state_bytes), expected_state_metadata);
        }
        load_console_state(host.console(), state_bytes);
    }

    const bool bios_active_at_start = host.console().bus().bios_enabled();
    bool bios_active_previous_frame = bios_active_at_start;
    std::optional<std::size_t> bios_handoff_frame;

    const HostInputScript input_script =
        opts.input_script.empty() ? HostInputScript{} : parse_host_input_script(read_text_file(opts.input_script));
    std::ofstream frame_log;
    if (!opts.dump_frame_log.empty()) {
        frame_log.open(opts.dump_frame_log);
        if (!frame_log) {
            throw std::runtime_error("cannot open frame log output");
        }
        frame_log << "frame,start_cycle,end_cycle,instructions,pc_min,pc_max,framebuffer_fnv1a64,"
                  << "audio_frames,nonzero_audio_samples,mapper,memory_control,bios_enabled,cartridge_enabled,work_ram_"
                     "enabled,expansion_enabled,card_enabled,io_chip_enabled,"
                  << "bank0,bank1,bank2,bank3,bank4,bank5,"
                  << "cartridge_ram,cartridge_ram_bank\n";
    }
    HostFrameResult result{};
    for (std::size_t frame = 0; frame < opts.host_frames; ++frame) {
        const std::size_t audio_start = host.audio().size();
        result = host.run_frame(input_script.state_for_frame(frame));
        const bool bios_active = host.console().bus().bios_enabled();
        if (bios_active_previous_frame && !bios_active && !bios_handoff_frame) {
            bios_handoff_frame = result.frame_index;
        }
        bios_active_previous_frame = bios_active;
        if (frame_log) {
            constexpr u64 offset_basis = 14695981039346656037ULL;
            constexpr u64 prime = 1099511628211ULL;
            u64 framebuffer_hash = offset_basis;
            for (const u32 pixel : host.framebuffer()) {
                for (int shift = 0; shift < 32; shift += 8) {
                    framebuffer_hash ^= static_cast<u8>((pixel >> shift) & 0xFF);
                    framebuffer_hash *= prime;
                }
            }
            const auto nonzero_audio = std::count_if(host.audio().begin() + static_cast<std::ptrdiff_t>(audio_start),
                                                     host.audio().end(),
                                                     [](s16 value) { return value != 0; });
            const auto mapper = host.console().bus().mapper_snapshot();
            std::array<u8, 6> banks{{0, 1, 2, 0, 0, 0}};
            if (mapper.mapper == CartridgeMapper::SMapper) {
                std::copy(mapper.smapper_slots.begin(), mapper.smapper_slots.end(), banks.begin());
            } else if (mapper.mapper == CartridgeMapper::CMapper) {
                std::copy(mapper.cmapper_slots.begin(), mapper.cmapper_slots.end(), banks.begin());
            } else if (mapper.mapper == CartridgeMapper::KMapper) {
                banks[2] = mapper.kmapper_slot2;
            } else if (mapper.mapper == CartridgeMapper::K8KMapper) {
                banks = mapper.k8k_slots;
            }
            frame_log << result.frame_index << ',' << result.start_cycle << ',' << result.end_cycle << ','
                      << result.instructions << ",0x" << std::hex << std::setw(4) << std::setfill('0') << result.pc_min
                      << ",0x" << std::setw(4) << result.pc_max << ',' << std::setw(16) << framebuffer_hash << std::dec
                      << std::setfill(' ') << ',' << (host.audio().size() - audio_start) / 2 << ',' << nonzero_audio
                      << ',' << cartridge_mapper_name(mapper.mapper) << ",0x" << std::hex << std::setw(2)
                      << std::setfill('0') << static_cast<int>(mapper.memory_control) << std::dec << std::setfill(' ')
                      << ',' << (mapper.bios_enabled ? 1 : 0) << ',' << (mapper.cartridge_enabled ? 1 : 0) << ','
                      << (mapper.work_ram_enabled ? 1 : 0) << ',' << (mapper.expansion_enabled ? 1 : 0) << ','
                      << (mapper.card_enabled ? 1 : 0) << ',' << (mapper.io_chip_enabled ? 1 : 0);
            for (u8 bank : banks) {
                frame_log << ',' << static_cast<int>(bank);
            }
            frame_log << ',' << (mapper.cartridge_ram_enabled ? 1 : 0) << ','
                      << static_cast<int>(mapper.cartridge_ram_bank) << '\n';
        }
    }

    const auto& framebuffer = host.framebuffer();
    const auto lit_pixels =
        std::count_if(framebuffer.begin(), framebuffer.end(), [](u32 pixel) { return (pixel & 0x00FFFFFF) != 0; });
    const auto sample = host.console().psg().sample();
    const auto fm_sample = host.console().ym2413().sample();

    std::cout << "host frames: " << opts.host_frames << "\nframe index: " << result.frame_index
              << "\ncycles: " << result.start_cycle << "-" << result.end_cycle
              << "\nvideo standard: " << host_video_standard_name(opts.video_standard) << " ("
              << host.config().cpu_cycles_per_scanline << "x" << host.config().scanlines_per_frame << " cycles/frame)"
              << "\naudio sample rate: " << host.config().audio_sample_rate
              << "\nframebuffer lit pixels: " << lit_pixels << "\naudio samples: " << result.stereo_samples
              << "\npsg sample: " << std::fixed << std::setprecision(4) << sample[0] << "," << sample[1]
              << "\nfm present: " << (host.console().ym2413().present() ? "yes" : "no")
              << ", fm enabled: " << (host.console().ym2413().fm_enabled() ? "yes" : "no")
              << ", fm sample: " << fm_sample[0] << "," << fm_sample[1] << "\nenhancements: mode="
              << (opts.enhancements.mode == RuntimeMode::Enhanced ? "enhanced"
                  : opts.enhancements.mode == RuntimeMode::Hybrid ? "hybrid"
                                                                  : "accurate")
              << ", disable_sprite_limit=" << (opts.enhancements.disable_sprite_limit ? "on" : "off")
              << ", reduce_flicker=" << (opts.enhancements.reduce_flicker ? "on" : "off")
              << ", enable_fm=" << (opts.enhancements.enable_fm ? "on" : "off") << "\n";
    if (bios == nullptr) {
        std::cout << "bios: not loaded\n";
    } else if (bios_handoff_frame) {
        std::cout << "bios: active at start, handoff frame " << *bios_handoff_frame << "\n";
    } else if (bios_active_at_start) {
        std::cout << "bios: active at start, handoff not observed\n";
    } else {
        std::cout << "bios: loaded, already disabled by restored state\n";
    }
    if (!opts.input_script.empty()) {
        std::cout << "input script: " << opts.input_script.string() << " (" << input_script.events().size()
                  << " events)\n";
    }
    if (!opts.dump_frame_log.empty()) {
        std::cout << "frame log dumped: " << opts.dump_frame_log.string() << "\n";
    }

    if (!opts.dump_frame.empty()) {
        write_frame_ppm(opts.dump_frame, framebuffer);
        std::cout << "frame dumped: " << opts.dump_frame.string() << "\n";
    }
    if (!opts.dump_frame_bmp.empty()) {
        write_frame_bmp(opts.dump_frame_bmp, framebuffer);
        std::cout << "bmp frame dumped: " << opts.dump_frame_bmp.string() << "\n";
    }
    if (!opts.dump_audio.empty()) {
        write_audio_wav(opts.dump_audio, host.audio(), host.config().audio_sample_rate);
        std::cout << "audio dumped: " << opts.dump_audio.string() << " (" << (host.audio().size() / 2)
                  << " stereo samples)\n";
    }
    if (!opts.dump_vgm.empty()) {
        write_psg_vgm(opts.dump_vgm, host.console().psg().logged_writes(), host.console().cpu().cycles);
        std::cout << "vgm dumped: " << opts.dump_vgm.string() << " (" << host.console().psg().logged_writes().size()
                  << " psg writes)\n";
    }
    if (!opts.dump_fm_log.empty()) {
        write_fm_log_csv(opts.dump_fm_log, host.console().ym2413().logged_writes());
        std::cout << "fm log dumped: " << opts.dump_fm_log.string() << " ("
                  << host.console().ym2413().logged_writes().size() << " writes)\n";
    }
    if (!opts.dump_io_log.empty()) {
        write_io_log_csv(opts.dump_io_log, host.console().bus().logged_io(), opts.io_port_filters);
        std::cout << "io log dumped: " << opts.dump_io_log.string() << " (" << host.console().bus().logged_io().size()
                  << " accesses)\n";
    }
    if (!opts.dump_memory_log.empty()) {
        write_memory_log_csv(opts.dump_memory_log, host.console().bus().logged_memory(), opts.memory_filters);
        std::cout << "memory log dumped: " << opts.dump_memory_log.string() << " ("
                  << host.console().bus().logged_memory().size() << " writes)\n";
    }
    if (!opts.dump_vdp_log.empty()) {
        write_vdp_log_csv(opts.dump_vdp_log, host.console().vdp().logged_accesses(), opts.vdp_filters);
        std::cout << "vdp log dumped: " << opts.dump_vdp_log.string() << " ("
                  << host.console().vdp().logged_accesses().size() << " writes)\n";
    }
    if (!opts.dump_vram.empty()) {
        write_binary_dump(opts.dump_vram, host.console().vdp().debug_vram());
        std::cout << "vram dumped: " << opts.dump_vram.string() << "\n";
    }
    if (!opts.dump_cram.empty()) {
        write_binary_dump(opts.dump_cram, host.console().vdp().debug_cram());
        std::cout << "cram dumped: " << opts.dump_cram.string() << "\n";
    }
    if (!opts.dump_tilemap.empty()) {
        const auto tilemap = host.console().vdp().debug_tilemap();
        write_tilemap_csv(opts.dump_tilemap, tilemap);
        std::cout << "tilemap dumped: " << opts.dump_tilemap.string() << " (" << tilemap.size() << " entries)\n";
    }
    if (!opts.dump_sprites.empty()) {
        const auto sprites = host.console().vdp().debug_sprites();
        write_sprites_csv(opts.dump_sprites, sprites);
        std::cout << "sprites dumped: " << opts.dump_sprites.string() << " (" << sprites.size() << " entries)\n";
    }
    if (!opts.dump_sram.empty()) {
        write_binary_dump(opts.dump_sram, host.console().bus().debug_cartridge_ram());
        std::cout << "sram dumped: " << opts.dump_sram.string() << "\n";
    }
    if (!opts.save_sram.empty()) {
        const auto& sram = host.console().bus().debug_cartridge_ram();
        write_binary_dump(opts.save_sram, sram);
        std::cout << "sram saved: " << opts.save_sram.string()
                  << (host.console().bus().cartridge_ram_dirty() ? " (dirty)" : " (unchanged)") << "\n";
    }
    if (!opts.save_state.empty()) {
        write_binary_dump(opts.save_state, save_console_state(host.console(), expected_state_metadata));
        std::cout << "state saved: " << opts.save_state.string() << "\n";
    }
}

const char* reg_lvalue(u8 index) {
    static constexpr const char* names[] = {"cpu.b", "cpu.c", "cpu.d", "cpu.e", "cpu.h", "cpu.l", "", "cpu.a"};
    return names[index & 0x07];
}

void emit_ld_reg_from_value(std::ostream& out, u8 reg, const std::string& value) {
    if (reg == 6) {
        out << "bus.write(cpu.hl(), " << value << "); ";
    } else {
        out << reg_lvalue(reg) << " = " << value << "; ";
    }
}

std::string read_reg_expr(u8 reg) {
    if (reg == 6) {
        return "bus.read(cpu.hl())";
    }
    return reg_lvalue(reg);
}

const char* qq_read_expr(u8 index) {
    static constexpr const char* names[] = {"cpu.bc()", "cpu.de()", "cpu.hl()", "cpu.af()"};
    return names[index & 0x03];
}

const char* qq_write_call(u8 index) {
    static constexpr const char* names[] = {"cpu.set_bc", "cpu.set_de", "cpu.set_hl", "cpu.set_af"};
    return names[index & 0x03];
}

std::string rp_read_expr(u8 index) {
    static constexpr const char* names[] = {"cpu.bc()", "cpu.de()", "cpu.hl()", "cpu.sp"};
    return names[index & 0x03];
}

void emit_rp_write(std::ostream& out, u8 index, const std::string& value) {
    switch (index & 0x03) {
    case 0:
        out << "cpu.set_bc(" << value << "); ";
        break;
    case 1:
        out << "cpu.set_de(" << value << "); ";
        break;
    case 2:
        out << "cpu.set_hl(" << value << "); ";
        break;
    default:
        out << "cpu.sp = " << value << "; ";
        break;
    }
}

const char* condition_expr(u8 index) {
    static constexpr const char* names[] = {
        "(cpu.f & 0x40) == 0",
        "(cpu.f & 0x40) != 0",
        "(cpu.f & 0x01) == 0",
        "(cpu.f & 0x01) != 0",
        "(cpu.f & 0x04) == 0",
        "(cpu.f & 0x04) != 0",
        "(cpu.f & 0x80) == 0",
        "(cpu.f & 0x80) != 0",
    };
    return names[index & 0x07];
}

void emit_push16(std::ostream& out, const std::string& value) {
    out << "cpu.sp = static_cast<sgrecomp::u16>(cpu.sp - 1); "
        << "bus.write(cpu.sp, static_cast<sgrecomp::u8>((" << value << " >> 8) & 0xff)); "
        << "cpu.sp = static_cast<sgrecomp::u16>(cpu.sp - 1); "
        << "bus.write(cpu.sp, static_cast<sgrecomp::u8>(" << value << " & 0xff)); ";
}

void emit_pop16_to(std::ostream& out, const std::string& target_call) {
    out << "{ const auto lo = bus.read(cpu.sp); cpu.sp = static_cast<sgrecomp::u16>(cpu.sp + 1); "
        << "const auto hi = bus.read(cpu.sp); cpu.sp = static_cast<sgrecomp::u16>(cpu.sp + 1); " << target_call
        << "(sgrecomp::make_u16(lo, hi)); ";
}

const char* alu_helper(u8 group) {
    static constexpr const char* names[] = {
        "sgrecomp_add8",
        "sgrecomp_adc8",
        "sgrecomp_sub8",
        "sgrecomp_sbc8",
        "sgrecomp_and8",
        "sgrecomp_xor8",
        "sgrecomp_or8",
        "sgrecomp_cp8",
    };
    return names[group & 0x07];
}

void emit_alu(std::ostream& out, u8 group, const std::string& rhs) {
    if (group == 7) {
        out << "sgrecomp_cp8(cpu, cpu.a, " << rhs << "); ";
    } else {
        out << "cpu.a = " << alu_helper(group) << "(cpu, cpu.a, " << rhs << "); ";
    }
}

void emit_instruction_body(std::ostream& out, const std::array<u8, 0x10000>& image, u16 pc) {
    const u8 opcode = image[pc];
    const auto decoded = decode_z80(image, pc);
    out << "/* " << decoded.mnemonic << " */ ";
    switch (opcode) {
    case 0x00:
        out << "cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 4; return;\n";
        break;
    case 0x01:
        out << "cpu.set_bc(0x" << std::setw(4) << make_u16(image[pc + 1], image[pc + 2]) << "); cpu.pc = 0x"
            << std::setw(4) << (pc + 3) << "; cpu.cycles += 10; return;\n";
        break;
    case 0x02:
        out << "bus.write(cpu.bc(), cpu.a); cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x08:
        out << "{ const auto a = cpu.a; const auto f = cpu.f; cpu.a = cpu.a_alt; cpu.f = cpu.f_alt; "
            << "cpu.a_alt = a; cpu.f_alt = f; cpu.pc = 0x" << std::setw(4) << (pc + 1)
            << "; cpu.cycles += 4; return; }\n";
        break;
    case 0x3E:
        out << "cpu.a = 0x" << std::setw(2) << static_cast<int>(image[pc + 1]) << "; cpu.pc = 0x" << std::setw(4)
            << (pc + 2) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x06:
        out << "cpu.b = 0x" << std::setw(2) << static_cast<int>(image[pc + 1]) << "; cpu.pc = 0x" << std::setw(4)
            << (pc + 2) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x0A:
        out << "cpu.a = bus.read(cpu.bc()); cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x0E:
        out << "cpu.c = 0x" << std::setw(2) << static_cast<int>(image[pc + 1]) << "; cpu.pc = 0x" << std::setw(4)
            << (pc + 2) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x10: {
        const auto displacement = static_cast<s8>(image[pc + 1]);
        const u16 target = static_cast<u16>(pc + 2 + displacement);
        out << "cpu.b = static_cast<sgrecomp::u8>(cpu.b - 1); if (cpu.b != 0) { cpu.pc = 0x" << std::setw(4) << target
            << "; cpu.cycles += 13; } else { cpu.pc = 0x" << std::setw(4) << (pc + 2)
            << "; cpu.cycles += 8; } return;\n";
        break;
    }
    case 0x11:
        out << "cpu.set_de(0x" << std::setw(4) << make_u16(image[pc + 1], image[pc + 2]) << "); cpu.pc = 0x"
            << std::setw(4) << (pc + 3) << "; cpu.cycles += 10; return;\n";
        break;
    case 0x12:
        out << "bus.write(cpu.de(), cpu.a); cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x16:
        out << "cpu.d = 0x" << std::setw(2) << static_cast<int>(image[pc + 1]) << "; cpu.pc = 0x" << std::setw(4)
            << (pc + 2) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x17:
        out << "{ const bool carry = (cpu.a & 0x80) != 0; cpu.a = static_cast<sgrecomp::u8>((cpu.a << 1) | (cpu.f & "
               "0x01)); "
            << "cpu.f = static_cast<sgrecomp::u8>((cpu.f & 0x84) | (cpu.a & 0x28) | (carry ? 0x01 : 0)); cpu.pc = 0x"
            << std::setw(4) << (pc + 1) << "; cpu.cycles += 4; return; }\n";
        break;
    case 0x18: {
        const auto displacement = static_cast<s8>(image[pc + 1]);
        const u16 target = static_cast<u16>(pc + 2 + displacement);
        out << "cpu.pc = 0x" << std::setw(4) << target << "; cpu.cycles += 12; return;\n";
        break;
    }
    case 0x20:
    case 0x28:
    case 0x30:
    case 0x38: {
        const auto displacement = static_cast<s8>(image[pc + 1]);
        const u16 target = static_cast<u16>(pc + 2 + displacement);
        const u8 condition = static_cast<u8>((opcode >> 3) & 0x03);
        out << "if (" << condition_expr(condition) << ") { cpu.pc = 0x" << std::setw(4) << target
            << "; cpu.cycles += 12; } else { cpu.pc = 0x" << std::setw(4) << (pc + 2)
            << "; cpu.cycles += 7; } return;\n";
        break;
    }
    case 0x1A:
        out << "cpu.a = bus.read(cpu.de()); cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x1E:
        out << "cpu.e = 0x" << std::setw(2) << static_cast<int>(image[pc + 1]) << "; cpu.pc = 0x" << std::setw(4)
            << (pc + 2) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x19:
        out << "{ const auto lhs = cpu.hl(); const auto rhs = cpu.de(); const auto sum = static_cast<unsigned>(lhs + "
               "rhs); "
            << "const auto result = static_cast<sgrecomp::u16>(sum); cpu.f = static_cast<sgrecomp::u8>((cpu.f & 0x84) "
               "| ((result >> 8) & 0x28) | "
            << "(((lhs ^ rhs ^ result) & 0x1000) ? 0x10 : 0) | (sum > 0xffff ? 0x01 : 0)); "
            << "cpu.set_hl(result); cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 11; return; }\n";
        break;
    case 0x21:
        out << "cpu.set_hl(0x" << std::setw(4) << make_u16(image[pc + 1], image[pc + 2]) << "); cpu.pc = 0x"
            << std::setw(4) << (pc + 3) << "; cpu.cycles += 10; return;\n";
        break;
    case 0x22: {
        const u16 address = make_u16(image[pc + 1], image[pc + 2]);
        out << "bus.write(0x" << std::setw(4) << address << ", cpu.l); bus.write(0x" << std::setw(4)
            << static_cast<u16>(address + 1) << ", cpu.h); cpu.pc = 0x" << std::setw(4) << (pc + 3)
            << "; cpu.cycles += 16; return;\n";
        break;
    }
    case 0x26:
        out << "cpu.h = 0x" << std::setw(2) << static_cast<int>(image[pc + 1]) << "; cpu.pc = 0x" << std::setw(4)
            << (pc + 2) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x2E:
        out << "cpu.l = 0x" << std::setw(2) << static_cast<int>(image[pc + 1]) << "; cpu.pc = 0x" << std::setw(4)
            << (pc + 2) << "; cpu.cycles += 7; return;\n";
        break;
    case 0x2A: {
        const u16 address = make_u16(image[pc + 1], image[pc + 2]);
        out << "cpu.l = bus.read(0x" << std::setw(4) << address << "); cpu.h = bus.read(0x" << std::setw(4)
            << static_cast<u16>(address + 1) << "); cpu.pc = 0x" << std::setw(4) << (pc + 3)
            << "; cpu.cycles += 16; return;\n";
        break;
    }
    case 0x31:
        out << "cpu.sp = 0x" << std::setw(4) << make_u16(image[pc + 1], image[pc + 2]) << "; cpu.pc = 0x"
            << std::setw(4) << (pc + 3) << "; cpu.cycles += 10; return;\n";
        break;
    case 0x32: {
        const u16 address = make_u16(image[pc + 1], image[pc + 2]);
        out << "bus.write(0x" << std::setw(4) << address << ", cpu.a); cpu.pc = 0x" << std::setw(4) << (pc + 3)
            << "; cpu.cycles += 13; return;\n";
        break;
    }
    case 0x36:
        out << "bus.write(cpu.hl(), 0x" << std::setw(2) << static_cast<int>(image[pc + 1]) << "); cpu.pc = 0x"
            << std::setw(4) << (pc + 2) << "; cpu.cycles += 10; return;\n";
        break;
    case 0x3A: {
        const u16 address = make_u16(image[pc + 1], image[pc + 2]);
        out << "cpu.a = bus.read(0x" << std::setw(4) << address << "); cpu.pc = 0x" << std::setw(4) << (pc + 3)
            << "; cpu.cycles += 13; return;\n";
        break;
    }
    case 0xC3: {
        const u16 target = make_u16(image[pc + 1], image[pc + 2]);
        out << "cpu.pc = 0x" << std::setw(4) << target << "; cpu.cycles += 10; return;\n";
        break;
    }
    case 0xC6:
    case 0xCE:
    case 0xD6:
    case 0xDE:
    case 0xE6:
    case 0xEE:
    case 0xF6:
    case 0xFE: {
        const u8 group = static_cast<u8>((opcode >> 3) & 0x07);
        std::ostringstream rhs;
        rhs << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(image[pc + 1]);
        emit_alu(out, group, rhs.str());
        out << "cpu.pc = 0x" << std::setw(4) << (pc + 2) << "; cpu.cycles += 7; return;\n";
        break;
    }
    case 0xC9:
        out << "{ const auto lo = bus.read(cpu.sp); cpu.sp = static_cast<sgrecomp::u16>(cpu.sp + 1); "
            << "const auto hi = bus.read(cpu.sp); cpu.sp = static_cast<sgrecomp::u16>(cpu.sp + 1); "
            << "cpu.pc = sgrecomp::make_u16(lo, hi); cpu.cycles += 10; return; }\n";
        break;
    case 0xCD: {
        const u16 target = make_u16(image[pc + 1], image[pc + 2]);
        emit_push16(
            out, "0x" + [&] {
                std::ostringstream ss;
                ss << std::hex << std::setw(4) << std::setfill('0') << static_cast<u16>(pc + 3);
                return ss.str();
            }());
        out << "cpu.pc = 0x" << std::setw(4) << target << "; cpu.cycles += 17; return;\n";
        break;
    }
    case 0xD3:
        out << "bus.output(0x" << std::setw(2) << static_cast<int>(image[pc + 1]) << ", cpu.a); cpu.pc = 0x"
            << std::setw(4) << (pc + 2) << "; cpu.cycles += 11; return;\n";
        break;
    case 0xD9:
        out << "{ const auto b = cpu.b; const auto c = cpu.c; const auto d = cpu.d; const auto e = cpu.e; "
            << "const auto h = cpu.h; const auto l = cpu.l; cpu.b = cpu.b_alt; cpu.c = cpu.c_alt; "
            << "cpu.d = cpu.d_alt; cpu.e = cpu.e_alt; cpu.h = cpu.h_alt; cpu.l = cpu.l_alt; "
            << "cpu.b_alt = b; cpu.c_alt = c; cpu.d_alt = d; cpu.e_alt = e; cpu.h_alt = h; cpu.l_alt = l; "
            << "cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 4; return; }\n";
        break;
    case 0xDB:
        out << "cpu.a = bus.input(0x" << std::setw(2) << static_cast<int>(image[pc + 1]) << "); cpu.pc = 0x"
            << std::setw(4) << (pc + 2) << "; cpu.cycles += 11; return;\n";
        break;
    case 0xE9:
        out << "cpu.pc = cpu.hl(); cpu.cycles += 4; return;\n";
        break;
    case 0xED:
        switch (image[static_cast<u16>(pc + 1)]) {
        case 0x43:
        case 0x53:
        case 0x63:
        case 0x73: {
            const u8 pair = static_cast<u8>((image[static_cast<u16>(pc + 1)] >> 4) & 0x03);
            const u16 address = make_u16(image[pc + 2], image[pc + 3]);
            out << "{ const auto value = " << rp_read_expr(pair) << "; bus.write(0x" << std::setw(4) << address
                << ", static_cast<sgrecomp::u8>(value & 0xff)); bus.write(0x" << std::setw(4)
                << static_cast<u16>(address + 1) << ", static_cast<sgrecomp::u8>((value >> 8) & 0xff)); cpu.pc = 0x"
                << std::setw(4) << (pc + 4) << "; cpu.cycles += 20; return; }\n";
            break;
        }
        case 0x45:
            out << "{ const auto lo = bus.read(cpu.sp); cpu.sp = static_cast<sgrecomp::u16>(cpu.sp + 1); "
                << "const auto hi = bus.read(cpu.sp); cpu.sp = static_cast<sgrecomp::u16>(cpu.sp + 1); "
                << "cpu.pc = sgrecomp::make_u16(lo, hi); cpu.iff1 = cpu.iff2; cpu.cycles += 14; return; }\n";
            break;
        case 0x4B:
        case 0x5B:
        case 0x6B:
        case 0x7B: {
            const u8 pair = static_cast<u8>((image[static_cast<u16>(pc + 1)] >> 4) & 0x03);
            const u16 address = make_u16(image[pc + 2], image[pc + 3]);
            out << "{ const auto lo = bus.read(0x" << std::setw(4) << address << "); const auto hi = bus.read(0x"
                << std::setw(4) << static_cast<u16>(address + 1) << "); ";
            emit_rp_write(out, pair, "sgrecomp::make_u16(lo, hi)");
            out << "cpu.pc = 0x" << std::setw(4) << (pc + 4) << "; cpu.cycles += 20; return; }\n";
            break;
        }
        case 0x4D:
            out << "{ const auto lo = bus.read(cpu.sp); cpu.sp = static_cast<sgrecomp::u16>(cpu.sp + 1); "
                << "const auto hi = bus.read(cpu.sp); cpu.sp = static_cast<sgrecomp::u16>(cpu.sp + 1); "
                << "cpu.pc = sgrecomp::make_u16(lo, hi); cpu.cycles += 14; return; }\n";
            break;
        case 0x46:
        case 0x66:
            out << "cpu.interrupt_mode = 0; cpu.pc = 0x" << std::setw(4) << (pc + 2) << "; cpu.cycles += 8; return;\n";
            break;
        case 0x56:
        case 0x76:
            out << "cpu.interrupt_mode = 1; cpu.pc = 0x" << std::setw(4) << (pc + 2) << "; cpu.cycles += 8; return;\n";
            break;
        case 0x5E:
        case 0x7E:
            out << "cpu.interrupt_mode = 2; cpu.pc = 0x" << std::setw(4) << (pc + 2) << "; cpu.cycles += 8; return;\n";
            break;
        case 0xA0:
            out << "{ const auto value = bus.read(cpu.hl()); bus.write(cpu.de(), value); "
                   "cpu.set_hl(static_cast<sgrecomp::u16>(cpu.hl() + 1)); "
                << "cpu.set_de(static_cast<sgrecomp::u16>(cpu.de() + 1)); "
                   "cpu.set_bc(static_cast<sgrecomp::u16>(cpu.bc() - 1)); "
                << "const auto sum = static_cast<sgrecomp::u8>(cpu.a + value); "
                << "cpu.f = static_cast<sgrecomp::u8>((cpu.f & 0xc1) | (sum & 0x08) | ((sum & 0x02) ? 0x20 : 0) | "
                   "(cpu.bc() != 0 ? 0x04 : 0)); cpu.pc = 0x"
                << std::setw(4) << (pc + 2) << "; cpu.cycles += 16; return; }\n";
            break;
        case 0xB0:
            out << "{ const auto value = bus.read(cpu.hl()); bus.write(cpu.de(), value); "
                   "cpu.set_hl(static_cast<sgrecomp::u16>(cpu.hl() + 1)); "
                << "cpu.set_de(static_cast<sgrecomp::u16>(cpu.de() + 1)); "
                   "cpu.set_bc(static_cast<sgrecomp::u16>(cpu.bc() - 1)); "
                << "const auto sum = static_cast<sgrecomp::u8>(cpu.a + value); "
                << "cpu.f = static_cast<sgrecomp::u8>((cpu.f & 0xc1) | (sum & 0x08) | ((sum & 0x02) ? 0x20 : 0) | "
                   "(cpu.bc() != 0 ? 0x04 : 0)); "
                << "if (cpu.bc() != 0) { cpu.pc = 0x" << std::setw(4) << pc
                << "; cpu.cycles += 21; } else { cpu.pc = 0x" << std::setw(4) << (pc + 2)
                << "; cpu.cycles += 16; } return; }\n";
            break;
        default:
            out << "sgrecomp::execute_one(cpu, bus); return;\n";
            break;
        }
        break;
    case 0xF3:
        out << "cpu.iff1 = false; cpu.iff2 = false; cpu.ei_pending = false; cpu.pc = 0x" << std::setw(4) << (pc + 1)
            << "; cpu.cycles += 4; return;\n";
        break;
    case 0xDD:
    case 0xFD:
        if (image[static_cast<u16>(pc + 1)] == 0xE1) {
            const std::string high = opcode == 0xDD ? "cpu.ixh" : "cpu.iyh";
            const std::string low = opcode == 0xDD ? "cpu.ixl" : "cpu.iyl";
            out << "{ const auto lo = bus.read(cpu.sp); cpu.sp = static_cast<sgrecomp::u16>(cpu.sp + 1); "
                << "const auto hi = bus.read(cpu.sp); cpu.sp = static_cast<sgrecomp::u16>(cpu.sp + 1); " << low
                << " = lo; " << high << " = hi; cpu.pc = 0x" << std::setw(4) << (pc + 2)
                << "; cpu.cycles += 14; return; }\n";
        } else if (image[static_cast<u16>(pc + 1)] == 0xE5) {
            const std::string value =
                opcode == 0xDD ? "sgrecomp::make_u16(cpu.ixl, cpu.ixh)" : "sgrecomp::make_u16(cpu.iyl, cpu.iyh)";
            emit_push16(out, value);
            out << "cpu.pc = 0x" << std::setw(4) << (pc + 2) << "; cpu.cycles += 15; return;\n";
        } else {
            out << "sgrecomp::execute_one(cpu, bus); return;\n";
        }
        break;
    case 0xFB:
        out << "cpu.ei_pending = true; cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 4; return;\n";
        break;
    case 0x76:
        out << "cpu.halted = true; cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 4; return;\n";
        break;
    default:
        if ((opcode & 0xC0) == 0x40 && opcode != 0x76) {
            const u8 dst = static_cast<u8>((opcode >> 3) & 0x07);
            const u8 src = static_cast<u8>(opcode & 0x07);
            emit_ld_reg_from_value(out, dst, read_reg_expr(src));
            out << "cpu.pc = 0x" << std::setw(4) << (pc + 1)
                << "; cpu.cycles += " << (((dst == 6) || (src == 6)) ? 7 : 4) << "; return;\n";
        } else if ((opcode & 0xCF) == 0x03) {
            const u8 pair = static_cast<u8>((opcode >> 4) & 0x03);
            emit_rp_write(out, pair, "static_cast<sgrecomp::u16>(" + rp_read_expr(pair) + " + 1)");
            out << "cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 6; return;\n";
        } else if ((opcode & 0xCF) == 0x09) {
            const u8 pair = static_cast<u8>((opcode >> 4) & 0x03);
            out << "{ const auto lhs = cpu.hl(); const auto rhs = " << rp_read_expr(pair)
                << "; const auto sum = static_cast<unsigned>(lhs + rhs); const auto result = "
                   "static_cast<sgrecomp::u16>(sum); "
                << "cpu.f = static_cast<sgrecomp::u8>((cpu.f & 0x84) | ((result >> 8) & 0x28) | (((lhs ^ rhs ^ result) "
                   "& 0x1000) ? 0x10 : 0) | "
                << "(sum > 0xffff ? 0x01 : 0)); cpu.set_hl(result); cpu.pc = 0x" << std::setw(4) << (pc + 1)
                << "; cpu.cycles += 11; return; }\n";
        } else if ((opcode & 0xCF) == 0x0B) {
            const u8 pair = static_cast<u8>((opcode >> 4) & 0x03);
            emit_rp_write(out, pair, "static_cast<sgrecomp::u16>(" + rp_read_expr(pair) + " - 1)");
            out << "cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 6; return;\n";
        } else if ((opcode & 0xC7) == 0x04) {
            const u8 reg = static_cast<u8>((opcode >> 3) & 0x07);
            emit_ld_reg_from_value(out, reg, "sgrecomp_inc8(cpu, " + read_reg_expr(reg) + ")");
            out << "cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += " << (reg == 6 ? 11 : 4)
                << "; return;\n";
        } else if ((opcode & 0xC7) == 0x05) {
            const u8 reg = static_cast<u8>((opcode >> 3) & 0x07);
            emit_ld_reg_from_value(out, reg, "sgrecomp_dec8(cpu, " + read_reg_expr(reg) + ")");
            out << "cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += " << (reg == 6 ? 11 : 4)
                << "; return;\n";
        } else if ((opcode & 0xC7) == 0x06) {
            const u8 reg = static_cast<u8>((opcode >> 3) & 0x07);
            std::ostringstream value;
            value << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(image[pc + 1]);
            emit_ld_reg_from_value(out, reg, value.str());
            out << "cpu.pc = 0x" << std::setw(4) << (pc + 2) << "; cpu.cycles += " << (reg == 6 ? 10 : 7)
                << "; return;\n";
        } else if ((opcode & 0xC0) == 0x80) {
            const u8 group = static_cast<u8>((opcode >> 3) & 0x07);
            const u8 src = static_cast<u8>(opcode & 0x07);
            emit_alu(out, group, read_reg_expr(src));
            out << "cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += " << (src == 6 ? 7 : 4)
                << "; return;\n";
        } else if ((opcode & 0xC7) == 0xC0) {
            const u8 condition = static_cast<u8>((opcode >> 3) & 0x07);
            out << "if (" << condition_expr(condition) << ") { const auto lo = bus.read(cpu.sp); "
                << "cpu.sp = static_cast<sgrecomp::u16>(cpu.sp + 1); const auto hi = bus.read(cpu.sp); "
                << "cpu.sp = static_cast<sgrecomp::u16>(cpu.sp + 1); cpu.pc = sgrecomp::make_u16(lo, hi); "
                << "cpu.cycles += 11; } else { cpu.pc = 0x" << std::setw(4) << (pc + 1)
                << "; cpu.cycles += 5; } return;\n";
        } else if ((opcode & 0xC7) == 0xC2) {
            const u8 condition = static_cast<u8>((opcode >> 3) & 0x07);
            const u16 target = make_u16(image[pc + 1], image[pc + 2]);
            out << "if (" << condition_expr(condition) << ") { cpu.pc = 0x" << std::setw(4) << target
                << "; } else { cpu.pc = 0x" << std::setw(4) << (pc + 3) << "; } cpu.cycles += 10; return;\n";
        } else if ((opcode & 0xC7) == 0xC4) {
            const u8 condition = static_cast<u8>((opcode >> 3) & 0x07);
            const u16 target = make_u16(image[pc + 1], image[pc + 2]);
            out << "if (" << condition_expr(condition) << ") { ";
            emit_push16(
                out, "0x" + [&] {
                    std::ostringstream ss;
                    ss << std::hex << std::setw(4) << std::setfill('0') << static_cast<u16>(pc + 3);
                    return ss.str();
                }());
            out << "cpu.pc = 0x" << std::setw(4) << target << "; cpu.cycles += 17; } else { cpu.pc = 0x" << std::setw(4)
                << (pc + 3) << "; cpu.cycles += 10; } return;\n";
        } else if ((opcode & 0xCF) == 0xC1) {
            const u8 pair = static_cast<u8>((opcode >> 4) & 0x03);
            emit_pop16_to(out, qq_write_call(pair));
            out << "cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 10; return; }\n";
        } else if ((opcode & 0xCF) == 0xC5) {
            const u8 pair = static_cast<u8>((opcode >> 4) & 0x03);
            emit_push16(out, qq_read_expr(pair));
            out << "cpu.pc = 0x" << std::setw(4) << (pc + 1) << "; cpu.cycles += 11; return;\n";
        } else if ((opcode & 0xC7) == 0xC7) {
            emit_push16(
                out, "0x" + [&] {
                    std::ostringstream ss;
                    ss << std::hex << std::setw(4) << std::setfill('0') << static_cast<u16>(pc + 1);
                    return ss.str();
                }());
            out << "cpu.pc = 0x" << std::setw(4) << static_cast<u16>(opcode & 0x38) << "; cpu.cycles += 11; return;\n";
        } else {
            out << "sgrecomp::execute_one(cpu, bus); return;\n";
        }
        break;
    }
}

void emit_case(std::ostream& out, const std::array<u8, 0x10000>& image, u16 pc) {
    out << "    case 0x" << std::hex << std::setw(4) << std::setfill('0') << pc << ": ";
    if (is_direct_emit_supported(image, pc)) {
        const u8 opcode = image[pc];
        const int m1_fetches = opcode == 0xCB || opcode == 0xED || opcode == 0xDD || opcode == 0xFD ? 2 : 1;
        out << "cpu.r = static_cast<sgrecomp::u8>((cpu.r & 0x80) | ((cpu.r + " << m1_fetches << ") & 0x7f)); ";
    }
    emit_instruction_body(out, image, pc);
}

std::string block_function_name(u16 pc) {
    std::ostringstream out;
    out << "sgrecomp_block_" << std::hex << std::setw(4) << std::setfill('0') << pc;
    return out.str();
}

void emit_block_function(std::ostream& out, const std::array<u8, 0x10000>& image, const BasicBlock& block) {
    out << "void " << block_function_name(block.start) << "(sgrecomp::Z80State& cpu, sgrecomp::Bus& bus) {\n";
    out << "    switch (cpu.pc) {\n";
    for (const auto& instruction : block.instructions) {
        emit_case(out, image, instruction.decoded.pc);
    }
    out << "    default: sgrecomp::execute_one(cpu, bus); return;\n";
    out << "    }\n";
    out << "}\n\n";
}

bool is_block_start(std::span<const BasicBlock> blocks, u16 pc) {
    return std::find_if(blocks.begin(), blocks.end(), [&](const BasicBlock& block) { return block.start == pc; }) !=
           blocks.end();
}

void generate_cpp(const std::filesystem::path& output,
                  const std::array<u8, 0x10000>& image,
                  std::size_t limit,
                  std::span<const BasicBlock> blocks) {
    std::ofstream out(output);
    if (!out) {
        throw std::runtime_error("cannot open output file");
    }

    out << "#include \"sgrecomp/bus.h\"\n";
    out << "#include \"sgrecomp/z80.h\"\n\n";
    out << "#include <array>\n\n";
    out << "namespace {\n";
    out << "constexpr std::array<sgrecomp::u8, " << std::dec << limit << "> kRom = {\n";
    for (std::size_t i = 0; i < limit; ++i) {
        if (i % 16 == 0) {
            out << "    ";
        }
        out << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(image[i]);
        if (i + 1 != limit) {
            out << ", ";
        }
        if (i % 16 == 15 || i + 1 == limit) {
            out << "\n";
        }
    }
    out << "};\n";
    out << "constexpr sgrecomp::u8 kFlagC = 0x01;\n";
    out << "constexpr sgrecomp::u8 kFlagN = 0x02;\n";
    out << "constexpr sgrecomp::u8 kFlagPV = 0x04;\n";
    out << "constexpr sgrecomp::u8 kFlagX = 0x08;\n";
    out << "constexpr sgrecomp::u8 kFlagH = 0x10;\n";
    out << "constexpr sgrecomp::u8 kFlagY = 0x20;\n";
    out << "constexpr sgrecomp::u8 kFlagZ = 0x40;\n";
    out << "constexpr sgrecomp::u8 kFlagS = 0x80;\n\n";
    out << "sgrecomp::u8 sgrecomp_parity(sgrecomp::u8 value) {\n";
    out << "    value ^= static_cast<sgrecomp::u8>(value >> 4);\n";
    out << "    value &= 0x0f;\n";
    out << "    return static_cast<sgrecomp::u8>((0x6996 >> value) & 1) ? 0 : kFlagPV;\n";
    out << "}\n\n";
    out << "sgrecomp::u8 sgrecomp_add8(sgrecomp::Z80State& cpu, sgrecomp::u8 lhs, sgrecomp::u8 rhs) {\n";
    out << "    const unsigned sum = lhs + rhs;\n";
    out << "    const auto result = static_cast<sgrecomp::u8>(sum);\n";
    out << "    cpu.f = static_cast<sgrecomp::u8>((result & (kFlagS | kFlagY | kFlagX)) | (result == 0 ? kFlagZ : 0) "
           "|\n";
    out << "        (((lhs ^ rhs ^ result) & 0x10) ? kFlagH : 0) |\n";
    out << "        (((~(lhs ^ rhs) & (lhs ^ result)) & 0x80) ? kFlagPV : 0) |\n";
    out << "        (sum > 0xff ? kFlagC : 0));\n";
    out << "    return result;\n";
    out << "}\n\n";
    out << "sgrecomp::u8 sgrecomp_adc8(sgrecomp::Z80State& cpu, sgrecomp::u8 lhs, sgrecomp::u8 rhs) {\n";
    out << "    const auto carry = static_cast<sgrecomp::u8>(cpu.f & kFlagC);\n";
    out << "    const unsigned sum = lhs + rhs + carry;\n";
    out << "    const auto result = static_cast<sgrecomp::u8>(sum);\n";
    out << "    cpu.f = static_cast<sgrecomp::u8>((result & (kFlagS | kFlagY | kFlagX)) | (result == 0 ? kFlagZ : 0) "
           "|\n";
    out << "        (((lhs ^ rhs ^ result) & 0x10) ? kFlagH : 0) |\n";
    out << "        (((~(lhs ^ rhs) & (lhs ^ result)) & 0x80) ? kFlagPV : 0) |\n";
    out << "        (sum > 0xff ? kFlagC : 0));\n";
    out << "    return result;\n";
    out << "}\n\n";
    out << "sgrecomp::u8 sgrecomp_sub8(sgrecomp::Z80State& cpu, sgrecomp::u8 lhs, sgrecomp::u8 rhs) {\n";
    out << "    const unsigned diff = lhs - rhs;\n";
    out << "    const auto result = static_cast<sgrecomp::u8>(diff);\n";
    out << "    cpu.f = static_cast<sgrecomp::u8>(kFlagN | (result & (kFlagS | kFlagY | kFlagX)) | (result == 0 ? "
           "kFlagZ : 0) |\n";
    out << "        (((lhs ^ rhs ^ result) & 0x10) ? kFlagH : 0) |\n";
    out << "        ((((lhs ^ rhs) & (lhs ^ result)) & 0x80) ? kFlagPV : 0) |\n";
    out << "        (diff & 0x100 ? kFlagC : 0));\n";
    out << "    return result;\n";
    out << "}\n\n";
    out << "sgrecomp::u8 sgrecomp_sbc8(sgrecomp::Z80State& cpu, sgrecomp::u8 lhs, sgrecomp::u8 rhs) {\n";
    out << "    const auto carry = static_cast<sgrecomp::u8>(cpu.f & kFlagC);\n";
    out << "    const unsigned diff = lhs - rhs - carry;\n";
    out << "    const auto result = static_cast<sgrecomp::u8>(diff);\n";
    out << "    cpu.f = static_cast<sgrecomp::u8>(kFlagN | (result & (kFlagS | kFlagY | kFlagX)) | (result == 0 ? "
           "kFlagZ : 0) |\n";
    out << "        (((lhs ^ rhs ^ result) & 0x10) ? kFlagH : 0) |\n";
    out << "        ((((lhs ^ rhs) & (lhs ^ result)) & 0x80) ? kFlagPV : 0) |\n";
    out << "        (diff & 0x100 ? kFlagC : 0));\n";
    out << "    return result;\n";
    out << "}\n\n";
    out << "sgrecomp::u8 sgrecomp_and8(sgrecomp::Z80State& cpu, sgrecomp::u8 lhs, sgrecomp::u8 rhs) {\n";
    out << "    const auto result = static_cast<sgrecomp::u8>(lhs & rhs);\n";
    out << "    cpu.f = static_cast<sgrecomp::u8>((result & (kFlagS | kFlagY | kFlagX)) | (result == 0 ? kFlagZ : 0) | "
           "kFlagH | sgrecomp_parity(result));\n";
    out << "    return result;\n";
    out << "}\n\n";
    out << "sgrecomp::u8 sgrecomp_xor8(sgrecomp::Z80State& cpu, sgrecomp::u8 lhs, sgrecomp::u8 rhs) {\n";
    out << "    const auto result = static_cast<sgrecomp::u8>(lhs ^ rhs);\n";
    out << "    cpu.f = static_cast<sgrecomp::u8>((result & (kFlagS | kFlagY | kFlagX)) | (result == 0 ? kFlagZ : 0) | "
           "sgrecomp_parity(result));\n";
    out << "    return result;\n";
    out << "}\n\n";
    out << "sgrecomp::u8 sgrecomp_or8(sgrecomp::Z80State& cpu, sgrecomp::u8 lhs, sgrecomp::u8 rhs) {\n";
    out << "    const auto result = static_cast<sgrecomp::u8>(lhs | rhs);\n";
    out << "    cpu.f = static_cast<sgrecomp::u8>((result & (kFlagS | kFlagY | kFlagX)) | (result == 0 ? kFlagZ : 0) | "
           "sgrecomp_parity(result));\n";
    out << "    return result;\n";
    out << "}\n\n";
    out << "void sgrecomp_cp8(sgrecomp::Z80State& cpu, sgrecomp::u8 lhs, sgrecomp::u8 rhs) {\n";
    out << "    (void)sgrecomp_sub8(cpu, lhs, rhs);\n";
    out << "}\n\n";
    out << "sgrecomp::u8 sgrecomp_inc8(sgrecomp::Z80State& cpu, sgrecomp::u8 value) {\n";
    out << "    const auto result = static_cast<sgrecomp::u8>(value + 1);\n";
    out << "    cpu.f = static_cast<sgrecomp::u8>((cpu.f & kFlagC) | (result & (kFlagS | kFlagY | kFlagX)) | (result "
           "== 0 ? kFlagZ : 0) |\n";
    out << "        (((value ^ result) & 0x10) ? kFlagH : 0) | (value == 0x7f ? kFlagPV : 0));\n";
    out << "    return result;\n";
    out << "}\n\n";
    out << "sgrecomp::u8 sgrecomp_dec8(sgrecomp::Z80State& cpu, sgrecomp::u8 value) {\n";
    out << "    const auto result = static_cast<sgrecomp::u8>(value - 1);\n";
    out << "    cpu.f = static_cast<sgrecomp::u8>((cpu.f & kFlagC) | kFlagN | (result & (kFlagS | kFlagY | kFlagX)) | "
           "(result == 0 ? kFlagZ : 0) |\n";
    out << "        (((value ^ result) & 0x10) ? kFlagH : 0) | (value == 0x80 ? kFlagPV : 0));\n";
    out << "    return result;\n";
    out << "}\n";
    out << "} // namespace\n\n";
    for (const auto& block : blocks) {
        emit_block_function(out, image, block);
    }
    out << "extern \"C\" void sgrecomp_load_rom(sgrecomp::Bus& bus) {\n";
    out << "    bus.load_rom(kRom);\n";
    out << "}\n\n";
    out << "extern \"C\" void sgrecomp_run_instruction(sgrecomp::Z80State& cpu, sgrecomp::Bus& bus) {\n";
    out << "    if (cpu.halted) { if (cpu.ei_pending) { cpu.iff1 = true; cpu.iff2 = true; cpu.ei_pending = false; } "
        << "cpu.r = static_cast<sgrecomp::u8>((cpu.r & 0x80) | ((cpu.r + 1) & 0x7f)); cpu.cycles += 4; return; }\n";
    out << "    if (cpu.ei_pending) { cpu.iff1 = true; cpu.iff2 = true; cpu.ei_pending = false; }\n";
    out << "    switch (cpu.pc) {\n";
    for (const auto& block : blocks) {
        out << "    case 0x" << std::hex << std::setw(4) << std::setfill('0') << block.start << ": "
            << block_function_name(block.start) << "(cpu, bus); return;\n";
    }
    for (u16 pc = 0; pc < limit && pc < 0xC000;) {
        const auto insn = decode_z80(image, pc);
        if (!is_block_start(blocks, pc)) {
            emit_case(out, image, pc);
        }
        pc = static_cast<u16>(pc + insn.size);
    }
    out << "    default: sgrecomp::execute_one(cpu, bus); return;\n";
    out << "    }\n";
    out << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options opts = parse_args(argc, argv);
        auto rom = normalize_rom_payload(read_file(opts.input));
        if (opts.header_write_mode != HeaderWriteMode::None) {
            CartridgeHeaderBuildOptions header_options;
            header_options.preserve_existing = opts.header_write_mode == HeaderWriteMode::Rebuild;
            header_options.region = opts.header_region;
            header_options.product_code = opts.header_product_code;
            header_options.version = opts.header_version;
            const CartridgeHeaderInfo header = rebuild_cartridge_header(rom, header_options);
            write_binary_file(opts.header_output, rom);
            std::cout << (header_options.preserve_existing ? "rebuilt" : "generated") << " cartridge header at 0x"
                      << std::hex << header.offset << ", checksum 0x" << std::setw(4) << std::setfill('0')
                      << header.stored_checksum << std::dec << "\nwrote " << opts.header_output.string() << "\n";
            return 0;
        }
        const std::optional<std::vector<u8>> bios =
            opts.bios.empty() ? std::optional<std::vector<u8>>{} : std::optional<std::vector<u8>>{read_file(opts.bios)};
        const auto image =
            image_for_decode(opts.model, opts.mapper, rom, opts.disassemble_only && bios ? &*bios : nullptr);
        const std::size_t configured_limit = std::min<std::size_t>(opts.max_static_bytes.value_or(0xC000), 0xC000);
        const std::size_t limit = std::min<std::size_t>(rom.size(), configured_limit);
        if (!opts.dump_analysis.empty()) {
            const auto entry_points = default_entry_points(limit);
            const auto blocks = discover_basic_blocks(image, limit, entry_points);
            write_analysis_report(opts.dump_analysis, image, rom, limit, entry_points, blocks);
            const CartridgeHeaderInfo header = analyze_cartridge_header(rom);
            std::cout << "analysis dumped: " << opts.dump_analysis.string() << "\n";
            std::cout << "rom header: " << (header.found ? "found" : "not found") << "\n";
        }

        if (opts.disassemble_only) {
            disassemble(image, limit);
        } else if (opts.run_smoke) {
            run_smoke(opts.model, rom, bios ? &*bios : nullptr, opts.max_steps, opts.trace, opts);
        } else if (opts.run_host) {
            run_host(opts.model, rom, bios ? &*bios : nullptr, opts);
        } else {
            const auto entry_points = default_entry_points(limit);
            const auto blocks = discover_basic_blocks(image, limit, entry_points);
            generate_cpp(opts.output, image, limit, blocks);
            std::cout << "generated " << opts.output.string() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "sgrecomp: " << e.what() << "\n";
        print_usage();
        return 1;
    }
    return 0;
}
