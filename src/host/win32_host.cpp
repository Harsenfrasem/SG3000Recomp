#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "sgrecomp/enhancements.h"
#include "sgrecomp/host_runtime.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <chrono>
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
    ConsoleModel model = ConsoleModel::SMS;
    EnhancementConfig enhancements;
    int scale = 3;
    bool audio = true;
    bool overlay = true;
    int audio_latency_ms = 80;
    std::size_t quit_after_frames = 0;
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
        target_latency_frames_ = static_cast<std::size_t>(
            (static_cast<u64>(sample_rate_) * static_cast<u64>(target_latency_ms_)) / 1000);
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
        const DWORD channel = muted_
            ? 0
            : static_cast<DWORD>((volume_percent_ * 0xFFFF) / 100);
        waveOutSetVolume(device_, channel | (channel << 16));
    }
};

struct AppState {
    std::unique_ptr<HostRuntime> host;
    std::unique_ptr<Win32Audio> audio;
    HostInputState input;
    HostFrameResult last_frame;
    BITMAPINFO bitmap_info{};
    bool running = true;
    bool emulation_paused = false;
    bool overlay_enabled = true;
    double fps = 0.0;
    u64 rendered_frames = 0;
    std::size_t quit_after_frames = 0;
    std::chrono::steady_clock::time_point fps_window_start = std::chrono::steady_clock::now();
    u64 fps_window_frames = 0;
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

void print_usage() {
    std::cout << "usage: sgrecomp_host <rom.sms|rom.sg> [--bios bios.sms] [--model sms|sg3000]\n"
              << "                    [--scale n] [--mute] [--no-overlay] [--audio-latency-ms n]\n"
              << "                    [--load-sram save.sav] [--save-sram save.sav]\n"
              << "                    [--quit-after-frames n]\n"
              << "                    [--disable-sprite-limit] [--reduce-flicker]\n";
}

Options parse_args(int argc, char** argv) {
    Options opts;
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
        if (arg == "--load-sram" && i + 1 < argc) {
            opts.load_sram = argv[++i];
            continue;
        }
        if (arg == "--save-sram" && i + 1 < argc) {
            opts.save_sram = argv[++i];
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
        if (arg == "--audio-latency-ms" && i + 1 < argc) {
            opts.audio_latency_ms = std::clamp(std::stoi(argv[++i]), 10, 300);
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
        if (opts.rom.empty()) {
            opts.rom = arg;
            continue;
        }
        throw std::runtime_error("unexpected argument: " + arg);
    }
    if (opts.rom.empty()) {
        throw std::runtime_error("missing input ROM");
    }
    return opts;
}

u8 button_for_key(WPARAM key) {
    switch (key) {
    case VK_UP: return Joypad::Up;
    case VK_DOWN: return Joypad::Down;
    case VK_LEFT: return Joypad::Left;
    case VK_RIGHT: return Joypad::Right;
    case 'Z': return Joypad::Button1;
    case 'X': return Joypad::Button2;
    default: return 0;
    }
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
    if (key == VK_SPACE && pressed) {
        app.emulation_paused = !app.emulation_paused;
        if (app.audio) {
            app.audio->set_paused(app.emulation_paused);
        }
    }
    if (key == 'R' && pressed) {
        app.host->reset();
        app.last_frame = {};
        app.rendered_frames = 0;
        app.fps_window_frames = 0;
        app.fps = 0.0;
        if (app.audio) {
            app.audio->flush();
        }
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
    case RuntimeMode::Accurate: return "accurate";
    case RuntimeMode::Enhanced: return "enhanced";
    case RuntimeMode::Hybrid: return "hybrid";
    default: return "unknown";
    }
}

std::string overlay_text(const AppState& app) {
    const auto& config = app.host->console().enhancements();
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << "FPS " << app.fps
        << "  frame " << app.last_frame.frame_index
        << "  PC $" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
        << static_cast<int>(app.host->console().cpu().pc) << std::dec << "\n"
        << "mode " << runtime_mode_name(config.mode)
        << "  sprite_limit " << (config.disable_sprite_limit ? "off" : "on")
        << "  reduce_flicker " << (config.reduce_flicker ? "on" : "off")
        << "  " << (app.emulation_paused ? "paused" : "running") << "\n";

    if (app.audio) {
        app.audio->cleanup_completed_buffers();
        const auto stats = app.audio->stats();
        out << "audio " << (app.audio->muted() ? "muted" : "on")
            << "  vol " << app.audio->volume_percent() << "%"
            << "  queued " << stats.queued_buffers
            << "/" << app.audio->queued_latency_ms() << "ms"
            << " target " << app.audio->target_latency_ms() << "ms"
            << "  underruns " << stats.underruns
            << "  drops " << stats.dropped_buffers
            << "  " << app.audio->sample_rate() << " Hz";
    } else {
        out << "audio muted";
    }
    out << "\nF1 overlay  Space pause  R reset  M mute  +/- volume";
    return out.str();
}

void draw_overlay(HDC dc, const AppState& app) {
    if (!app.overlay_enabled) {
        return;
    }

    const std::string text = overlay_text(app);
    RECT background{8, 8, 388, 84};
    HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(dc, &background, brush);
    DeleteObject(brush);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(220, 240, 220));
    RECT text_rect{14, 12, 382, 82};
    DrawTextA(dc, text.c_str(), -1, &text_rect, DT_LEFT | DT_TOP | DT_NOCLIP);
}

void render_frame(HWND hwnd, AppState& app) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);

    const int client_width = client.right - client.left;
    const int client_height = client.bottom - client.top;
    const int scale = std::max(1, std::min(client_width / Vdp::width, client_height / Vdp::height));
    const int output_width = Vdp::width * scale;
    const int output_height = Vdp::height * scale;
    const int output_x = (client_width - output_width) / 2;
    const int output_y = (client_height - output_height) / 2;

    FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    StretchDIBits(
        dc,
        output_x,
        output_y,
        output_width,
        output_height,
        0,
        0,
        Vdp::width,
        Vdp::height,
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
        }
        return 0;
    case WM_KEYUP:
        if (app != nullptr) {
            update_key(*app, wparam, false);
        }
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

HWND create_main_window(HINSTANCE instance, AppState& app, int scale) {
    constexpr const char* class_name = "SG3000RecompHostWindow";

    WNDCLASS wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClass(&wc);

    RECT rect{0, 0, Vdp::width * scale, Vdp::height * scale};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowEx(
        0,
        class_name,
        "SG3000Recomp Host",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        instance,
        &app);
    if (hwnd == nullptr) {
        throw std::runtime_error("cannot create host window");
    }
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
                app.fps = static_cast<double>(app.fps_window_frames)
                    / std::chrono::duration<double>(elapsed).count();
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
    const Options opts = parse_args(argc, argv);
    auto rom = normalize_rom_payload(read_file(opts.rom));
    const std::optional<std::vector<u8>> bios = opts.bios.empty()
        ? std::optional<std::vector<u8>>{}
        : std::optional<std::vector<u8>>{read_file(opts.bios)};

    AppState app;
    app.host = std::make_unique<HostRuntime>(opts.model, opts.enhancements);
    app.overlay_enabled = opts.overlay;
    app.quit_after_frames = opts.quit_after_frames;
    if (bios) {
        app.host->load_bios(*bios);
    }
    app.host->load_rom(rom);
    if (!opts.load_sram.empty()) {
        app.host->console().bus().load_cartridge_ram(read_file(opts.load_sram));
    }

    app.bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    app.bitmap_info.bmiHeader.biWidth = Vdp::width;
    app.bitmap_info.bmiHeader.biHeight = -Vdp::height;
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
    run_message_loop(hwnd, app);
    if (!opts.save_sram.empty()) {
        const auto& sram = app.host->console().bus().debug_cartridge_ram();
        write_binary_file(opts.save_sram, std::span<const u8>(sram.data(), sram.size()));
        std::cout << "sram saved: " << opts.save_sram.string()
                  << (app.host->console().bus().cartridge_ram_dirty() ? " (dirty)" : " (unchanged)") << "\n";
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
