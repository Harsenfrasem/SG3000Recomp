#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "sgrecomp/enhancements.h"
#include "sgrecomp/cartridge.h"
#include "sgrecomp/game_profile.h"
#include "sgrecomp/game_library.h"
#include "sgrecomp/host_runtime.h"
#include "sgrecomp/media_io.h"
#include "sgrecomp/recent_games.h"
#include "sgrecomp/save_state.h"

#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace sgrecomp;

struct Options {
    std::filesystem::path rom;
    std::filesystem::path bios;
    std::filesystem::path load_sram;
    std::filesystem::path save_sram;
    std::filesystem::path load_state;
    std::filesystem::path save_state;
    std::filesystem::path profile;
    ConsoleModel model = ConsoleModel::SMS;
    CartridgeMapper mapper = CartridgeMapper::Auto;
    EnhancementConfig enhancements;
    HostVideoStandard video_standard = HostVideoStandard::Ntsc;
    int scale = 3;
    bool audio = true;
    bool overlay = true;
    bool print_hash = false;
    bool force_state = false;
    int audio_latency_ms = 80;
    u32 audio_sample_rate = 44100;
    std::size_t quit_after_frames = 0;
    bool gui_launch = false;
    bool show_status_window = false;
};

class Win32Audio {
  public:
    struct Stats {
        std::size_t queued_buffers = 0;
        std::size_t queued_sample_frames = 0;
        u64 submitted_buffers = 0;
        u64 dropped_buffers = 0;
        u64 underruns = 0;
    };

    Win32Audio() = default;
    Win32Audio(const Win32Audio&) = delete;
    Win32Audio& operator=(const Win32Audio&) = delete;

    ~Win32Audio() {
        close();
    }

    bool open(u32 sample_rate, int target_latency_ms) {
        sample_rate_ = sample_rate;
        target_latency_ms_ = std::max(10, target_latency_ms);
        target_latency_frames_ =
            static_cast<std::size_t>((static_cast<u64>(sample_rate_) * static_cast<u64>(target_latency_ms_)) / 1000);
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = sample_rate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>((format.nChannels * format.wBitsPerSample) / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

        if (waveOutOpen(&device_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            device_ = nullptr;
            return false;
        }
        return true;
    }

    void submit(std::span<const s16> interleaved_stereo) {
        if (device_ == nullptr || interleaved_stereo.empty()) {
            return;
        }

        cleanup_completed_buffers();
        const std::size_t sample_frames = interleaved_stereo.size() / 2;
        if (primed_ && stats_.queued_sample_frames < sample_frames) {
            ++stats_.underruns;
        }
        if (!primed_) {
            while (stats_.queued_sample_frames < target_latency_frames_) {
                if (!queue_samples(silence_for(interleaved_stereo.size()))) {
                    break;
                }
            }
            primed_ = true;
        }
        if (!queue_samples(interleaved_stereo)) {
            ++stats_.dropped_buffers;
        }
    }

    Stats stats() const {
        return stats_;
    }

    u32 sample_rate() const {
        return sample_rate_;
    }

    int target_latency_ms() const {
        return target_latency_ms_;
    }

    int queued_latency_ms() const {
        if (sample_rate_ == 0) {
            return 0;
        }
        return static_cast<int>((static_cast<u64>(stats_.queued_sample_frames) * 1000) / sample_rate_);
    }

    int volume_percent() const {
        return volume_percent_;
    }

    bool muted() const {
        return muted_;
    }

    void set_volume_percent(int volume_percent) {
        volume_percent_ = std::clamp(volume_percent, 0, 100);
        apply_volume();
    }

    void set_muted(bool muted) {
        muted_ = muted;
        apply_volume();
    }

    void set_paused(bool paused) {
        if (device_ == nullptr) {
            return;
        }
        if (paused) {
            waveOutPause(device_);
        } else {
            waveOutRestart(device_);
        }
    }

    void flush() {
        if (device_ == nullptr) {
            return;
        }
        waveOutReset(device_);
        for (auto& buffer : buffers_) {
            if (buffer.prepared) {
                waveOutUnprepareHeader(device_, &buffer.header, sizeof(buffer.header));
                buffer.prepared = false;
                buffer.sample_frames = 0;
            }
        }
        stats_.queued_buffers = 0;
        stats_.queued_sample_frames = 0;
        primed_ = false;
    }

    void cleanup_completed_buffers() {
        if (device_ == nullptr) {
            return;
        }
        for (auto& buffer : buffers_) {
            if (buffer.prepared && (buffer.header.dwFlags & WHDR_DONE) != 0) {
                waveOutUnprepareHeader(device_, &buffer.header, sizeof(buffer.header));
                buffer.prepared = false;
                stats_.queued_buffers = stats_.queued_buffers == 0 ? 0 : stats_.queued_buffers - 1;
                stats_.queued_sample_frames = buffer.sample_frames > stats_.queued_sample_frames
                                                  ? 0
                                                  : stats_.queued_sample_frames - buffer.sample_frames;
                buffer.sample_frames = 0;
            }
        }
    }

    void close() {
        if (device_ == nullptr) {
            return;
        }
        waveOutReset(device_);
        for (auto& buffer : buffers_) {
            if (buffer.prepared) {
                waveOutUnprepareHeader(device_, &buffer.header, sizeof(buffer.header));
                buffer.prepared = false;
                buffer.sample_frames = 0;
            }
        }
        stats_.queued_buffers = 0;
        stats_.queued_sample_frames = 0;
        waveOutClose(device_);
        device_ = nullptr;
    }

  private:
    struct AudioBuffer {
        WAVEHDR header{};
        std::vector<s16> samples;
        std::size_t sample_frames = 0;
        bool prepared = false;
    };

    HWAVEOUT device_ = nullptr;
    std::array<AudioBuffer, 8> buffers_{};
    Stats stats_{};
    u32 sample_rate_ = 44100;
    int target_latency_ms_ = 80;
    std::size_t target_latency_frames_ = 3528;
    int volume_percent_ = 100;
    bool muted_ = false;
    bool primed_ = false;
    std::vector<s16> silence_;

    std::span<const s16> silence_for(std::size_t samples) {
        silence_.assign(samples, 0);
        return silence_;
    }

    bool queue_samples(std::span<const s16> interleaved_stereo) {
        for (auto& buffer : buffers_) {
            if (!buffer.prepared) {
                buffer.samples.assign(interleaved_stereo.begin(), interleaved_stereo.end());
                buffer.sample_frames = buffer.samples.size() / 2;
                buffer.header = {};
                buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.samples.data());
                buffer.header.dwBufferLength = static_cast<DWORD>(buffer.samples.size() * sizeof(s16));
                if (waveOutPrepareHeader(device_, &buffer.header, sizeof(buffer.header)) != MMSYSERR_NOERROR) {
                    return false;
                }
                buffer.prepared = true;
                if (waveOutWrite(device_, &buffer.header, sizeof(buffer.header)) != MMSYSERR_NOERROR) {
                    waveOutUnprepareHeader(device_, &buffer.header, sizeof(buffer.header));
                    buffer.prepared = false;
                    buffer.sample_frames = 0;
                    return false;
                }
                ++stats_.queued_buffers;
                stats_.queued_sample_frames += buffer.sample_frames;
                ++stats_.submitted_buffers;
                return true;
            }
        }
        return false;
    }

    void apply_volume() {
        if (device_ == nullptr) {
            return;
        }
        const DWORD channel = muted_ ? 0 : static_cast<DWORD>((volume_percent_ * 0xFFFF) / 100);
        waveOutSetVolume(device_, channel | (channel << 16));
    }
};

enum class ControlAction : std::size_t {
    Up,
    Down,
    Left,
    Right,
    Button1,
    Button2,
    Pause,
    Count,
};

struct InputBindings {
    std::array<UINT, static_cast<std::size_t>(ControlAction::Count)> keys{
        VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'Z', 'X', VK_RETURN};
};

struct AppState {
    std::unique_ptr<HostRuntime> host;
    std::unique_ptr<Win32Audio> audio;
    HostInputState input;
    HostFrameResult last_frame;
    std::string rom_hash;
    std::string profile_name;
    std::filesystem::path quick_state_path;
    std::filesystem::path current_rom_path;
    std::filesystem::path current_sram_path;
    std::vector<std::filesystem::path> recent_games;
    std::vector<GameLibraryEntry> game_library;
    Options session_options;
    InputBindings bindings;
    std::optional<ControlAction> pending_binding;
    HMENU controls_menu = nullptr;
    HMENU scale_menu = nullptr;
    HMENU recent_menu = nullptr;
    HMENU library_menu = nullptr;
    HWND status_window = nullptr;
    HWND status_text = nullptr;
    SaveStateMetadata state_metadata;
    std::string status_message;
    BITMAPINFO bitmap_info{};
    bool running = true;
    bool emulation_paused = false;
    bool overlay_enabled = true;
    bool compatibility_warning_acknowledged = false;
    bool has_rom = false;
    int window_scale = 3;
    double fps = 0.0;
    u64 rendered_frames = 0;
    std::size_t quit_after_frames = 0;
    std::vector<s16> recorded_audio;
    u32 recorded_audio_sample_rate = 44100;
    bool audio_recording = false;
    std::chrono::steady_clock::time_point fps_window_start = std::chrono::steady_clock::now();
    u64 fps_window_frames = 0;
};

struct GraphicalSettings {
    bool overlay = true;
    bool enhanced_mode = false;
    bool reduce_flicker = false;
    bool disable_sprite_limit = false;
    bool enable_ym2612 = false;
    bool muted = false;
    int volume_percent = 100;
    int window_scale = 3;
    InputBindings bindings;
};

enum MenuCommand : UINT {
    MenuFileExit = 1000,
    MenuFileOpenRom,
    MenuFileSelectBios,
    MenuFileClearBios,
    MenuFileSaveState,
    MenuFileLoadState,
    MenuFileScreenshot,
    MenuAudioStartRecording,
    MenuAudioStopAndSave,
    MenuAudioSaveLast,
    MenuAudioClear,
    MenuEmulationPause,
    MenuEmulationReset,
    MenuModeAccurate,
    MenuModeEnhanced,
    MenuEnhancementReduceFlicker,
    MenuEnhancementDisableSpriteLimit,
    MenuEnhancementYm2612,
    MenuViewOverlay,
    MenuViewStatus,
    MenuHelpControls,
    MenuRecentFirst = 1100,
    MenuRecentLast = MenuRecentFirst + 9,
    MenuScaleFirst = 1200,
    MenuScaleLast = MenuScaleFirst + 5,
    MenuControlFirst = 1300,
    MenuControlLast = MenuControlFirst + static_cast<UINT>(ControlAction::Count) - 1,
    MenuControlReset,
    MenuProfileMapperFirst = 1400,
    MenuProfileMapperLast = MenuProfileMapperFirst + 5,
    MenuProfileVideoNtsc,
    MenuProfileVideoPal,
    MenuProfileSave,
    MenuProfileRemove,
    MenuLibraryFirst = 1500,
    MenuLibraryLast = MenuLibraryFirst + 49,
    MenuLibrarySetAlias,
    MenuLibraryClearAlias,
};

constexpr std::array<CartridgeMapper, 6> graphical_profile_mappers{
    CartridgeMapper::Auto,
    CartridgeMapper::Plain,
    CartridgeMapper::SMapper,
    CartridgeMapper::CMapper,
    CartridgeMapper::KMapper,
    CartridgeMapper::K8KMapper,
};

std::vector<u8> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open input file");
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<u8> normalize_rom_payload(std::vector<u8> rom) {
    if (rom.size() > 512 && (rom.size() % 0x4000) == 512) {
        rom.erase(rom.begin(), rom.begin() + 512);
    }
    return rom;
}

CartridgeMapper parse_mapper(std::string text) {
    return cartridge_mapper_from_name(text);
}

HostVideoStandard parse_video_standard(std::string text) {
    std::transform(
        text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (text == "ntsc") {
        return HostVideoStandard::Ntsc;
    }
    if (text == "pal") {
        return HostVideoStandard::Pal;
    }
    throw std::runtime_error("unknown video standard: " + text);
}

void write_binary_file(const std::filesystem::path& path, std::span<const u8> bytes) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open output file");
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::filesystem::path graphical_user_data_root() {
    std::array<wchar_t, 32768> value{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), static_cast<DWORD>(value.size()));
    if (length > 0 && length < value.size()) {
        return std::filesystem::path(value.data()) / L"SG3000Recomp";
    }
    return std::filesystem::temp_directory_path() / L"SG3000Recomp";
}

bool parse_setting_bool(const std::string& value, bool fallback) {
    if (value == "1" || value == "true") {
        return true;
    }
    if (value == "0" || value == "false") {
        return false;
    }
    return fallback;
}

constexpr std::array<const char*, static_cast<std::size_t>(ControlAction::Count)> control_setting_names{
    "key_up", "key_down", "key_left", "key_right", "key_button1", "key_button2", "key_pause"};

GraphicalSettings load_graphical_settings(const std::filesystem::path& path) {
    GraphicalSettings settings;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "overlay") {
            settings.overlay = parse_setting_bool(value, settings.overlay);
        } else if (key == "enhanced_mode") {
            settings.enhanced_mode = parse_setting_bool(value, settings.enhanced_mode);
        } else if (key == "reduce_flicker") {
            settings.reduce_flicker = parse_setting_bool(value, settings.reduce_flicker);
        } else if (key == "disable_sprite_limit") {
            settings.disable_sprite_limit = parse_setting_bool(value, settings.disable_sprite_limit);
        } else if (key == "enable_ym2612") {
            settings.enable_ym2612 = parse_setting_bool(value, settings.enable_ym2612);
        } else if (key == "muted") {
            settings.muted = parse_setting_bool(value, settings.muted);
        } else if (key == "volume_percent") {
            try {
                settings.volume_percent = std::clamp(std::stoi(value), 0, 100);
            } catch (const std::exception&) {
                // Keep the default when a local setting was edited incorrectly.
            }
        } else if (key == "window_scale") {
            try {
                settings.window_scale = std::clamp(std::stoi(value), 1, 6);
            } catch (const std::exception&) {
                // Keep the default when a local setting was edited incorrectly.
            }
        } else {
            for (std::size_t index = 0; index < control_setting_names.size(); ++index) {
                if (key == control_setting_names[index]) {
                    try {
                        settings.bindings.keys[index] = static_cast<UINT>(std::clamp(std::stoi(value), 1, 255));
                    } catch (const std::exception&) {
                        // Keep the default when a local setting was edited incorrectly.
                    }
                    break;
                }
            }
        }
    }
    const InputBindings defaults;
    for (std::size_t index = 0; index < settings.bindings.keys.size(); ++index) {
        const auto duplicate = std::find(
            settings.bindings.keys.begin(), settings.bindings.keys.begin() + index, settings.bindings.keys[index]);
        if (duplicate != settings.bindings.keys.begin() + index) {
            const auto replacement = std::find_if(defaults.keys.begin(), defaults.keys.end(), [&](UINT candidate) {
                return std::find(settings.bindings.keys.begin(), settings.bindings.keys.begin() + index, candidate) ==
                       settings.bindings.keys.begin() + index;
            });
            settings.bindings.keys[index] =
                replacement == defaults.keys.end() ? static_cast<UINT>('A' + index) : *replacement;
        }
    }
    return settings;
}

void save_graphical_settings(const std::filesystem::path& path,
                             const AppState& app,
                             const GraphicalSettings& previous) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        throw std::runtime_error("cannot save graphical settings");
    }
    const auto& enhancements = app.host->console().enhancements();
    const bool muted = app.audio ? app.audio->muted() : previous.muted;
    const int volume = app.audio ? app.audio->volume_percent() : previous.volume_percent;
    file << "version=4\n"
         << "overlay=" << (app.overlay_enabled ? 1 : 0) << "\n"
         << "enhanced_mode=" << (enhancements.mode == RuntimeMode::Enhanced ? 1 : 0) << "\n"
         << "reduce_flicker=" << (enhancements.reduce_flicker ? 1 : 0) << "\n"
         << "disable_sprite_limit=" << (enhancements.disable_sprite_limit ? 1 : 0) << "\n"
         << "enable_ym2612=" << (enhancements.enable_ym2612 ? 1 : 0) << "\n"
         << "muted=" << (muted ? 1 : 0) << "\n"
         << "volume_percent=" << volume << "\n"
         << "window_scale=" << app.window_scale << "\n";
    for (std::size_t index = 0; index < control_setting_names.size(); ++index) {
        file << control_setting_names[index] << '=' << app.bindings.keys[index] << '\n';
    }
}

std::string hash_file_stem(std::string hash) {
    std::replace_if(
        hash.begin(),
        hash.end(),
        [](char ch) { return !(std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '-' || ch == '_'); },
        '-');
    return hash;
}

void print_usage() {
    std::cout
        << "usage: sgrecomp_host                         (open an idle graphical frontend)\n"
        << "       sgrecomp_host --gui                   (open an idle graphical frontend)\n"
        << "       sgrecomp_host <rom.sms|rom.sg> [--bios bios.sms] [--model sms|sg3000] [--mapper "
           "auto|plain|smapper|cmapper|kmapper|k8k]\n"
        << "                    [--video-standard ntsc|pal]\n"
        << "                    [--scale n] [--mute] [--no-overlay] [--audio-latency-ms n] [--audio-sample-rate hz]\n"
        << "                    [--load-sram save.sav] [--save-sram save.sav]\n"
        << "                    [--load-state state.sgstate] [--save-state state.sgstate] [--force-state]\n"
        << "                    [--profile profiles.txt]\n"
        << "                    [--print-hash]\n"
        << "                    [--quit-after-frames n]\n"
        << "                    [--show-status]\n"
        << "                    [--disable-sprite-limit] [--reduce-flicker] [--enable-fm] [--enable-ym2612]\n";
}

Options parse_args(int argc, char** argv) {
    Options opts;
    opts.gui_launch = argc == 1;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            std::exit(0);
        }
        if (arg == "--gui") {
            opts.gui_launch = true;
            continue;
        }
        if (arg == "--show-status") {
            opts.show_status_window = true;
            continue;
        }
        if (arg == "--bios" && i + 1 < argc) {
            opts.bios = argv[++i];
            continue;
        }
        if (arg == "--gui-rom" && i + 1 < argc) {
            opts.gui_launch = true;
            opts.rom = argv[++i];
            continue;
        }
        if (arg == "--mapper" && i + 1 < argc) {
            opts.mapper = parse_mapper(argv[++i]);
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
        if (arg == "--profile" && i + 1 < argc) {
            opts.profile = argv[++i];
            continue;
        }
        if (arg == "--model" && i + 1 < argc) {
            const std::string model = argv[++i];
            if (model == "sms") {
                opts.model = ConsoleModel::SMS;
            } else if (model == "sg3000" || model == "sg-3000") {
                opts.model = ConsoleModel::SG3000;
            } else {
                throw std::runtime_error("unknown model: " + model);
            }
            continue;
        }
        if (arg == "--video-standard" && i + 1 < argc) {
            opts.video_standard = parse_video_standard(argv[++i]);
            continue;
        }
        if (arg == "--scale" && i + 1 < argc) {
            opts.scale = std::max(1, std::stoi(argv[++i]));
            continue;
        }
        if (arg == "--mute") {
            opts.audio = false;
            continue;
        }
        if (arg == "--no-overlay") {
            opts.overlay = false;
            continue;
        }
        if (arg == "--print-hash") {
            opts.print_hash = true;
            continue;
        }
        if (arg == "--audio-latency-ms" && i + 1 < argc) {
            opts.audio_latency_ms = std::clamp(std::stoi(argv[++i]), 10, 300);
            continue;
        }
        if (arg == "--audio-sample-rate" && i + 1 < argc) {
            opts.audio_sample_rate = static_cast<u32>(std::clamp(std::stoi(argv[++i]), 8000, 96000));
            continue;
        }
        if (arg == "--quit-after-frames" && i + 1 < argc) {
            opts.quit_after_frames = static_cast<std::size_t>(std::stoull(argv[++i]));
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
        if (arg == "--enable-ym2612") {
            opts.enhancements.enable_ym2612 = true;
            opts.enhancements.mode = RuntimeMode::Enhanced;
            continue;
        }
        if (opts.rom.empty()) {
            opts.rom = arg;
            continue;
        }
        throw std::runtime_error("unexpected argument: " + arg);
    }
    if (opts.rom.empty() && !opts.gui_launch) {
        throw std::runtime_error("missing input ROM");
    }
    return opts;
}

std::optional<std::filesystem::path> choose_local_file(const wchar_t* title, const wchar_t* filter) {
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = title;
    dialog.lpstrFilter = filter;
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) {
        return std::filesystem::path(path.data());
    }
    const DWORD error = CommDlgExtendedError();
    if (error != 0) {
        throw std::runtime_error("the Windows file selector failed");
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> choose_rom_file() {
    static constexpr wchar_t filter[] = L"ROMs Sega (*.sms;*.sg;*.bin;*.rom)\0*.sms;*.sg;*.bin;*.rom\0"
                                        L"Todos os arquivos (*.*)\0*.*\0\0";
    return choose_local_file(L"Selecione a ROM para jogar", filter);
}

std::optional<std::filesystem::path> choose_bios_file() {
    static constexpr wchar_t filter[] = L"BIOS Sega (*.sms;*.bin;*.rom)\0*.sms;*.bin;*.rom\0"
                                        L"Todos os arquivos (*.*)\0*.*\0\0";
    return choose_local_file(L"Selecione a BIOS", filter);
}

std::optional<std::filesystem::path> choose_state_file() {
    static constexpr wchar_t filter[] = L"Estados SG3000Recomp (*.sgstate)\0*.sgstate\0"
                                        L"Todos os arquivos (*.*)\0*.*\0\0";
    return choose_local_file(L"Carregar estado", filter);
}

std::optional<std::filesystem::path> choose_output_file(const wchar_t* title,
                                                        const wchar_t* filter,
                                                        const wchar_t* extension,
                                                        const std::filesystem::path& suggested_name) {
    std::array<wchar_t, 32768> path{};
    const std::wstring suggested = suggested_name.wstring();
    std::copy_n(suggested.c_str(), std::min(suggested.size(), path.size() - 1), path.data());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = title;
    dialog.lpstrFilter = filter;
    dialog.lpstrDefExt = extension;
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&dialog)) {
        return std::filesystem::path(path.data());
    }
    if (CommDlgExtendedError() != 0) {
        throw std::runtime_error("the Windows save file selector failed");
    }
    return std::nullopt;
}

std::filesystem::path suggested_media_name(const AppState& app, const wchar_t* extension) {
    std::filesystem::path name = app.current_rom_path.stem();
    if (name.empty()) {
        name = L"SG3000Recomp";
    }
    name += extension;
    return name;
}

const char* control_action_name(ControlAction action) {
    switch (action) {
    case ControlAction::Up:
        return "Cima";
    case ControlAction::Down:
        return "Baixo";
    case ControlAction::Left:
        return "Esquerda";
    case ControlAction::Right:
        return "Direita";
    case ControlAction::Button1:
        return "Botao 1";
    case ControlAction::Button2:
        return "Botao 2";
    case ControlAction::Pause:
        return "Pause/NMI";
    case ControlAction::Count:
        break;
    }
    return "Desconhecido";
}

std::wstring key_display_name(UINT key) {
    UINT scan_code = MapVirtualKeyW(key, MAPVK_VK_TO_VSC) << 16;
    if (key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN || key == VK_INSERT || key == VK_DELETE ||
        key == VK_HOME || key == VK_END || key == VK_PRIOR || key == VK_NEXT) {
        scan_code |= 1U << 24;
    }
    std::array<wchar_t, 64> name{};
    if (GetKeyNameTextW(static_cast<LONG>(scan_code), name.data(), static_cast<int>(name.size())) > 0) {
        return name.data();
    }
    return L"VK " + std::to_wstring(key);
}

u8 joypad_button_for_action(ControlAction action) {
    switch (action) {
    case ControlAction::Up:
        return Joypad::Up;
    case ControlAction::Down:
        return Joypad::Down;
    case ControlAction::Left:
        return Joypad::Left;
    case ControlAction::Right:
        return Joypad::Right;
    case ControlAction::Button1:
        return Joypad::Button1;
    case ControlAction::Button2:
        return Joypad::Button2;
    case ControlAction::Pause:
    case ControlAction::Count:
        return 0;
    }
    return 0;
}

void set_emulation_paused(AppState& app, bool paused) {
    app.emulation_paused = paused;
    if (app.audio) {
        app.audio->set_paused(paused);
    }
}

void reset_emulation(AppState& app) {
    if (!app.has_rom) {
        app.status_message = "nenhum jogo carregado";
        return;
    }
    app.host->reset();
    app.last_frame = {};
    app.rendered_frames = 0;
    app.fps_window_frames = 0;
    app.fps = 0.0;
    app.status_message = "emulacao reiniciada";
    if (app.audio) {
        app.audio->flush();
    }
}

void resize_game_window(HWND hwnd, AppState& app, int scale) {
    app.window_scale = std::clamp(scale, 1, 6);
    RECT window{};
    GetWindowRect(hwnd, &window);
    RECT desired{0, 0, Vdp::width * app.window_scale, app.host->console().vdp().active_height() * app.window_scale};
    AdjustWindowRectEx(&desired,
                       static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_STYLE)),
                       GetMenu(hwnd) != nullptr,
                       static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_EXSTYLE)));
    SetWindowPos(hwnd,
                 nullptr,
                 window.left,
                 window.top,
                 desired.right - desired.left,
                 desired.bottom - desired.top,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    app.status_message = "tela ajustada para " + std::to_string(app.window_scale) + "x";
}

std::wstring recent_game_menu_label(std::size_t index, const std::filesystem::path& game);

std::wstring escape_menu_label(std::wstring label) {
    for (std::size_t position = 0; (position = label.find(L'&', position)) != std::wstring::npos; position += 2) {
        label.insert(position, 1, L'&');
    }
    return label;
}

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring converted(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), converted.data(), length);
    return converted;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return std::string(text.begin(), text.end());
    }
    std::string converted(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8,
                        WC_ERR_INVALID_CHARS,
                        text.data(),
                        static_cast<int>(text.size()),
                        converted.data(),
                        length,
                        nullptr,
                        nullptr);
    return converted;
}

void refresh_recent_menu(AppState& app) {
    if (app.recent_menu == nullptr) {
        return;
    }
    while (DeleteMenu(app.recent_menu, 0, MF_BYPOSITION)) {
    }
    if (app.recent_games.empty()) {
        AppendMenuW(app.recent_menu, MF_STRING | MF_GRAYED, 0, L"(nenhum jogo recente)");
        return;
    }
    for (std::size_t index = 0; index < app.recent_games.size() && index < 10; ++index) {
        const std::wstring label = recent_game_menu_label(index, app.recent_games[index]);
        AppendMenuW(app.recent_menu, MF_STRING, MenuRecentFirst + index, label.c_str());
    }
}

void refresh_library_menu(AppState& app) {
    if (app.library_menu == nullptr) {
        return;
    }
    while (DeleteMenu(app.library_menu, 0, MF_BYPOSITION)) {
    }
    if (app.game_library.empty()) {
        AppendMenuW(app.library_menu, MF_STRING | MF_GRAYED, 0, L"(biblioteca vazia)");
    } else {
        for (std::size_t index = 0; index < app.game_library.size() && index < 50; ++index) {
            const auto& entry = app.game_library[index];
            std::wstring label = entry.alias.empty() ? entry.path.filename().wstring() : utf8_to_wide(entry.alias);
            label = escape_menu_label(std::move(label));
            if (!entry.platform.empty()) {
                label += L"\t" + utf8_to_wide(entry.platform);
            }
            AppendMenuW(app.library_menu, MF_STRING, MenuLibraryFirst + index, label.c_str());
        }
    }
    AppendMenuW(app.library_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(app.library_menu, MF_STRING, MenuLibrarySetAlias, L"Definir apelido para o jogo atual...");
    AppendMenuW(app.library_menu, MF_STRING, MenuLibraryClearAlias, L"Remover apelido do jogo atual");
}

void save_active_sram(AppState& app) {
    if (!app.has_rom || app.current_sram_path.empty()) {
        return;
    }
    const auto& sram = app.host->console().bus().debug_cartridge_ram();
    write_binary_file(app.current_sram_path, std::span<const u8>(sram.data(), sram.size()));
}

void apply_profile_to_options(Options& options,
                              const std::string& rom_hash,
                              std::string& profile_name,
                              std::string& profile_fingerprint) {
    if (options.profile.empty() || !std::filesystem::exists(options.profile)) {
        return;
    }
    const auto profiles = GameProfileDatabase::load(options.profile);
    const GameProfile* profile = profiles.find_by_hash(rom_hash);
    if (profile == nullptr) {
        return;
    }
    profile_name = profile->name.empty() ? profile->hash : profile->name;
    profile_fingerprint = game_profile_fingerprint(*profile);
    if (profile->has_model) {
        options.model = profile->model;
    }
    if (profile->has_mapper) {
        options.mapper = profile->mapper;
    }
    if (profile->has_enhancements) {
        options.enhancements = profile->enhancements;
    }
    if (profile->has_audio_latency_ms) {
        options.audio_latency_ms = profile->audio_latency_ms;
    }
    if (profile->has_audio_sample_rate) {
        options.audio_sample_rate = profile->audio_sample_rate;
    }
    if (profile->has_video_standard) {
        options.video_standard = profile->video_standard;
    }
}

void configure_session_audio(AppState& app, const Options& options) {
    const int previous_volume = app.audio ? app.audio->volume_percent() : 100;
    const bool previous_muted = app.audio ? app.audio->muted() : false;
    app.audio.reset();
    if (!options.audio) {
        return;
    }
    auto audio = std::make_unique<Win32Audio>();
    if (!audio->open(options.audio_sample_rate, options.audio_latency_ms)) {
        return;
    }
    audio->set_volume_percent(previous_volume);
    audio->set_muted(previous_muted);
    app.audio = std::move(audio);
}

void load_game_session(HWND hwnd, AppState& app, const std::filesystem::path& rom_path) {
    auto rom = normalize_rom_payload(read_file(rom_path));
    const std::string rom_hash = rom_hash_fnv1a64(rom);
    const CartridgeHeaderInfo header = analyze_cartridge_header(rom);
    Options options = app.session_options;
    options.rom = rom_path;
    options.enhancements = app.host->console().enhancements();
    std::string profile_name;
    std::string profile_fingerprint;
    apply_profile_to_options(options, rom_hash, profile_name, profile_fingerprint);
    const std::optional<std::vector<u8>> bios = options.bios.empty()
                                                    ? std::optional<std::vector<u8>>{}
                                                    : std::optional<std::vector<u8>>{read_file(options.bios)};

    HostRuntimeConfig runtime_config = host_runtime_config_for_video_standard(options.video_standard);
    runtime_config.audio_sample_rate = options.audio_sample_rate;
    auto host = std::make_unique<HostRuntime>(options.model, options.enhancements, runtime_config);
    host->console().bus().set_mapper(options.mapper);
    if (bios) {
        host->load_bios(*bios);
    }
    host->load_rom(rom);

    const std::filesystem::path saves = graphical_user_data_root() / L"saves";
    std::filesystem::create_directories(saves);
    const std::string stem = hash_file_stem(rom_hash);
    const std::filesystem::path sram_path = saves / (stem + ".sav");
    if (std::filesystem::exists(sram_path)) {
        host->console().bus().load_cartridge_ram(read_file(sram_path));
    }

    save_active_sram(app);
    const bool recording_interrupted = app.audio_recording;
    app.audio_recording = false;
    app.host = std::move(host);
    configure_session_audio(app, options);
    app.has_rom = true;
    app.current_rom_path = std::filesystem::absolute(rom_path);
    app.current_sram_path = sram_path;
    app.quick_state_path = saves / (stem + ".sgstate");
    app.rom_hash = rom_hash;
    app.profile_name = profile_name;
    app.state_metadata = {};
    app.state_metadata.present = true;
    app.state_metadata.model = options.model;
    app.state_metadata.rom_hash = rom_hash;
    app.state_metadata.environment_identity_present = true;
    app.state_metadata.bios_hash = bios ? rom_hash_fnv1a64(*bios) : std::string{};
    app.state_metadata.profile_fingerprint = profile_fingerprint;
    app.input = {};
    app.last_frame = {};
    app.rendered_frames = 0;
    app.fps_window_frames = 0;
    app.fps = 0.0;
    app.emulation_paused = false;
    app.bitmap_info.bmiHeader.biHeight = -app.host->console().vdp().active_height();
    app.recent_games = touch_recent_game(app.recent_games, app.current_rom_path);
    save_recent_games(graphical_user_data_root() / L"recent-games.txt", app.recent_games);
    refresh_recent_menu(app);
    GameLibraryEntry library_entry;
    library_entry.path = app.current_rom_path;
    library_entry.hash = rom_hash;
    library_entry.platform = cartridge_platform_name(cartridge_header_platform(header));
    library_entry.region = cartridge_region_name(header.region);
    library_entry.product_code = header.product_code;
    app.game_library = touch_game_library(app.game_library, std::move(library_entry));
    save_game_library(graphical_user_data_root() / L"game-library.txt", app.game_library);
    refresh_library_menu(app);
    resize_game_window(hwnd, app, app.window_scale);
    const std::string title = "SG3000Recomp - " + rom_path.filename().string();
    SetWindowTextA(hwnd, title.c_str());
    app.status_message = options.bios.empty() ? "jogo iniciado sem BIOS" : "BIOS iniciada antes do jogo";
    if (recording_interrupted) {
        app.status_message += "; gravacao anterior mantida na memoria";
    }
}

void select_session_bios(HWND hwnd, AppState& app) {
    const auto bios = choose_bios_file();
    if (!bios) {
        return;
    }
    (void)read_file(*bios);
    app.session_options.bios = *bios;
    if (app.has_rom) {
        const std::filesystem::path game = app.current_rom_path;
        load_game_session(hwnd, app, game);
    } else {
        app.status_message = "BIOS selecionada; abra uma ROM para iniciar";
        const std::string title = "SG3000Recomp - Sem jogo - BIOS: " + bios->filename().string();
        SetWindowTextA(hwnd, title.c_str());
    }
}

void clear_session_bios(HWND hwnd, AppState& app) {
    app.session_options.bios.clear();
    if (app.has_rom) {
        const std::filesystem::path game = app.current_rom_path;
        load_game_session(hwnd, app, game);
    } else {
        app.status_message = "BIOS removida; a proxima ROM iniciara diretamente";
        SetWindowTextA(hwnd, "SG3000Recomp - Nenhum jogo carregado");
    }
}

void save_state_as(AppState& app) {
    static constexpr wchar_t filter[] = L"Estados SG3000Recomp (*.sgstate)\0*.sgstate\0\0";
    const auto path = choose_output_file(L"Salvar estado", filter, L"sgstate", suggested_media_name(app, L".sgstate"));
    if (!path) {
        return;
    }
    const auto bytes = save_console_state(app.host->console(), app.state_metadata);
    write_binary_file(*path, std::span<const u8>(bytes.data(), bytes.size()));
    app.status_message = "estado salvo em " + path->filename().string();
}

void load_state_from_file(AppState& app) {
    const auto path = choose_state_file();
    if (!path) {
        return;
    }
    const auto bytes = read_file(*path);
    validate_save_state_metadata(read_save_state_metadata(bytes), app.state_metadata);
    load_console_state(app.host->console(), bytes);
    app.host->clear_audio();
    if (app.audio) {
        app.audio->flush();
    }
    app.input = {};
    app.last_frame = {};
    app.status_message = "estado carregado de " + path->filename().string();
}

void save_screenshot(AppState& app) {
    static constexpr wchar_t filter[] = L"Imagem bitmap (*.bmp)\0*.bmp\0\0";
    const auto path = choose_output_file(L"Salvar screenshot", filter, L"bmp", suggested_media_name(app, L".bmp"));
    if (!path) {
        return;
    }
    write_bmp_image(*path,
                    std::span<const u32>(app.host->framebuffer().data(), app.host->framebuffer().size()),
                    Vdp::width,
                    app.host->console().vdp().active_height());
    app.status_message = "screenshot salvo em " + path->filename().string();
}

bool save_recorded_audio(AppState& app) {
    if (app.recorded_audio.empty()) {
        app.status_message = "nenhum audio gravado";
        return false;
    }
    static constexpr wchar_t filter[] = L"Audio WAV (*.wav)\0*.wav\0\0";
    const auto path =
        choose_output_file(L"Salvar gravacao de audio", filter, L"wav", suggested_media_name(app, L"-audio.wav"));
    if (!path) {
        app.status_message = "gravacao mantida na memoria";
        return false;
    }
    write_pcm16_stereo_wav(*path, app.recorded_audio, app.recorded_audio_sample_rate);
    app.status_message = "audio salvo em " + path->filename().string();
    return true;
}

void start_audio_recording(AppState& app) {
    app.recorded_audio.clear();
    app.recorded_audio_sample_rate = app.host->config().audio_sample_rate;
    app.audio_recording = true;
    app.status_message = "gravacao de audio iniciada";
}

void stop_audio_recording(AppState& app) {
    app.audio_recording = false;
    if (save_recorded_audio(app)) {
        app.recorded_audio.clear();
    }
}

std::vector<GameProfile> load_editable_profiles(const std::filesystem::path& path) {
    if (path.empty() || !std::filesystem::exists(path)) {
        return {};
    }
    const auto database = GameProfileDatabase::load(path);
    return database.profiles();
}

GameProfile current_game_profile(const AppState& app) {
    GameProfile profile;
    profile.name = app.current_rom_path.stem().string();
    profile.hash = app.rom_hash;
    profile.has_model = true;
    profile.model = app.state_metadata.model;
    profile.has_mapper = true;
    profile.mapper = app.host->console().bus().mapper_snapshot().requested_mapper;
    profile.has_enhancements = true;
    profile.enhancements = app.host->console().enhancements();
    profile.has_audio_latency_ms = true;
    profile.audio_latency_ms = app.audio ? app.audio->target_latency_ms() : app.session_options.audio_latency_ms;
    profile.has_audio_sample_rate = true;
    profile.audio_sample_rate = app.host->config().audio_sample_rate;
    profile.has_video_standard = true;
    profile.video_standard =
        app.host->config().scanlines_per_frame == 313 ? HostVideoStandard::Pal : HostVideoStandard::Ntsc;
    return profile;
}

void store_game_profile(HWND hwnd,
                        AppState& app,
                        std::optional<CartridgeMapper> mapper = {},
                        std::optional<HostVideoStandard> video_standard = {}) {
    GameProfile profile = current_game_profile(app);
    if (mapper) {
        profile.mapper = *mapper;
    }
    if (video_standard) {
        profile.video_standard = *video_standard;
    }
    auto profiles = load_editable_profiles(app.session_options.profile);
    profiles.erase(std::remove_if(profiles.begin(),
                                  profiles.end(),
                                  [&](const GameProfile& candidate) { return candidate.hash == profile.hash; }),
                   profiles.end());
    profiles.push_back(profile);
    save_game_profiles(app.session_options.profile, profiles);
    const std::filesystem::path game = app.current_rom_path;
    load_game_session(hwnd, app, game);
    app.status_message = "perfil local salvo e aplicado";
}

void remove_game_profile(HWND hwnd, AppState& app) {
    auto profiles = load_editable_profiles(app.session_options.profile);
    const auto end = std::remove_if(
        profiles.begin(), profiles.end(), [&](const GameProfile& profile) { return profile.hash == app.rom_hash; });
    if (end == profiles.end()) {
        app.status_message = "este jogo nao possui perfil local";
        return;
    }
    profiles.erase(end, profiles.end());
    save_game_profiles(app.session_options.profile, profiles);
    const std::filesystem::path game = app.current_rom_path;
    load_game_session(hwnd, app, game);
    app.status_message = "perfil local removido";
}

struct AliasPromptState {
    HWND edit = nullptr;
    bool done = false;
    bool accepted = false;
    std::wstring value;
};

LRESULT CALLBACK alias_prompt_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<AliasPromptState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCT*>(lparam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    switch (message) {
    case WM_COMMAND:
        if (state != nullptr && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL)) {
            if (LOWORD(wparam) == IDOK) {
                std::array<wchar_t, 256> value{};
                GetWindowTextW(state->edit, value.data(), static_cast<int>(value.size()));
                state->value = value.data();
                state->accepted = true;
            }
            state->done = true;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        if (state != nullptr) {
            state->done = true;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state != nullptr) {
            state->done = true;
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

std::optional<std::string> prompt_game_alias(HWND owner, const std::string& current_alias) {
    constexpr const wchar_t* class_name = L"SG3000RecompAliasPrompt";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = alias_prompt_proc;
    window_class.hInstance = GetModuleHandle(nullptr);
    window_class.lpszClassName = class_name;
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("cannot register alias window");
    }

    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    AliasPromptState state;
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME,
                                  class_name,
                                  L"SG3000Recomp - Apelido do jogo",
                                  WS_CAPTION | WS_SYSMENU,
                                  owner_rect.left + 80,
                                  owner_rect.top + 80,
                                  440,
                                  155,
                                  owner,
                                  nullptr,
                                  GetModuleHandle(nullptr),
                                  &state);
    if (window == nullptr) {
        throw std::runtime_error("cannot create alias window");
    }
    CreateWindowExW(0,
                    L"STATIC",
                    L"Apelido exibido na biblioteca local:",
                    WS_CHILD | WS_VISIBLE,
                    14,
                    14,
                    390,
                    20,
                    window,
                    nullptr,
                    GetModuleHandle(nullptr),
                    nullptr);
    state.edit = CreateWindowExW(WS_EX_CLIENTEDGE,
                                 L"EDIT",
                                 utf8_to_wide(current_alias).c_str(),
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 14,
                                 38,
                                 396,
                                 25,
                                 window,
                                 nullptr,
                                 GetModuleHandle(nullptr),
                                 nullptr);
    if (state.edit == nullptr) {
        DestroyWindow(window);
        throw std::runtime_error("cannot create alias input");
    }
    SendMessageW(state.edit, EM_SETLIMITTEXT, 120, 0);
    CreateWindowExW(0,
                    L"BUTTON",
                    L"Salvar",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    238,
                    76,
                    82,
                    28,
                    window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                    GetModuleHandle(nullptr),
                    nullptr);
    CreateWindowExW(0,
                    L"BUTTON",
                    L"Cancelar",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    328,
                    76,
                    82,
                    28,
                    window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                    GetModuleHandle(nullptr),
                    nullptr);
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    SetFocus(state.edit);
    SendMessageW(state.edit, EM_SETSEL, 0, -1);
    MSG message{};
    while (!state.done) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) {
                PostQuitMessage(static_cast<int>(message.wParam));
            }
            break;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (IsWindow(window)) {
        DestroyWindow(window);
    }
    if (IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return state.accepted ? std::optional<std::string>{wide_to_utf8(state.value)} : std::nullopt;
}

void edit_current_game_alias(HWND hwnd, AppState& app) {
    const auto entry = std::find_if(app.game_library.begin(),
                                    app.game_library.end(),
                                    [&](const GameLibraryEntry& game) { return game.hash == app.rom_hash; });
    const std::string current = entry == app.game_library.end() ? std::string{} : entry->alias;
    const auto alias = prompt_game_alias(hwnd, current);
    if (!alias) {
        return;
    }
    if (!set_game_library_alias(app.game_library, app.rom_hash, *alias)) {
        throw std::runtime_error("current game is missing from the local library");
    }
    save_game_library(graphical_user_data_root() / L"game-library.txt", app.game_library);
    refresh_library_menu(app);
    app.status_message = alias->empty() ? "apelido removido" : "apelido salvo na biblioteca local";
}

void clear_current_game_alias(AppState& app) {
    if (set_game_library_alias(app.game_library, app.rom_hash, {})) {
        save_game_library(graphical_user_data_root() / L"game-library.txt", app.game_library);
        refresh_library_menu(app);
        app.status_message = "apelido removido";
    }
}

void request_control_binding(HWND hwnd, AppState& app, ControlAction action) {
    app.pending_binding = action;
    app.input = {};
    const std::wstring message = L"Apos fechar esta mensagem, pressione a nova tecla para " +
                                 std::wstring(control_action_name(action),
                                              control_action_name(action) + std::strlen(control_action_name(action))) +
                                 L".\n\nPressione Esc para cancelar.";
    MessageBoxW(hwnd, message.c_str(), L"SG3000Recomp - Remapear controle", MB_OK | MB_ICONINFORMATION);
    app.status_message = std::string{"aguardando tecla para "} + control_action_name(action);
}

bool confirm_enhanced_mode(HWND hwnd, AppState& app) {
    if (app.compatibility_warning_acknowledged) {
        return true;
    }
    const int result =
        MessageBoxW(hwnd,
                    L"O modo enhanced pode alterar a aparencia original do jogo.\n\n"
                    L"Reducao de flicker, remocao do limite de sprites e o YM2612 experimental nao representam "
                    L"exatamente o hardware historico do SMS/SG-3000. Voce pode voltar ao modo fiel a qualquer "
                    L"momento.\n\n"
                    L"Deseja continuar?",
                    L"SG3000Recomp - Aviso de compatibilidade",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (result == IDYES) {
        app.compatibility_warning_acknowledged = true;
        return true;
    }
    return false;
}

void set_runtime_mode(AppState& app, RuntimeMode mode) {
    auto config = app.host->console().enhancements();
    config.mode = mode;
    if (mode == RuntimeMode::Accurate) {
        config.reduce_flicker = false;
        config.disable_sprite_limit = false;
        config.enable_ym2612 = false;
        app.status_message = "modo fiel ativado";
    } else {
        app.status_message = "modo enhanced ativado";
    }
    app.host->console().set_enhancements(config);
}

void set_enhancement(AppState& app, MenuCommand command, bool enabled) {
    auto config = app.host->console().enhancements();
    if (command == MenuEnhancementReduceFlicker) {
        config.reduce_flicker = enabled;
        app.status_message = enabled ? "reducao de flicker ativada" : "reducao de flicker desativada";
    } else if (command == MenuEnhancementDisableSpriteLimit) {
        config.disable_sprite_limit = enabled;
        app.status_message = enabled ? "limite de sprites desativado" : "limite de sprites restaurado";
    } else if (command == MenuEnhancementYm2612) {
        config.enable_ym2612 = enabled;
        app.status_message = enabled ? "YM2612 experimental ativado" : "YM2612 experimental desativado";
    }
    if (enabled && config.mode == RuntimeMode::Accurate) {
        config.mode = RuntimeMode::Enhanced;
    }
    app.host->console().set_enhancements(config);
}

void update_menu_checks(HWND hwnd, const AppState& app) {
    const HMENU menu = GetMenu(hwnd);
    if (menu == nullptr || !app.host) {
        return;
    }
    const auto check = [menu](UINT command, bool checked) {
        CheckMenuItem(menu, command, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
    };
    EnableMenuItem(menu, MenuEmulationPause, MF_BYCOMMAND | (app.has_rom ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, MenuEmulationReset, MF_BYCOMMAND | (app.has_rom ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, MenuFileSaveState, MF_BYCOMMAND | (app.has_rom ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, MenuFileLoadState, MF_BYCOMMAND | (app.has_rom ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, MenuFileScreenshot, MF_BYCOMMAND | (app.has_rom ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(
        menu, MenuAudioStartRecording, MF_BYCOMMAND | (app.has_rom && !app.audio_recording ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, MenuAudioStopAndSave, MF_BYCOMMAND | (app.audio_recording ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu,
                   MenuAudioSaveLast,
                   MF_BYCOMMAND | (!app.audio_recording && !app.recorded_audio.empty() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu,
                   MenuAudioClear,
                   MF_BYCOMMAND | (!app.audio_recording && !app.recorded_audio.empty() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(
        menu, MenuFileClearBios, MF_BYCOMMAND | (!app.session_options.bios.empty() ? MF_ENABLED : MF_GRAYED));
    for (UINT command = MenuProfileMapperFirst; command <= MenuProfileMapperLast; ++command) {
        EnableMenuItem(menu, command, MF_BYCOMMAND | (app.has_rom ? MF_ENABLED : MF_GRAYED));
    }
    EnableMenuItem(menu, MenuProfileVideoNtsc, MF_BYCOMMAND | (app.has_rom ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, MenuProfileVideoPal, MF_BYCOMMAND | (app.has_rom ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, MenuProfileSave, MF_BYCOMMAND | (app.has_rom ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(
        menu, MenuProfileRemove, MF_BYCOMMAND | (app.has_rom && !app.profile_name.empty() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, MenuLibrarySetAlias, MF_BYCOMMAND | (app.has_rom ? MF_ENABLED : MF_GRAYED));
    const auto library_entry = std::find_if(app.game_library.begin(), app.game_library.end(), [&](const auto& entry) {
        return entry.hash == app.rom_hash;
    });
    EnableMenuItem(menu,
                   MenuLibraryClearAlias,
                   MF_BYCOMMAND |
                       (app.has_rom && library_entry != app.game_library.end() && !library_entry->alias.empty()
                            ? MF_ENABLED
                            : MF_GRAYED));
    const auto& config = app.host->console().enhancements();
    check(MenuEmulationPause, app.emulation_paused);
    CheckMenuRadioItem(menu,
                       MenuModeAccurate,
                       MenuModeEnhanced,
                       config.mode == RuntimeMode::Accurate ? MenuModeAccurate : MenuModeEnhanced,
                       MF_BYCOMMAND);
    check(MenuEnhancementReduceFlicker, config.reduce_flicker);
    check(MenuEnhancementDisableSpriteLimit, config.disable_sprite_limit);
    check(MenuEnhancementYm2612, config.enable_ym2612);
    check(MenuViewOverlay, app.overlay_enabled);
    check(MenuViewStatus, app.status_window != nullptr && IsWindowVisible(app.status_window));
    const auto mapper = app.host->console().bus().mapper_snapshot().requested_mapper;
    const auto mapper_it = std::find(graphical_profile_mappers.begin(), graphical_profile_mappers.end(), mapper);
    if (mapper_it != graphical_profile_mappers.end()) {
        CheckMenuRadioItem(menu,
                           MenuProfileMapperFirst,
                           MenuProfileMapperLast,
                           MenuProfileMapperFirst + static_cast<UINT>(mapper_it - graphical_profile_mappers.begin()),
                           MF_BYCOMMAND);
    }
    CheckMenuRadioItem(menu,
                       MenuProfileVideoNtsc,
                       MenuProfileVideoPal,
                       app.host->config().scanlines_per_frame == 313 ? MenuProfileVideoPal : MenuProfileVideoNtsc,
                       MF_BYCOMMAND);
    CheckMenuRadioItem(app.scale_menu,
                       MenuScaleFirst,
                       MenuScaleLast,
                       MenuScaleFirst + static_cast<UINT>(app.window_scale - 1),
                       MF_BYCOMMAND);
    if (app.controls_menu != nullptr) {
        for (std::size_t index = 0; index < app.bindings.keys.size(); ++index) {
            const auto action = static_cast<ControlAction>(index);
            const std::wstring label =
                std::wstring(control_action_name(action),
                             control_action_name(action) + std::strlen(control_action_name(action))) +
                L"\t" + key_display_name(app.bindings.keys[index]);
            ModifyMenuW(app.controls_menu,
                        MenuControlFirst + static_cast<UINT>(index),
                        MF_BYCOMMAND | MF_STRING,
                        MenuControlFirst + static_cast<UINT>(index),
                        label.c_str());
        }
    }
}

void update_key(AppState& app, WPARAM key, bool pressed) {
    if (pressed && app.pending_binding) {
        if (key == VK_ESCAPE) {
            app.status_message = "remapeamento cancelado";
        } else {
            const std::size_t target = static_cast<std::size_t>(*app.pending_binding);
            const UINT previous = app.bindings.keys[target];
            const auto conflict = std::find(app.bindings.keys.begin(), app.bindings.keys.end(), static_cast<UINT>(key));
            if (conflict != app.bindings.keys.end()) {
                *conflict = previous;
            }
            app.bindings.keys[target] = static_cast<UINT>(key);
            app.status_message = std::string{control_action_name(*app.pending_binding)} + " remapeado";
        }
        app.pending_binding.reset();
        app.input = {};
        return;
    }

    bool matched_game_control = false;
    for (std::size_t index = 0; index < app.bindings.keys.size(); ++index) {
        if (app.bindings.keys[index] != key) {
            continue;
        }
        matched_game_control = true;
        const auto action = static_cast<ControlAction>(index);
        if (action == ControlAction::Pause) {
            app.input.pause = pressed;
        } else if (const u8 button = joypad_button_for_action(action); button != 0) {
            if (pressed) {
                app.input.player1 = static_cast<u8>(app.input.player1 | button);
            } else {
                app.input.player1 = static_cast<u8>(app.input.player1 & ~button);
            }
        }
    }
    if (matched_game_control) {
        return;
    }
    if (key == VK_F1 && pressed) {
        app.overlay_enabled = !app.overlay_enabled;
    }
    if (key == VK_F5 && pressed && !app.quick_state_path.empty()) {
        try {
            const auto bytes = save_console_state(app.host->console(), app.state_metadata);
            write_binary_file(app.quick_state_path, std::span<const u8>(bytes.data(), bytes.size()));
            app.status_message = "quick save gravado (F5)";
        } catch (const std::exception& error) {
            app.status_message = std::string{"erro no quick save: "} + error.what();
        }
    }
    if (key == VK_F9 && pressed && !app.quick_state_path.empty()) {
        try {
            if (!std::filesystem::exists(app.quick_state_path)) {
                app.status_message = "quick save ainda nao existe";
            } else {
                const auto bytes = read_file(app.quick_state_path);
                validate_save_state_metadata(read_save_state_metadata(bytes), app.state_metadata);
                load_console_state(app.host->console(), bytes);
                app.host->clear_audio();
                if (app.audio) {
                    app.audio->flush();
                }
                app.status_message = "quick save carregado (F9)";
            }
        } catch (const std::exception& error) {
            app.status_message = std::string{"erro no quick load: "} + error.what();
        }
    }
    if (key == VK_SPACE && pressed) {
        set_emulation_paused(app, !app.emulation_paused);
    }
    if (key == 'R' && pressed) {
        reset_emulation(app);
    }
    if (key == 'M' && pressed && app.audio) {
        app.audio->set_muted(!app.audio->muted());
    }
    if ((key == VK_OEM_PLUS || key == VK_ADD) && pressed && app.audio) {
        app.audio->set_volume_percent(app.audio->volume_percent() + 5);
    }
    if ((key == VK_OEM_MINUS || key == VK_SUBTRACT) && pressed && app.audio) {
        app.audio->set_volume_percent(app.audio->volume_percent() - 5);
    }
}

const char* runtime_mode_name(RuntimeMode mode) {
    switch (mode) {
    case RuntimeMode::Accurate:
        return "accurate";
    case RuntimeMode::Enhanced:
        return "enhanced";
    case RuntimeMode::Hybrid:
        return "hybrid";
    default:
        return "unknown";
    }
}

std::string overlay_text(const AppState& app) {
    const auto& config = app.host->console().enhancements();
    const auto& cpu = app.host->console().cpu();
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << "FPS " << app.fps << "  frame " << app.last_frame.frame_index
        << "  PC $" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << static_cast<int>(cpu.pc)
        << std::dec << "\n"
        << "AF $" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << cpu.af() << " BC $"
        << std::setw(4) << cpu.bc() << " DE $" << std::setw(4) << cpu.de() << " HL $" << std::setw(4) << cpu.hl()
        << " SP $" << std::setw(4) << cpu.sp << std::dec << "\n"
        << "IX $" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << make_u16(cpu.ixl, cpu.ixh)
        << " IY $" << std::setw(4) << make_u16(cpu.iyl, cpu.iyh) << " I $" << std::setw(2) << static_cast<int>(cpu.i)
        << " R $" << std::setw(2) << static_cast<int>(cpu.r) << std::dec << " IM "
        << static_cast<int>(cpu.interrupt_mode) << " IFF " << (cpu.iff1 ? "1" : "0") << "/" << (cpu.iff2 ? "1" : "0")
        << " cycles " << cpu.cycles << (cpu.halted ? " halted" : "") << "\n"
        << "mode " << runtime_mode_name(config.mode) << "  sprite_limit "
        << (config.disable_sprite_limit ? "off" : "on") << "  reduce_flicker " << (config.reduce_flicker ? "on" : "off")
        << "  ym2612 " << (config.enable_ym2612 ? "on" : "off") << "  " << (app.emulation_paused ? "paused" : "running")
        << "\n"
        << "execution " << host_execution_mode_name(app.host->config().execution_mode) << "  interpreted "
        << app.last_frame.interpreted_instructions << "  recompiled " << app.last_frame.recompiled_instructions
        << "  fallback " << app.last_frame.fallback_instructions << "\n";
    if (!app.profile_name.empty()) {
        out << "profile " << app.profile_name << "  ";
    } else {
        out << "profile none  ";
    }
    out << app.rom_hash << "\n";
    const auto mapper = app.host->console().bus().mapper_snapshot();
    out << "mapper " << cartridge_mapper_name(mapper.mapper) << " req "
        << cartridge_mapper_name(mapper.requested_mapper) << " mem $" << std::hex << std::uppercase << std::setw(2)
        << std::setfill('0') << static_cast<int>(mapper.memory_control) << std::dec << " bios "
        << (mapper.bios_enabled ? "on" : "off") << " cart " << (mapper.cartridge_enabled ? "on" : "off") << " ram "
        << (mapper.work_ram_enabled ? "on" : "off");
    out << " exp " << (mapper.expansion_enabled ? "on" : "off") << " card " << (mapper.card_enabled ? "on" : "off")
        << " io " << (mapper.io_chip_enabled ? "on" : "off");
    if (mapper.mapper == CartridgeMapper::SMapper) {
        out << " slots " << static_cast<int>(mapper.smapper_slots[0]) << ","
            << static_cast<int>(mapper.smapper_slots[1]) << "," << static_cast<int>(mapper.smapper_slots[2]);
        if (mapper.cartridge_ram_enabled) {
            out << " cart_ram bank " << static_cast<int>(mapper.cartridge_ram_bank);
        }
    } else if (mapper.mapper == CartridgeMapper::CMapper) {
        out << " slots " << static_cast<int>(mapper.cmapper_slots[0]) << ","
            << static_cast<int>(mapper.cmapper_slots[1]) << "," << static_cast<int>(mapper.cmapper_slots[2]);
    } else if (mapper.mapper == CartridgeMapper::KMapper) {
        out << " slot2 " << static_cast<int>(mapper.kmapper_slot2);
    }
    out << "\n";
    const auto vdp = app.host->console().vdp().debug_snapshot();
    out << "vdp line " << vdp.scanline << "/" << vdp.scanlines_per_frame << " visible " << vdp.active_height << " +"
        << vdp.scanline_cycles << "/" << vdp.cpu_cycles_per_scanline << " status $" << std::hex << std::uppercase
        << std::setw(2) << std::setfill('0') << static_cast<int>(vdp.status) << std::dec << " display "
        << (vdp.display_enabled ? "on" : "off") << " irq " << (vdp.frame_irq_pending ? "vblank" : "-") << "/"
        << (vdp.line_irq_pending ? "line" : "-") << "\n";

    if (app.audio) {
        app.audio->cleanup_completed_buffers();
        const auto stats = app.audio->stats();
        out << "audio " << (app.audio->muted() ? "muted" : "on") << "  vol " << app.audio->volume_percent() << "%"
            << "  queued " << stats.queued_buffers << "/" << app.audio->queued_latency_ms() << "ms"
            << " target " << app.audio->target_latency_ms() << "ms"
            << "  underruns " << stats.underruns << "  drops " << stats.dropped_buffers << "  "
            << app.audio->sample_rate() << " Hz";
    } else {
        out << "audio muted";
    }

    if (!app.status_message.empty()) {
        out << "\n" << app.status_message;
    }
    out << "\nF1 overlay  F5 salvar  F9 carregar"
        << "\nSpace pause  R reset  M mute  +/- volume";
    return out.str();
}

std::string detailed_status_text(const AppState& app) {
    std::ostringstream out;
    out << "SG3000Recomp - status em tempo real\r\n"
        << "Modelo: "
        << ((app.has_rom ? app.state_metadata.model : app.session_options.model) == ConsoleModel::SG3000
                ? "SG-3000"
                : "Master System")
        << "\r\nVideo: " << (app.host->config().scanlines_per_frame == 313 ? "PAL" : "NTSC")
        << " | Backend de janela: Win32"
        << " | Backend de audio: " << (app.audio ? "waveOut" : "desativado") << "\r\n";
    if (!app.has_rom) {
        out << "\r\nNenhum jogo carregado. Configure a sessao e use Arquivo > Abrir ROM...\r\n";
    } else {
        std::string diagnostics = overlay_text(app);
        for (std::size_t position = 0; (position = diagnostics.find('\n', position)) != std::string::npos;
             position += 2) {
            diagnostics.insert(position, 1, '\r');
        }
        out << "ROM: " << app.current_rom_path.filename().string() << "\r\n"
            << "BIOS: " << (app.session_options.bios.empty() ? "nao selecionada" : "selecionada") << "\r\n\r\n"
            << diagnostics << "\r\n";
    }

    const auto& enhancements = app.host->console().enhancements();
    out << "\r\nAvisos de compatibilidade:\r\n";
    bool warned = false;
    if (enhancements.mode != RuntimeMode::Accurate) {
        out << "- Modo enhanced ativo; a saida pode diferir do hardware original.\r\n";
        warned = true;
    }
    if (enhancements.enable_ym2612) {
        out << "- YM2612/Nuked-OPN2 e uma extensao experimental, nao hardware SMS/SG-3000.\r\n";
        warned = true;
    }
    if (app.last_frame.fallback_instructions != 0) {
        out << "- O executor usou fallback interpretado neste frame.\r\n";
        warned = true;
    }
    if (!warned) {
        out << "- Nenhum aviso ativo.\r\n";
    }
    if (app.audio_recording) {
        out << "\r\nGravacao WAV em andamento: " << app.recorded_audio.size() / 2 << " frames de audio.\r\n";
    }
    return out.str();
}

void refresh_status_window(AppState& app) {
    if (app.status_text == nullptr || !IsWindow(app.status_text)) {
        return;
    }
    const std::string text = detailed_status_text(app);
    SetWindowTextA(app.status_text, text.c_str());
}

LRESULT CALLBACK status_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<AppState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCT*>(lparam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    switch (message) {
    case WM_TIMER:
        if (app != nullptr) {
            refresh_status_window(*app);
        }
        return 0;
    case WM_SIZE:
        if (app != nullptr && app->status_text != nullptr) {
            MoveWindow(app->status_text, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (app != nullptr) {
            app->status_window = nullptr;
            app->status_text = nullptr;
            if (const HWND owner = GetWindow(hwnd, GW_OWNER); owner != nullptr && IsWindow(owner)) {
                update_menu_checks(owner, *app);
                DrawMenuBar(owner);
            }
        }
        return 0;
    default:
        return DefWindowProc(hwnd, message, wparam, lparam);
    }
}

void show_status_window(HWND owner, AppState& app) {
    if (app.status_window != nullptr && IsWindow(app.status_window)) {
        ShowWindow(app.status_window, SW_SHOWNORMAL);
        SetForegroundWindow(app.status_window);
        return;
    }

    constexpr const wchar_t* class_name = L"SG3000RecompStatusWindow";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = status_window_proc;
    window_class.hInstance = GetModuleHandle(nullptr);
    window_class.lpszClassName = class_name;
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("cannot register status window");
    }

    app.status_window = CreateWindowExW(WS_EX_TOOLWINDOW,
                                        class_name,
                                        L"SG3000Recomp - Status detalhado",
                                        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                        CW_USEDEFAULT,
                                        CW_USEDEFAULT,
                                        760,
                                        560,
                                        owner,
                                        nullptr,
                                        GetModuleHandle(nullptr),
                                        &app);
    if (app.status_window == nullptr) {
        throw std::runtime_error("cannot create status window");
    }
    app.status_text =
        CreateWindowExW(WS_EX_CLIENTEDGE,
                        L"EDIT",
                        L"",
                        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                        0,
                        0,
                        740,
                        520,
                        app.status_window,
                        nullptr,
                        GetModuleHandle(nullptr),
                        nullptr);
    if (app.status_text == nullptr) {
        DestroyWindow(app.status_window);
        throw std::runtime_error("cannot create status text view");
    }
    SendMessage(app.status_text, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(ANSI_FIXED_FONT)), TRUE);
    SetTimer(app.status_window, 1, 250, nullptr);
    refresh_status_window(app);
}

std::wstring controls_help_text(const AppState& app) {
    std::wstring text = L"Controles do jogo:\n";
    for (std::size_t index = 0; index < app.bindings.keys.size(); ++index) {
        const auto action = static_cast<ControlAction>(index);
        text += std::wstring(control_action_name(action),
                             control_action_name(action) + std::strlen(control_action_name(action))) +
                L": " + key_display_name(app.bindings.keys[index]) + L"\n";
    }
    text += L"\nAtalhos livres:\nSpace: pausar emulacao\nR: reset\nM: mute\n+ / -: volume\n"
            L"F1: overlay\nF5: salvar rapido\nF9: carregar rapido\n\n"
            L"Um atalho deixa de agir no host quando for atribuido a um controle do jogo.";
    return text;
}

void draw_overlay(HDC dc, const AppState& app) {
    if (!app.overlay_enabled) {
        return;
    }

    const std::string text = overlay_text(app);
    RECT background{8, 8, 680, 190};
    HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(dc, &background, brush);
    DeleteObject(brush);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(220, 240, 220));
    RECT text_rect{14, 12, 660, 184};
    DrawTextA(dc, text.c_str(), -1, &text_rect, DT_LEFT | DT_TOP | DT_NOCLIP);
}

void render_frame(HWND hwnd, AppState& app) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);

    const int client_width = client.right - client.left;
    const int client_height = client.bottom - client.top;
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    if (!app.has_rom) {
        EndPaint(hwnd, &paint);
        return;
    }
    const int active_height = app.host->console().vdp().active_height();
    const int scale = std::max(1, std::min(client_width / Vdp::width, client_height / active_height));
    const int output_width = Vdp::width * scale;
    const int output_height = active_height * scale;
    const int output_x = (client_width - output_width) / 2;
    const int output_y = (client_height - output_height) / 2;

    app.bitmap_info.bmiHeader.biHeight = -active_height;
    StretchDIBits(dc,
                  output_x,
                  output_y,
                  output_width,
                  output_height,
                  0,
                  0,
                  Vdp::width,
                  active_height,
                  app.host->framebuffer().data(),
                  &app.bitmap_info,
                  DIB_RGB_COLORS,
                  SRCCOPY);
    draw_overlay(dc, app);

    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<AppState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCT*>(lparam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_KEYDOWN:
        if (app != nullptr) {
            update_key(*app, wparam, true);
            update_menu_checks(hwnd, *app);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYUP:
        if (app != nullptr) {
            update_key(*app, wparam, false);
        }
        return 0;
    case WM_COMMAND:
        if (app == nullptr) {
            break;
        }
        if (const UINT command = LOWORD(wparam);
            command >= MenuLibraryFirst && command <= MenuLibraryLast &&
            static_cast<std::size_t>(command - MenuLibraryFirst) < app->game_library.size()) {
            try {
                const std::filesystem::path game = app->game_library[command - MenuLibraryFirst].path;
                load_game_session(hwnd, *app, game);
            } catch (const std::exception& error) {
                MessageBoxA(hwnd, error.what(), "SG3000Recomp - Erro na biblioteca", MB_OK | MB_ICONERROR);
            }
            update_menu_checks(hwnd, *app);
            return 0;
        }
        if (const UINT command = LOWORD(wparam);
            command >= MenuRecentFirst && command <= MenuRecentLast &&
            static_cast<std::size_t>(command - MenuRecentFirst) < app->recent_games.size()) {
            try {
                const std::filesystem::path game = app->recent_games[command - MenuRecentFirst];
                load_game_session(hwnd, *app, game);
            } catch (const std::exception& error) {
                MessageBoxA(hwnd, error.what(), "SG3000Recomp - Erro ao abrir ROM", MB_OK | MB_ICONERROR);
            }
            update_menu_checks(hwnd, *app);
            return 0;
        }
        if (const UINT command = LOWORD(wparam); command >= MenuScaleFirst && command <= MenuScaleLast) {
            resize_game_window(hwnd, *app, static_cast<int>(command - MenuScaleFirst + 1));
            update_menu_checks(hwnd, *app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (const UINT command = LOWORD(wparam);
            command >= MenuProfileMapperFirst && command <= MenuProfileMapperLast && app->has_rom) {
            try {
                store_game_profile(
                    hwnd, *app, graphical_profile_mappers[command - MenuProfileMapperFirst], std::nullopt);
            } catch (const std::exception& error) {
                MessageBoxA(hwnd, error.what(), "SG3000Recomp - Erro de perfil", MB_OK | MB_ICONERROR);
            }
            update_menu_checks(hwnd, *app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (const UINT command = LOWORD(wparam); command >= MenuControlFirst && command <= MenuControlLast) {
            request_control_binding(hwnd, *app, static_cast<ControlAction>(command - MenuControlFirst));
            return 0;
        }
        switch (LOWORD(wparam)) {
        case MenuFileExit:
            DestroyWindow(hwnd);
            return 0;
        case MenuFileOpenRom:
            if (const auto rom = choose_rom_file()) {
                try {
                    load_game_session(hwnd, *app, *rom);
                } catch (const std::exception& error) {
                    MessageBoxA(hwnd, error.what(), "SG3000Recomp - Erro ao abrir ROM", MB_OK | MB_ICONERROR);
                }
            }
            update_menu_checks(hwnd, *app);
            return 0;
        case MenuFileSelectBios:
            try {
                select_session_bios(hwnd, *app);
            } catch (const std::exception& error) {
                MessageBoxA(hwnd, error.what(), "SG3000Recomp - Erro ao abrir BIOS", MB_OK | MB_ICONERROR);
            }
            update_menu_checks(hwnd, *app);
            return 0;
        case MenuFileClearBios:
            try {
                clear_session_bios(hwnd, *app);
            } catch (const std::exception& error) {
                MessageBoxA(hwnd, error.what(), "SG3000Recomp - Erro ao remover BIOS", MB_OK | MB_ICONERROR);
            }
            update_menu_checks(hwnd, *app);
            return 0;
        case MenuProfileVideoNtsc:
        case MenuProfileVideoPal:
        case MenuProfileSave:
        case MenuProfileRemove:
            try {
                if (LOWORD(wparam) == MenuProfileVideoNtsc) {
                    store_game_profile(hwnd, *app, std::nullopt, HostVideoStandard::Ntsc);
                } else if (LOWORD(wparam) == MenuProfileVideoPal) {
                    store_game_profile(hwnd, *app, std::nullopt, HostVideoStandard::Pal);
                } else if (LOWORD(wparam) == MenuProfileSave) {
                    store_game_profile(hwnd, *app);
                } else {
                    remove_game_profile(hwnd, *app);
                }
            } catch (const std::exception& error) {
                MessageBoxA(hwnd, error.what(), "SG3000Recomp - Erro de perfil", MB_OK | MB_ICONERROR);
            }
            update_menu_checks(hwnd, *app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case MenuLibrarySetAlias:
        case MenuLibraryClearAlias:
            try {
                if (LOWORD(wparam) == MenuLibrarySetAlias) {
                    edit_current_game_alias(hwnd, *app);
                } else {
                    clear_current_game_alias(*app);
                }
            } catch (const std::exception& error) {
                MessageBoxA(hwnd, error.what(), "SG3000Recomp - Erro na biblioteca", MB_OK | MB_ICONERROR);
            }
            update_menu_checks(hwnd, *app);
            return 0;
        case MenuFileSaveState:
        case MenuFileLoadState:
        case MenuFileScreenshot:
        case MenuAudioStartRecording:
        case MenuAudioStopAndSave:
        case MenuAudioSaveLast:
        case MenuAudioClear:
            try {
                switch (LOWORD(wparam)) {
                case MenuFileSaveState:
                    save_state_as(*app);
                    break;
                case MenuFileLoadState:
                    load_state_from_file(*app);
                    break;
                case MenuFileScreenshot:
                    save_screenshot(*app);
                    break;
                case MenuAudioStartRecording:
                    start_audio_recording(*app);
                    break;
                case MenuAudioStopAndSave:
                    stop_audio_recording(*app);
                    break;
                case MenuAudioSaveLast:
                    if (save_recorded_audio(*app)) {
                        app->recorded_audio.clear();
                    }
                    break;
                case MenuAudioClear:
                    app->recorded_audio.clear();
                    app->status_message = "gravacao descartada";
                    break;
                default:
                    break;
                }
            } catch (const std::exception& error) {
                MessageBoxA(hwnd, error.what(), "SG3000Recomp - Erro de arquivo", MB_OK | MB_ICONERROR);
            }
            update_menu_checks(hwnd, *app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case MenuEmulationPause:
            set_emulation_paused(*app, !app->emulation_paused);
            break;
        case MenuEmulationReset:
            reset_emulation(*app);
            break;
        case MenuModeAccurate:
            set_runtime_mode(*app, RuntimeMode::Accurate);
            break;
        case MenuModeEnhanced:
            if (!confirm_enhanced_mode(hwnd, *app)) {
                return 0;
            }
            set_runtime_mode(*app, RuntimeMode::Enhanced);
            break;
        case MenuEnhancementReduceFlicker:
            if (!app->host->console().enhancements().reduce_flicker && !confirm_enhanced_mode(hwnd, *app)) {
                return 0;
            }
            set_enhancement(*app, MenuEnhancementReduceFlicker, !app->host->console().enhancements().reduce_flicker);
            break;
        case MenuEnhancementDisableSpriteLimit:
            if (!app->host->console().enhancements().disable_sprite_limit && !confirm_enhanced_mode(hwnd, *app)) {
                return 0;
            }
            set_enhancement(
                *app, MenuEnhancementDisableSpriteLimit, !app->host->console().enhancements().disable_sprite_limit);
            break;
        case MenuEnhancementYm2612:
            if (!app->host->console().enhancements().enable_ym2612 && !confirm_enhanced_mode(hwnd, *app)) {
                return 0;
            }
            set_enhancement(*app, MenuEnhancementYm2612, !app->host->console().enhancements().enable_ym2612);
            break;
        case MenuViewOverlay:
            app->overlay_enabled = !app->overlay_enabled;
            break;
        case MenuViewStatus:
            try {
                show_status_window(hwnd, *app);
            } catch (const std::exception& error) {
                MessageBoxA(hwnd, error.what(), "SG3000Recomp - Erro de status", MB_OK | MB_ICONERROR);
            }
            break;
        case MenuControlReset:
            app->bindings = {};
            app->input = {};
            app->status_message = "controles restaurados";
            break;
        case MenuHelpControls:
            MessageBoxW(
                hwnd, controls_help_text(*app).c_str(), L"SG3000Recomp - Controles", MB_OK | MB_ICONINFORMATION);
            return 0;
        default:
            return 0;
        }
        update_menu_checks(hwnd, *app);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT:
        if (app != nullptr && app->host) {
            render_frame(hwnd, *app);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (app != nullptr) {
            app->running = false;
            if (app->status_window != nullptr && IsWindow(app->status_window)) {
                DestroyWindow(app->status_window);
            }
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

std::wstring recent_game_menu_label(std::size_t index, const std::filesystem::path& game) {
    std::wstring filename = game.filename().wstring();
    for (std::size_t position = 0; (position = filename.find(L'&', position)) != std::wstring::npos; position += 2) {
        filename.insert(position, 1, L'&');
    }
    return L"&" + std::to_wstring(index + 1) + L" " + filename;
}

HMENU create_application_menu(AppState& app) {
    const HMENU menu = CreateMenu();
    const HMENU file = CreatePopupMenu();
    const HMENU recent = CreatePopupMenu();
    const HMENU library = CreatePopupMenu();
    const HMENU audio_dump = CreatePopupMenu();
    const HMENU profile = CreatePopupMenu();
    const HMENU emulation = CreatePopupMenu();
    const HMENU enhancements = CreatePopupMenu();
    const HMENU view = CreatePopupMenu();
    const HMENU scale = CreatePopupMenu();
    const HMENU controls = CreatePopupMenu();
    const HMENU help = CreatePopupMenu();
    app.scale_menu = scale;
    app.controls_menu = controls;
    app.recent_menu = recent;
    app.library_menu = library;

    AppendMenuW(file, MF_STRING, MenuFileOpenRom, L"Abrir ROM...");
    AppendMenuW(file, MF_STRING, MenuFileSelectBios, L"Selecionar BIOS...");
    AppendMenuW(file, MF_STRING, MenuFileClearBios, L"Remover BIOS selecionada");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, MenuFileSaveState, L"Salvar estado como...");
    AppendMenuW(file, MF_STRING, MenuFileLoadState, L"Carregar estado...");
    AppendMenuW(file, MF_STRING, MenuFileScreenshot, L"Salvar screenshot...");
    AppendMenuW(audio_dump, MF_STRING, MenuAudioStartRecording, L"Iniciar gravacao");
    AppendMenuW(audio_dump, MF_STRING, MenuAudioStopAndSave, L"Parar e salvar...");
    AppendMenuW(audio_dump, MF_STRING, MenuAudioSaveLast, L"Salvar ultima gravacao...");
    AppendMenuW(audio_dump, MF_STRING, MenuAudioClear, L"Descartar ultima gravacao");
    AppendMenuW(file, MF_POPUP, reinterpret_cast<UINT_PTR>(audio_dump), L"Gravacao de audio");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(profile, MF_STRING, MenuProfileMapperFirst + 0, L"Mapper automatico");
    AppendMenuW(profile, MF_STRING, MenuProfileMapperFirst + 1, L"Mapper linear/plain");
    AppendMenuW(profile, MF_STRING, MenuProfileMapperFirst + 2, L"Mapper Sega");
    AppendMenuW(profile, MF_STRING, MenuProfileMapperFirst + 3, L"Mapper Codemasters");
    AppendMenuW(profile, MF_STRING, MenuProfileMapperFirst + 4, L"Mapper Korean");
    AppendMenuW(profile, MF_STRING, MenuProfileMapperFirst + 5, L"Mapper Korean 8K");
    AppendMenuW(profile, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(profile, MF_STRING, MenuProfileVideoNtsc, L"Video NTSC");
    AppendMenuW(profile, MF_STRING, MenuProfileVideoPal, L"Video PAL");
    AppendMenuW(profile, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(profile, MF_STRING, MenuProfileSave, L"Salvar configuracao atual");
    AppendMenuW(profile, MF_STRING, MenuProfileRemove, L"Remover perfil deste jogo");
    AppendMenuW(file, MF_POPUP, reinterpret_cast<UINT_PTR>(profile), L"Perfil do jogo");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    refresh_library_menu(app);
    AppendMenuW(file, MF_POPUP, reinterpret_cast<UINT_PTR>(library), L"Biblioteca local");
    if (app.recent_games.empty()) {
        AppendMenuW(recent, MF_STRING | MF_GRAYED, 0, L"(nenhum jogo recente)");
    } else {
        for (std::size_t index = 0; index < app.recent_games.size() && index < 10; ++index) {
            const std::wstring label = recent_game_menu_label(index, app.recent_games[index]);
            AppendMenuW(recent, MF_STRING, MenuRecentFirst + index, label.c_str());
        }
    }
    AppendMenuW(file, MF_POPUP, reinterpret_cast<UINT_PTR>(recent), L"Jogos recentes");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, MenuFileExit, L"Sair");
    AppendMenuW(emulation, MF_STRING, MenuEmulationPause, L"Pausar\tSpace");
    AppendMenuW(emulation, MF_STRING, MenuEmulationReset, L"Soft reset\tR");
    AppendMenuW(enhancements, MF_STRING, MenuModeAccurate, L"Modo fiel (accurate)");
    AppendMenuW(enhancements, MF_STRING, MenuModeEnhanced, L"Modo enhanced");
    AppendMenuW(enhancements, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(enhancements, MF_STRING, MenuEnhancementReduceFlicker, L"Reduzir flicker");
    AppendMenuW(enhancements, MF_STRING, MenuEnhancementDisableSpriteLimit, L"Desativar limite de sprites");
    AppendMenuW(enhancements, MF_STRING, MenuEnhancementYm2612, L"YM2612 experimental (portas F4-F7)");
    AppendMenuW(view, MF_STRING, MenuViewOverlay, L"Overlay de diagnostico\tF1");
    AppendMenuW(view, MF_STRING, MenuViewStatus, L"Status detalhado...");
    for (UINT factor = 1; factor <= 6; ++factor) {
        const std::wstring label = std::to_wstring(factor) + L"x";
        AppendMenuW(scale, MF_STRING, MenuScaleFirst + factor - 1, label.c_str());
    }
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(scale), L"Tamanho da tela");
    for (std::size_t index = 0; index < app.bindings.keys.size(); ++index) {
        const auto action = static_cast<ControlAction>(index);
        const std::wstring label =
            std::wstring(control_action_name(action),
                         control_action_name(action) + std::strlen(control_action_name(action))) +
            L"\t" + key_display_name(app.bindings.keys[index]);
        AppendMenuW(controls, MF_STRING, MenuControlFirst + index, label.c_str());
    }
    AppendMenuW(controls, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(controls, MF_STRING, MenuControlReset, L"Restaurar padrao");
    AppendMenuW(help, MF_STRING, MenuHelpControls, L"Controles");

    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"Arquivo");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(emulation), L"Emulacao");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(enhancements), L"Melhorias");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"Exibicao");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(controls), L"Controles");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"Ajuda");
    return menu;
}

HWND create_main_window(HINSTANCE instance, AppState& app, int scale) {
    constexpr const char* class_name = "SG3000RecompHostWindow";

    WNDCLASS wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClass(&wc);

    const HMENU menu = create_application_menu(app);
    RECT rect{0, 0, Vdp::width * scale, app.host->console().vdp().active_height() * scale};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, TRUE);

    HWND hwnd = CreateWindowEx(0,
                               class_name,
                               "SG3000Recomp Host",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               CW_USEDEFAULT,
                               CW_USEDEFAULT,
                               rect.right - rect.left,
                               rect.bottom - rect.top,
                               nullptr,
                               menu,
                               instance,
                               &app);
    if (hwnd == nullptr) {
        DestroyMenu(menu);
        throw std::runtime_error("cannot create host window");
    }
    update_menu_checks(hwnd, app);
    return hwnd;
}

void run_message_loop(HWND hwnd, AppState& app) {
    using clock = std::chrono::steady_clock;
    auto next_frame = clock::now();

    while (app.running) {
        MSG message{};
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                app.running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        if (!app.running) {
            break;
        }

        if (!app.has_rom) {
            if (app.quit_after_frames != 0 && ++app.rendered_frames >= app.quit_after_frames) {
                app.running = false;
                DestroyWindow(hwnd);
                continue;
            }
            Sleep(10);
            next_frame = clock::now();
            continue;
        }

        const auto now = clock::now();
        const auto frame_duration = std::chrono::duration<double>(
            static_cast<double>(app.host->config().cycles_per_frame()) / app.host->config().cpu_clock_hz);
        if (now >= next_frame) {
            if (app.emulation_paused) {
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateWindow(hwnd);
                next_frame = now + std::chrono::duration_cast<clock::duration>(frame_duration);
                Sleep(1);
                continue;
            }
            app.last_frame = app.host->run_frame(app.input);
            ++app.rendered_frames;
            ++app.fps_window_frames;
            if (app.quit_after_frames != 0 && app.rendered_frames >= app.quit_after_frames) {
                app.running = false;
                DestroyWindow(hwnd);
                continue;
            }
            if (app.audio) {
                app.audio->submit(app.host->audio());
            }
            if (app.audio_recording) {
                const auto samples = app.host->audio();
                const std::size_t maximum_samples =
                    static_cast<std::size_t>(app.recorded_audio_sample_rate) * 2 * 60 * 120;
                const std::size_t available = maximum_samples - std::min(maximum_samples, app.recorded_audio.size());
                const std::size_t count = std::min(available, samples.size());
                app.recorded_audio.insert(app.recorded_audio.end(), samples.begin(), samples.begin() + count);
                if (count != samples.size() || app.recorded_audio.size() == maximum_samples) {
                    app.audio_recording = false;
                    app.status_message = "limite de duas horas atingido; gravacao mantida na memoria";
                    update_menu_checks(hwnd, app);
                }
            }
            app.host->clear_audio();

            const auto elapsed = now - app.fps_window_start;
            if (elapsed >= std::chrono::seconds(1)) {
                app.fps = static_cast<double>(app.fps_window_frames) / std::chrono::duration<double>(elapsed).count();
                app.fps_window_frames = 0;
                app.fps_window_start = now;
            }

            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
            next_frame += std::chrono::duration_cast<clock::duration>(frame_duration);
            if (next_frame < now) {
                next_frame = now;
            }
        } else {
            Sleep(1);
        }
    }
}

int run_empty_frontend(Options options,
                       const GraphicalSettings& graphical_settings,
                       const std::filesystem::path& settings_path) {
    AppState app;
    if (options.profile.empty()) {
        options.profile = graphical_user_data_root() / L"profiles.txt";
    }
    HostRuntimeConfig runtime_config = host_runtime_config_for_video_standard(options.video_standard);
    runtime_config.audio_sample_rate = options.audio_sample_rate;
    app.host = std::make_unique<HostRuntime>(options.model, options.enhancements, runtime_config);
    app.host->console().bus().set_mapper(options.mapper);
    app.session_options = options;
    app.overlay_enabled = options.overlay;
    app.window_scale = options.scale;
    app.quit_after_frames = options.quit_after_frames;
    app.bindings = graphical_settings.bindings;
    app.recent_games = load_recent_games(graphical_user_data_root() / L"recent-games.txt");
    save_recent_games(graphical_user_data_root() / L"recent-games.txt", app.recent_games);
    app.game_library = load_game_library(graphical_user_data_root() / L"game-library.txt");
    save_game_library(graphical_user_data_root() / L"game-library.txt", app.game_library);
    app.bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    app.bitmap_info.bmiHeader.biWidth = Vdp::width;
    app.bitmap_info.bmiHeader.biHeight = -Vdp::height;
    app.bitmap_info.bmiHeader.biPlanes = 1;
    app.bitmap_info.bmiHeader.biBitCount = 32;
    app.bitmap_info.bmiHeader.biCompression = BI_RGB;
    app.status_message = "nenhum jogo carregado";

    configure_session_audio(app, options);
    if (app.audio) {
        app.audio->set_volume_percent(graphical_settings.volume_percent);
        app.audio->set_muted(graphical_settings.muted);
    }

    HWND hwnd = create_main_window(GetModuleHandle(nullptr), app, options.scale);
    SetWindowTextA(hwnd, "SG3000Recomp - Nenhum jogo carregado");
    if (options.show_status_window) {
        show_status_window(hwnd, app);
    }
    if (!options.rom.empty()) {
        load_game_session(hwnd, app, options.rom);
        update_menu_checks(hwnd, app);
    }
    run_message_loop(hwnd, app);
    save_active_sram(app);
    save_graphical_settings(settings_path, app, graphical_settings);
    return 0;
}

int run(int argc, char** argv) {
    Options opts = parse_args(argc, argv);
    const std::filesystem::path graphical_settings_path = graphical_user_data_root() / L"settings.ini";
    GraphicalSettings graphical_settings;
    if (opts.gui_launch) {
        graphical_settings = load_graphical_settings(graphical_settings_path);
        opts.overlay = graphical_settings.overlay;
        opts.scale = graphical_settings.window_scale;
        opts.enhancements.reduce_flicker = graphical_settings.reduce_flicker;
        opts.enhancements.disable_sprite_limit = graphical_settings.disable_sprite_limit;
        opts.enhancements.enable_ym2612 = graphical_settings.enable_ym2612;
        if (graphical_settings.enhanced_mode || opts.enhancements.reduce_flicker ||
            opts.enhancements.disable_sprite_limit || opts.enhancements.enable_ym2612) {
            opts.enhancements.mode = RuntimeMode::Enhanced;
        }
    }
    if (opts.gui_launch) {
        return run_empty_frontend(opts, graphical_settings, graphical_settings_path);
    }
    auto rom = normalize_rom_payload(read_file(opts.rom));
    const std::string rom_hash = rom_hash_fnv1a64(rom);
    const CartridgeHeaderInfo header = analyze_cartridge_header(rom);
    if (opts.print_hash) {
        std::cout << rom_hash << "\n";
        if (header.found) {
            std::cout << "header: " << cartridge_platform_name(cartridge_header_platform(header)) << ", "
                      << cartridge_region_name(header.region) << ", " << cartridge_size_code_name(header.region_size)
                      << ", offset 0x" << std::hex << header.offset << std::dec;
            if (header.declared_size_available) {
                std::cout << ", checksum " << (header.checksum_matches_declared_size ? "ok" : "mismatch");
            }
            std::cout << "\n";
        } else {
            std::cout << "header: not found\n";
        }
        return 0;
    }
    if (cartridge_header_is_game_gear(header) && opts.model == ConsoleModel::SMS) {
        std::cout << "warning: cartridge header identifies a Game Gear image; SMS host support is not expected to be "
                     "faithful yet\n";
    }
    std::string profile_name;
    std::string profile_fingerprint;
    if (!opts.profile.empty()) {
        const auto profiles = GameProfileDatabase::load(opts.profile);
        if (const GameProfile* profile = profiles.find_by_hash(rom_hash)) {
            profile_name = profile->name.empty() ? profile->hash : profile->name;
            profile_fingerprint = game_profile_fingerprint(*profile);
            if (profile->has_model) {
                opts.model = profile->model;
            }
            if (profile->has_mapper) {
                opts.mapper = profile->mapper;
            }
            if (profile->has_enhancements) {
                opts.enhancements = profile->enhancements;
            }
            if (profile->has_audio_latency_ms) {
                opts.audio_latency_ms = profile->audio_latency_ms;
            }
            if (profile->has_audio_sample_rate) {
                opts.audio_sample_rate = profile->audio_sample_rate;
            }
            if (profile->has_video_standard) {
                opts.video_standard = profile->video_standard;
            }
            std::cout << "profile matched: " << profile_name << "\n";
        } else {
            std::cout << "profile matched: none (" << rom_hash << ")\n";
        }
    }
    const std::optional<std::vector<u8>> bios =
        opts.bios.empty() ? std::optional<std::vector<u8>>{} : std::optional<std::vector<u8>>{read_file(opts.bios)};

    AppState app;
    const HostRuntimeConfig host_config = host_runtime_config_for_video_standard(opts.video_standard);
    HostRuntimeConfig runtime_config = host_config;
    runtime_config.audio_sample_rate = opts.audio_sample_rate;
    app.host = std::make_unique<HostRuntime>(opts.model, opts.enhancements, runtime_config);
    app.host->console().bus().set_mapper(opts.mapper);
    app.overlay_enabled = opts.overlay;
    app.quit_after_frames = opts.quit_after_frames;
    app.rom_hash = rom_hash;
    app.profile_name = profile_name;
    app.current_rom_path = std::filesystem::absolute(opts.rom);
    app.current_sram_path = opts.save_sram;
    app.session_options = opts;
    app.bindings = graphical_settings.bindings;
    app.window_scale = opts.scale;
    app.has_rom = true;
    if (bios) {
        app.host->load_bios(*bios);
    }
    app.host->load_rom(rom);
    if (!opts.load_sram.empty()) {
        app.host->console().bus().load_cartridge_ram(read_file(opts.load_sram));
    }
    SaveStateMetadata expected_state_metadata;
    expected_state_metadata.present = true;
    expected_state_metadata.model = opts.model;
    expected_state_metadata.rom_hash = rom_hash;
    expected_state_metadata.bios_hash = bios ? rom_hash_fnv1a64(*bios) : std::string{};
    expected_state_metadata.profile_fingerprint = profile_fingerprint;
    app.state_metadata = expected_state_metadata;
    if (!opts.load_state.empty()) {
        const auto state_bytes = read_file(opts.load_state);
        if (!opts.force_state) {
            validate_save_state_metadata(read_save_state_metadata(state_bytes), expected_state_metadata);
        }
        load_console_state(app.host->console(), state_bytes);
    }

    app.bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    app.bitmap_info.bmiHeader.biWidth = Vdp::width;
    app.bitmap_info.bmiHeader.biHeight = -app.host->console().vdp().active_height();
    app.bitmap_info.bmiHeader.biPlanes = 1;
    app.bitmap_info.bmiHeader.biBitCount = 32;
    app.bitmap_info.bmiHeader.biCompression = BI_RGB;

    if (opts.audio) {
        app.audio = std::make_unique<Win32Audio>();
        if (!app.audio->open(app.host->config().audio_sample_rate, opts.audio_latency_ms)) {
            std::cerr << "sgrecomp_host: audio device unavailable, continuing muted\n";
            app.audio.reset();
        }
    }

    HINSTANCE instance = GetModuleHandle(nullptr);
    HWND hwnd = create_main_window(instance, app, opts.scale);
    const std::string window_title = "SG3000Recomp - " + opts.rom.filename().string();
    SetWindowTextA(hwnd, window_title.c_str());
    run_message_loop(hwnd, app);
    if (!opts.save_sram.empty()) {
        const auto& sram = app.host->console().bus().debug_cartridge_ram();
        write_binary_file(opts.save_sram, std::span<const u8>(sram.data(), sram.size()));
        std::cout << "sram saved: " << opts.save_sram.string()
                  << (app.host->console().bus().cartridge_ram_dirty() ? " (dirty)" : " (unchanged)") << "\n";
    }
    if (!opts.save_state.empty()) {
        const auto bytes = save_console_state(app.host->console(), expected_state_metadata);
        write_binary_file(opts.save_state, std::span<const u8>(bytes.data(), bytes.size()));
        std::cout << "state saved: " << opts.save_state.string() << "\n";
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "sgrecomp_host: " << e.what() << "\n";
        print_usage();
        MessageBoxA(nullptr, e.what(), "SG3000Recomp Host", MB_ICONERROR | MB_OK);
        return 1;
    }
}
