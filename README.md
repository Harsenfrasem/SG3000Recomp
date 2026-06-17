# SG3000Recomp

Static recompilation toolkit for SMS and SG-3000 ROMs.

The project follows a documented pipeline style:

1. Load a cartridge ROM and model its visible Z80 address space.
2. Decode Z80 instructions into an analyzable instruction stream.
3. Lift supported instructions into native C++ ahead of time.
4. Link generated code with a small console runtime.
5. Fall back to the checked interpreter for instructions that are not lifted yet.

This repository is intentionally not a clone of any existing recompilation project. It borrows the project discipline of clear tools, runtime, templates, configuration, and docs.

## Current Status

- Z80 CPU state, decoder, and fallback interpreter for the first instruction subset.
- SMS and SG-3000 runtime shell: bus, ROM mapping, RAM writes, VDP ports, PSG latch, joypad reads.
- `sgrecomp` CLI that can disassemble, analyze reachable basic blocks, or generate a C++ instruction dispatcher.
- Smoke tests for the Z80 runtime and for generated C++ compilation.

Full compatibility requires completing the Z80/CB/DD/FD/ED opcode tables, exact cycle accounting, SMapper variants, VDP rendering modes, PSG synthesis, pause/NMI behavior, and ROM database heuristics.

## Build

Portable local build, already validated for this workspace:

```powershell
$env:PATH="$env:APPDATA\Python\Python314\Scripts;$env:PATH"
cmake --preset zig-debug
cmake --build --preset zig-debug
ctest --preset zig-debug
```

If your Visual Studio C++ tools are registered with CMake, this preset is also available:

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

## Use

```powershell
build\Debug\sgrecomp.exe game.sms -o generated_game.cpp
build\Debug\sgrecomp.exe game.sms --disasm
build\Debug\sgrecomp.exe game.sms -o generated_game.cpp --dump-analysis analysis.txt
build\Debug\sgrecomp.exe game.sg --model sg3000 -o generated_sg3000.cpp
```

`--dump-analysis` writes a static report with reachable basic blocks, static successors, direct-emitted instructions, fallback instructions, and indirect exits. It is the easiest way to see how close a ROM is to the current lifted C++ path.

Runtime smoke execution is useful before full video/audio support exists:

```powershell
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --steps 50000
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --steps 200 --trace
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --dump-frame out\frame.ppm --dump-frame-bmp out\frame.bmp --dump-audio out\audio.wav --dump-vgm out\audio.vgm
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --mapper auto --dump-frame-bmp out\frame-auto.bmp
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --mapper plain --dump-frame-bmp out\frame-plain.bmp
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --enable-fm --dump-audio out\fm.wav --dump-fm-log out\fm-writes.csv
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --dump-io-log out\io.csv --dump-tilemap out\tilemap.csv --dump-sprites out\sprites.csv
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --dump-memory-log out\memory.csv --watch 0xc000-0xc0ff --dump-vdp-log out\vdp.csv --watch-vdp 0x0000-0x03ff --io-port 0xbe
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --save-state local-saves\game.sgstate
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --load-state local-saves\game.sgstate --steps 50000
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --disable-sprite-limit
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --reduce-flicker
.\build\zig-debug\sgrecomp.exe game.sms --run-host --frames 3 --dump-frame-bmp out\host-frame.bmp --dump-audio out\host-audio.wav
.\build\zig-debug\sgrecomp_host.exe game.sms --bios bios.sms --scale 3
.\build\zig-debug\sgrecomp_host.exe game.sms --mute
.\build\zig-debug\sgrecomp_host.exe game.sms --no-overlay
.\build\zig-debug\sgrecomp_host.exe game.sms --audio-latency-ms 100
.\build\zig-debug\sgrecomp_host.exe game.sms --load-sram local-saves\game.sav --save-sram local-saves\game.sav
.\build\zig-debug\sgrecomp_host.exe game.sms --load-state local-saves\game.sgstate --save-state local-saves\game.sgstate
.\build\zig-debug\sgrecomp_host.exe game.sms --profile config\profiles.example.txt
.\build\zig-debug\sgrecomp_host.exe game.sms --print-hash
```

If execution reaches an unsupported opcode, the tool prints the instruction, PC, registers, interrupt state, and cycle count.

Enhancements are off by default. Smoke summaries print the active mode and enhancement flags so compatibility tests can confirm whether they are running in accurate or enhanced mode.

Mapper selection can be forced with `--mapper auto|plain|smapper|cmapper|kmapper|k8k` in both `sgrecomp` and `sgrecomp_host`. Use this when a ROM executes but stays blank; `auto` is the default, while explicit mapper modes are useful for local compatibility matrices.

The runtime normalizes common SMS I/O mirrors: `0x40-0x7f` route counter reads and PSG writes, and `0x80-0xbf` route VDP data/control access by even/odd port.

`--dump-frame`, `--dump-frame-bmp`, `--dump-audio`, `--dump-vgm`, `--dump-fm-log`, `--dump-io-log`, `--dump-memory-log`, `--dump-vdp-log`, `--dump-tilemap`, and `--dump-sprites` are local smoke artifacts for visual/audio inspection and reverse engineering. Keep them under ignored directories such as `out/` when testing private ROMs. BMP is convenient for opening a quick frame preview on Windows; PPM remains a simple raw technical frame dump. WAV is useful for listening to the current PSG/FM renderer; VGM is useful for inspecting captured PSG writes with timing; FM, I/O, memory, and VDP CSV logs capture runtime activity; tilemap and sprite CSVs expose VDP tables in a readable form. Use `--watch`, `--watch-vdp`, and `--io-port` to filter noisy logs.

Sprite enhancements currently keep the original overflow status bit. Accurate mode renders the original 8 sprites per scanline, `--reduce-flicker` raises that render limit to 16, and `--disable-sprite-limit` renders all visible sprites on the scanline. `--enable-fm` enables the optional FM path for software/profile testing; the current FM synthesis is diagnostic plumbing and still needs a faithful YM2413/OPLL core.

Save states are local binary snapshots of mutable runtime state. They do not embed ROM or BIOS bytes, so load the same software first and keep `.sgstate` files under ignored local folders such as `local-saves/`. New save states include the ROM hash and console model; loading refuses mismatches unless `--force-state` is used for debugging.

`--run-host` uses the headless host runtime path. It advances full frames, samples audio at 44.1 kHz, applies joypad state through the runtime API, and exposes the latest framebuffer for a future window backend.

On Windows, `sgrecomp_host` opens the first native video/input/audio host window. Arrow keys map to the directional pad, `Z`/`X` map to the two action buttons, and `Enter` sends Pause/NMI. `Space` pauses emulation, `R` resets the runtime, `M` mutes audio, and `+`/`-` adjust volume. Audio uses the Win32 waveOut backend, can be disabled with `--mute`, and accepts `--audio-latency-ms`. Local cartridge RAM can be loaded and saved with `--load-sram` and `--save-sram`; keep those files under ignored local folders. `--profile` loads hash-based local profiles for model, enhancements, and audio latency without storing ROM paths, and `--print-hash` prints the local ROM hash plus detected cartridge header metadata needed for a profile. The debug overlay shows FPS, frame count, PC, runtime mode, mapper/banks, VDP line/status/IRQ state, pause state, volume, audio queue, underruns, dropped buffers, ROM hash, and matched profile; press `F1` to toggle it or start with `--no-overlay`.

Generated code exposes:

```cpp
extern "C" void sgrecomp_load_rom(sgrecomp::Bus& bus);
extern "C" void sgrecomp_run_instruction(sgrecomp::Z80State& cpu, sgrecomp::Bus& bus);
```

## Layout

- `src/tools`: ROM analysis and code generation tools.
- `src/runtime`: console runtime linked by generated projects.
- `include/sgrecomp`: public runtime API.
- `docs`: architecture and implementation notes.
- `templates/project`: starter project for recompiled ROM ports.
- `config`: example target configuration.

## Continuation Checklist

See `docs/CHECKLIST.md` for the implementation roadmap and `docs/BUILDING.md` for toolchain details.
