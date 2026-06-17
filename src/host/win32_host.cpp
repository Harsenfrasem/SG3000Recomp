#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "sgrecomp/enhancements.h"
#include "sgrecomp/host_runtime.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace sgrecomp;

struct Options {
    std::filesystem::path rom;
    std::filesystem::path bios;
    ConsoleModel model = ConsoleModel::SMS;
    EnhancementConfig enhancements;
    int scale = 3;
};

struct AppState {
    std::unique_ptr<HostRuntime> host;
    HostInputState input;
    BITMAPINFO bitmap_info{};
    bool running = true;
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

void print_usage() {
    std::cout << "usage: sgrecomp_host <rom.sms|rom.sg> [--bios bios.sms] [--model sms|sg3000]\n"
              << "                    [--scale n] [--disable-sprite-limit] [--reduce-flicker]\n";
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
            app.host->run_frame(app.input);
            app.host->clear_audio();
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
    if (bios) {
        app.host->load_bios(*bios);
    }
    app.host->load_rom(rom);

    app.bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    app.bitmap_info.bmiHeader.biWidth = Vdp::width;
    app.bitmap_info.bmiHeader.biHeight = -Vdp::height;
    app.bitmap_info.bmiHeader.biPlanes = 1;
    app.bitmap_info.bmiHeader.biBitCount = 32;
    app.bitmap_info.bmiHeader.biCompression = BI_RGB;

    HINSTANCE instance = GetModuleHandle(nullptr);
    HWND hwnd = create_main_window(instance, app, opts.scale);
    run_message_loop(hwnd, app);
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
