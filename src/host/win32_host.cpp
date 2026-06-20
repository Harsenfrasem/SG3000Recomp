#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "sgrecomp/enhancements.h"
#include "sgrecomp/cartridge.h"
#include "sgrecomp/game_profile.h"
#include "sgrecomp/host_runtime.h"
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

struct AppState {
    std::unique_ptr<HostRuntime> host;
    std::unique_ptr<Win32Audio> audio;
    HostInputState input;
    HostFrameResult last_frame;
    std::string rom_hash;
    std::string profile_name;
    std::filesystem::path quick_state_path;
    std::vector<std::filesystem::path> recent_games;
    std::optional<std::filesystem::path> pending_gui_rom;
    SaveStateMetadata state_metadata;
    std::string status_message;
    BITMAPINFO bitmap_info{};
    bool running = true;
    bool emulation_paused = false;
    bool overlay_enabled = true;
    bool compatibility_warning_acknowledged = false;
    double fps = 0.0;
    u64 rendered_frames = 0;
    std::size_t quit_after_frames = 0;
    std::chrono::steady_clock::time_point fps_window_start = std::chrono::steady_clock::now();
    u64 fps_window_frames = 0;
};

struct GraphicalSettings {
    bool overlay = true;
    bool enhanced_mode = false;
    bool reduce_flicker = false;
    bool disable_sprite_limit = false;
    bool muted = false;
    int volume_percent = 100;
};

enum MenuCommand : UINT {
    MenuFileExit = 1000,
    MenuFileOpenRom,
    MenuEmulationPause,
    MenuEmulationReset,
    MenuModeAccurate,
    MenuModeEnhanced,
    MenuEnhancementReduceFlicker,
    MenuEnhancementDisableSpriteLimit,
    MenuViewOverlay,
    MenuHelpControls,
    MenuRecentFirst = 1100,
    MenuRecentLast = MenuRecentFirst + 9,
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
        } else if (key == "muted") {
            settings.muted = parse_setting_bool(value, settings.muted);
        } else if (key == "volume_percent") {
            try {
                settings.volume_percent = std::clamp(std::stoi(value), 0, 100);
            } catch (const std::exception&) {
                // Keep the default when a local setting was edited incorrectly.
            }
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
    file << "version=2\n"
         << "overlay=" << (app.overlay_enabled ? 1 : 0) << "\n"
         << "enhanced_mode=" << (enhancements.mode == RuntimeMode::Enhanced ? 1 : 0) << "\n"
         << "reduce_flicker=" << (enhancements.reduce_flicker ? 1 : 0) << "\n"
         << "disable_sprite_limit=" << (enhancements.disable_sprite_limit ? 1 : 0) << "\n"
         << "muted=" << (muted ? 1 : 0) << "\n"
         << "volume_percent=" << volume << "\n";
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
        << "usage: sgrecomp_host                         (open graphical ROM/BIOS selectors)\n"
        << "       sgrecomp_host <rom.sms|rom.sg> [--bios bios.sms] [--model sms|sg3000] [--mapper "
           "auto|plain|smapper|cmapper|kmapper|k8k]\n"
        << "                    [--video-standard ntsc|pal]\n"
        << "                    [--scale n] [--mute] [--no-overlay] [--audio-latency-ms n] [--audio-sample-rate hz]\n"
        << "                    [--load-sram save.sav] [--save-sram save.sav]\n"
        << "                    [--load-state state.sgstate] [--save-state state.sgstate] [--force-state]\n"
        << "                    [--profile profiles.txt]\n"
        << "                    [--print-hash]\n"
        << "                    [--quit-after-frames n]\n"
        << "                    [--disable-sprite-limit] [--reduce-flicker] [--enable-fm]\n";
}

bool launch_graphical_host(const std::optional<std::filesystem::path>& rom) {
    std::array<wchar_t, 32768> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        return false;
    }
    std::wstring command = L"\"" + std::wstring(executable.data(), length) + L"\"";
    if (rom && !rom->empty()) {
        command += L" --gui-rom \"" + rom->wstring() + L"\"";
    }
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL launched = CreateProcessW(
        executable.data(), mutable_command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process);
    if (launched) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    return launched != FALSE;
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

bool complete_graphical_launch_options(Options& opts) {
    static constexpr wchar_t rom_filter[] = L"ROMs Sega (*.sms;*.sg;*.bin;*.rom)\0*.sms;*.sg;*.bin;*.rom\0"
                                            L"Todos os arquivos (*.*)\0*.*\0\0";
    static constexpr wchar_t bios_filter[] = L"BIOS Sega (*.sms;*.bin;*.rom)\0*.sms;*.bin;*.rom\0"
                                             L"Todos os arquivos (*.*)\0*.*\0\0";

    if (opts.rom.empty()) {
        const auto rom = choose_local_file(L"Selecione a ROM para jogar", rom_filter);
        if (!rom) {
            return false;
        }
        opts.rom = *rom;
    }

    const int bios_choice =
        MessageBoxW(nullptr,
                    L"Deseja selecionar uma BIOS?\n\nSim: escolher BIOS local\nNão: iniciar diretamente pelo cartucho",
                    L"SG3000Recomp - BIOS opcional",
                    MB_ICONQUESTION | MB_YESNOCANCEL);
    if (bios_choice == IDCANCEL) {
        return false;
    }
    if (bios_choice == IDYES) {
        const auto bios = choose_local_file(L"Selecione a BIOS", bios_filter);
        if (!bios) {
            return false;
        }
        opts.bios = *bios;
    }
    return true;
}

u8 button_for_key(WPARAM key) {
    switch (key) {
    case VK_UP:
        return Joypad::Up;
    case VK_DOWN:
        return Joypad::Down;
    case VK_LEFT:
        return Joypad::Left;
    case VK_RIGHT:
        return Joypad::Right;
    case 'Z':
        return Joypad::Button1;
    case 'X':
        return Joypad::Button2;
    default:
        return 0;
    }
}

void set_emulation_paused(AppState& app, bool paused) {
    app.emulation_paused = paused;
    if (app.audio) {
        app.audio->set_paused(paused);
    }
}

void reset_emulation(AppState& app) {
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

void request_gui_relaunch(HWND hwnd, AppState& app, std::filesystem::path rom) {
    app.pending_gui_rom = std::move(rom);
    DestroyWindow(hwnd);
}

bool confirm_enhanced_mode(HWND hwnd, AppState& app) {
    if (app.compatibility_warning_acknowledged) {
        return true;
    }
    const int result =
        MessageBoxW(hwnd,
                    L"O modo enhanced pode alterar a aparencia original do jogo.\n\n"
                    L"Reducao de flicker e remocao do limite de sprites nao representam "
                    L"exatamente o hardware historico. Voce pode voltar ao modo fiel a qualquer momento.\n\n"
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
    const auto& config = app.host->console().enhancements();
    check(MenuEmulationPause, app.emulation_paused);
    CheckMenuRadioItem(menu,
                       MenuModeAccurate,
                       MenuModeEnhanced,
                       config.mode == RuntimeMode::Accurate ? MenuModeAccurate : MenuModeEnhanced,
                       MF_BYCOMMAND);
    check(MenuEnhancementReduceFlicker, config.reduce_flicker);
    check(MenuEnhancementDisableSpriteLimit, config.disable_sprite_limit);
    check(MenuViewOverlay, app.overlay_enabled);
}

void update_key(AppState& app, WPARAM key, bool pressed) {
    const u8 button = button_for_key(key);
    if (button != 0) {
        if (pressed) {
            app.input.player1 = static_cast<u8>(app.input.player1 | button);
        } else {
            app.input.player1 = static_cast<u8>(app.input.player1 & ~button);
        }
    }
    if (key == VK_RETURN) {
        app.input.pause = pressed;
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
        << "  " << (app.emulation_paused ? "paused" : "running") << "\n"
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
    const int active_height = app.host->console().vdp().active_height();
    const int scale = std::max(1, std::min(client_width / Vdp::width, client_height / active_height));
    const int output_width = Vdp::width * scale;
    const int output_height = active_height * scale;
    const int output_x = (client_width - output_width) / 2;
    const int output_y = (client_height - output_height) / 2;

    FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
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
            command >= MenuRecentFirst && command <= MenuRecentLast &&
            static_cast<std::size_t>(command - MenuRecentFirst) < app->recent_games.size()) {
            request_gui_relaunch(hwnd, *app, app->recent_games[command - MenuRecentFirst]);
            return 0;
        }
        switch (LOWORD(wparam)) {
        case MenuFileExit:
            DestroyWindow(hwnd);
            return 0;
        case MenuFileOpenRom:
            request_gui_relaunch(hwnd, *app, {});
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
        case MenuViewOverlay:
            app->overlay_enabled = !app->overlay_enabled;
            break;
        case MenuHelpControls:
            MessageBoxW(hwnd,
                        L"Setas: direcional\nZ / X: botoes 1 e 2\nEnter: Pause/NMI do console\n"
                        L"Space: pausar emulacao\nR: reset\nM: mute\n+ / -: volume\n"
                        L"F1: overlay\nF5: salvar rapido\nF9: carregar rapido",
                        L"SG3000Recomp - Controles",
                        MB_OK | MB_ICONINFORMATION);
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

HMENU create_application_menu(const AppState& app) {
    const HMENU menu = CreateMenu();
    const HMENU file = CreatePopupMenu();
    const HMENU recent = CreatePopupMenu();
    const HMENU emulation = CreatePopupMenu();
    const HMENU enhancements = CreatePopupMenu();
    const HMENU view = CreatePopupMenu();
    const HMENU help = CreatePopupMenu();

    AppendMenuW(file, MF_STRING, MenuFileOpenRom, L"Abrir outro jogo...");
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
    AppendMenuW(emulation, MF_STRING, MenuEmulationReset, L"Resetar\tR");
    AppendMenuW(enhancements, MF_STRING, MenuModeAccurate, L"Modo fiel (accurate)");
    AppendMenuW(enhancements, MF_STRING, MenuModeEnhanced, L"Modo enhanced");
    AppendMenuW(enhancements, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(enhancements, MF_STRING, MenuEnhancementReduceFlicker, L"Reduzir flicker");
    AppendMenuW(enhancements, MF_STRING, MenuEnhancementDisableSpriteLimit, L"Desativar limite de sprites");
    AppendMenuW(view, MF_STRING, MenuViewOverlay, L"Overlay de diagnostico\tF1");
    AppendMenuW(help, MF_STRING, MenuHelpControls, L"Controles");

    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"Arquivo");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(emulation), L"Emulacao");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(enhancements), L"Melhorias");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"Exibicao");
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
    const auto frame_duration = std::chrono::duration<double>(
        static_cast<double>(app.host->config().cycles_per_frame()) / app.host->config().cpu_clock_hz);
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

        const auto now = clock::now();
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

int run(int argc, char** argv) {
    Options opts = parse_args(argc, argv);
    if (opts.gui_launch && !complete_graphical_launch_options(opts)) {
        return 0;
    }
    const std::filesystem::path graphical_settings_path = graphical_user_data_root() / L"settings.ini";
    GraphicalSettings graphical_settings;
    if (opts.gui_launch) {
        graphical_settings = load_graphical_settings(graphical_settings_path);
        opts.overlay = graphical_settings.overlay;
        opts.enhancements.reduce_flicker = graphical_settings.reduce_flicker;
        opts.enhancements.disable_sprite_limit = graphical_settings.disable_sprite_limit;
        if (graphical_settings.enhanced_mode || opts.enhancements.reduce_flicker ||
            opts.enhancements.disable_sprite_limit) {
            opts.enhancements.mode = RuntimeMode::Enhanced;
        }
    }
    auto rom = normalize_rom_payload(read_file(opts.rom));
    std::vector<std::filesystem::path> recent_games;
    if (opts.gui_launch) {
        const std::filesystem::path recent_games_path = graphical_user_data_root() / L"recent-games.txt";
        recent_games = touch_recent_game(load_recent_games(recent_games_path), std::filesystem::absolute(opts.rom));
        save_recent_games(recent_games_path, recent_games);
    }
    const std::string rom_hash = rom_hash_fnv1a64(rom);
    std::filesystem::path graphical_quick_state_path;
    if (opts.gui_launch) {
        const std::filesystem::path saves = graphical_user_data_root() / L"saves";
        std::filesystem::create_directories(saves);
        const std::string stem = hash_file_stem(rom_hash);
        const std::filesystem::path automatic_sram = saves / (stem + ".sav");
        graphical_quick_state_path = saves / (stem + ".sgstate");
        if (std::filesystem::exists(automatic_sram)) {
            opts.load_sram = automatic_sram;
        }
        opts.save_sram = automatic_sram;
    }
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
    app.quick_state_path = graphical_quick_state_path;
    app.recent_games = std::move(recent_games);
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
        } else if (opts.gui_launch) {
            app.audio->set_volume_percent(graphical_settings.volume_percent);
            app.audio->set_muted(graphical_settings.muted);
        }
    }

    HINSTANCE instance = GetModuleHandle(nullptr);
    HWND hwnd = create_main_window(instance, app, opts.scale);
    const std::string window_title = "SG3000Recomp - " + opts.rom.filename().string();
    SetWindowTextA(hwnd, window_title.c_str());
    run_message_loop(hwnd, app);
    if (opts.gui_launch) {
        try {
            save_graphical_settings(graphical_settings_path, app, graphical_settings);
        } catch (const std::exception& error) {
            std::cerr << "sgrecomp_host: " << error.what() << "\n";
        }
    }
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
    if (app.pending_gui_rom && !launch_graphical_host(app.pending_gui_rom)) {
        MessageBoxW(
            nullptr, L"Nao foi possivel abrir uma nova instancia da interface.", L"SG3000Recomp", MB_OK | MB_ICONERROR);
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
