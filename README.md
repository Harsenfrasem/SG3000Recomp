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

- Z80 CPU state, decoder, and fallback interpreter with executable coverage across base, CB, ED, DD/FD, and DDCB/FDCB opcode pages; exact undocumented flags/timing still require external-suite conformance.
- SMS and SG-3000 runtime shell: bus, ROM mapping, RAM writes, VDP ports, PSG latch, joypad reads.
- Explicit Game Gear runtime with automatic header selection, 160x144 LCD viewport, 12-bit palette,
  Start input, and stereo PSG routing; current limits are documented in `docs/GAME_GEAR.md`.
- `sgrecomp` CLI that can disassemble, analyze reachable basic blocks, or generate a C++ instruction dispatcher.
- Smoke tests for the Z80 runtime and for generated C++ compilation.

Full compatibility requires completing external Z80 conformance, mapper variants, remaining VDP edge cases, faithful FM synthesis, timing validation, and ROM database heuristics.

The runtime now reaches active gameplay in four deterministic private regressions. One completed a five-minute NTSC session with continuous audio, save-state round-trip, and accurate/enhanced comparison; the other three completed shorter 1,200-frame gameplay probes with audio. This is a compatibility milestone, not a blanket compatibility claim. The evidence and remaining expansion gates are tracked in `docs/PLAYABLE_STATUS.md`.

Contributions follow [`CONTRIBUTING.md`](CONTRIBUTING.md). The per-ROM workflow is
documented in [`docs/PORTING_GUIDE.md`](docs/PORTING_GUIDE.md) and keeps private game
material outside version control.

## Build

Portable local build, already validated for this workspace:

```powershell
$env:PATH="$env:APPDATA\Python\Python314\Scripts;$env:PATH"
cmake --preset zig-debug
cmake --build --preset zig-debug
ctest --preset zig-debug
```

For the simplest Windows test, double-click `build\zig-debug\SG3000Recomp.exe`. The idle black frontend lets you configure controls and scaling, optionally select a BIOS, and then open or switch ROMs without restarting the process. The graphical flow automatically persists cartridge SRAM, provides hash-validated save-state management, BMP screenshots, WAV audio recording, editable per-game profiles, a hash/header game library with aliases, a live detailed-status window, and keeps a local ten-game recent list under `%LOCALAPPDATA%\SG3000Recomp`. `sgrecomp_host.exe` remains the equivalent technical/CLI name. See `docs/GUI.md` for controls and current limits.

The optional non-historical YM2612 extension uses [Nuked-OPN2](https://github.com/nukeykt/Nuked-OPN2)
by Alexey Khokholov (Nuke.YKT), distributed under LGPL-2.1-or-later. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for the pinned revision and license details.

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

Target defaults can come from a small TOML file:

```powershell
.\build\zig-debug\sgrecomp.exe game.sms --config config\example.toml --run-host --frames 1
```

The config reader validates `[target]`, `[recompiler]`, `[runtime]`, `[run]`, and
`[paths]` tables, rejecting unknown/duplicate keys and tables. Relative paths are based
on the TOML location; local profiles are matched by ROM hash after loading. CLI flags
after `--config` can still override those defaults.

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
.\build\zig-debug\sgrecomp.exe game.sms --run-host --frames 300 --input-script local-saves\title-input.csv
.\build\zig-debug\sgrecomp.exe game.sms --run-host --frames 300 --input-script local-saves\title-input.csv --dump-frame-log out\frames.csv
.\build\zig-debug\sgrecomp.exe game.sms --rebuild-header out\game-fixed.sms
.\build\zig-debug\sgrecomp.exe game.sms --generate-header out\game-header.sms --header-region sms-export --product-code 00000 --header-version 0
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

Mapper selection can be forced with `--mapper auto|plain|smapper|cmapper|kmapper|k8k` in both `sgrecomp` and `sgrecomp_host`. Use this when a ROM executes but stays blank; `auto` is the default, while explicit mapper modes are useful for local compatibility matrices. The base Sega mapper models its fixed 1 KiB window, mirrored paging registers, two cartridge-RAM banks, and cartridge-slot gating; EEPROM/write-protect variants remain profile-driven compatibility work.

The runtime normalizes common SMS I/O mirrors: `0x40-0x7f` route counter reads and PSG writes, and `0x80-0xbf` route VDP data/control access by even/odd port.

`--dump-frame`, `--dump-frame-bmp`, `--dump-audio`, `--dump-vgm`, `--dump-fm-log`, `--dump-io-log`, `--dump-memory-log`, `--dump-vdp-log`, `--dump-tilemap`, and `--dump-sprites` are local smoke artifacts for visual/audio inspection and reverse engineering. Keep them under ignored directories such as `out/` when testing private ROMs. BMP is convenient for opening a quick frame preview on Windows; PPM remains a simple raw technical frame dump. WAV is useful for listening to the current PSG/FM renderer; VGM is useful for inspecting captured PSG writes with timing; FM, I/O, memory, and VDP CSV logs capture runtime activity; tilemap and sprite CSVs expose VDP tables in a readable form. Use `--watch`, `--watch-vdp`, and `--io-port` to filter noisy logs.

Sprite enhancements keep the original overflow and collision status behavior. Accurate mode renders the original 8 sprites per scanline, `--reduce-flicker` raises that render limit to 16, and `--disable-sprite-limit` renders all visible sprites on the scanline. Extra enhanced sprites remain visually ordered but do not create hardware collision flags. `--enable-fm` enables the optional YM2413/OPLL path backed by the MIT-licensed emu2413 core, including ROM instruments, envelopes, LFO and rhythm mode.

Save states are local binary snapshots of mutable runtime state. They do not embed ROM or BIOS bytes, so load the same software first and keep `.sgstate` files under ignored local folders such as `local-saves/`. New save states include the ROM hash and console model; loading refuses mismatches unless `--force-state` is used for debugging.

The current save-state format is version 14. It persists exact emu2413 and Nuked-OPN2 core state,
Game Gear palette/stereo state, the Z80 MEMPTR/WZ and Q latch, the 240-line framebuffer,
environment identity, mapper-family lock, and VDP read buffer while remaining able to read
versions 1 through 13.

`--run-host` uses the headless host runtime path. It advances full frames, samples audio at the configured host rate, applies joypad state through the runtime API, and exposes the latest framebuffer. `HostRuntimeConfig` drives the VDP scanline/frame timing used by the host loop; pass `--video-standard ntsc|pal` or set `video_standard = "pal"` in a local profile to switch frame timing without storing ROM paths. Use `--audio-sample-rate hz` or `audio_sample_rate = 48000` in a local profile to change host audio output rate. The Windows backend adapts its queue after underruns while preserving queued audio across pause/resume; see [`docs/AUDIO.md`](docs/AUDIO.md) for mixer and fidelity limits.

`--input-script` applies deterministic player and Pause state at frame boundaries. See `docs/INPUT_SCRIPT.md` for the CSV format. The `--run-host` replay path can combine that input with frame/audio dumps, VGM/FM capture, filtered I/O and memory/VDP logs, and final VRAM/CRAM/tilemap/sprite dumps so an interactive transition can be diagnosed without switching to a different execution path.

`--dump-frame-log` writes deterministic per-frame diagnostics: framebuffer hash, executed PC range and instruction count, mapper/bank state, BIOS/cartridge/work-RAM visibility, cycle range, and non-zero audio sample count. When a BIOS is loaded, the headless summary reports the frame where its overlay is disabled and execution hands off to the cartridge.

`--rebuild-header` preserves an existing `TMR SEGA` header and updates its checksum. `--generate-header` writes a new header using the supplied region, five-digit hexadecimal product code, version, and ROM-size code. Both commands write a separate ROM image and reject sizes that the header cannot represent.

On Windows, `sgrecomp_host` opens the first native video/input/audio host window. Arrow keys map to the directional pad, `Z`/`X` map to the two action buttons, and `Enter` sends Pause/NMI. `Space` pauses emulation, `R` resets the runtime, `M` mutes audio, and `+`/`-` adjust volume. Audio uses the Win32 waveOut backend, can be disabled with `--mute`, and accepts `--audio-latency-ms` plus `--audio-sample-rate`. Local cartridge RAM can be loaded and saved with `--load-sram` and `--save-sram`; keep those files under ignored local folders. `--profile` loads hash-based local profiles for model, mapper, enhancements, video standard, audio latency, and audio sample rate without storing ROM paths, and `--print-hash` prints the local ROM hash plus detected cartridge header metadata needed for a profile. Matched profile values override the corresponding CLI defaults/options. The debug overlay shows FPS, frame count, PC, runtime mode, mapper/banks, VDP line/cycle timing plus status/IRQ state, pause state, volume, audio queue, underruns, dropped buffers, ROM hash, and matched profile; press `F1` to toggle it or start with `--no-overlay`.

The SMS memory-control port (`0x3E`) is modeled for the core boot/runtime cases: BIOS overlay enable, cartridge mapping enable, and work-RAM enable are tracked in the bus state, save states, I/O logs, and host overlay. Peripheral/region-specific bits remain compatibility work.

SG-3000 consoles use the TMS9918-compatible renderer path. Register mode bits select Graphics I, Graphics II, Text, or Multicolor; the runtime models their name/pattern/color table layouts, palette, 8x8 and four-pattern 16x16 sprites, and 2x zoom where the selected mode supports them.

Generated code exposes:

```cpp
extern "C" void sgrecomp_load_rom(sgrecomp::Bus& bus);
extern "C" void sgrecomp_run_instruction(sgrecomp::Z80State& cpu, sgrecomp::Bus& bus);
```

Generated files include per-basic-block helpers for statically reachable entry points and keep the PC dispatcher as a compatibility bridge for fallback and non-discovered paths.

## Layout

- `src/tools`: ROM analysis and code generation tools.
- `src/runtime`: console runtime linked by generated projects.
- `include/sgrecomp`: public runtime API.
- `docs`: architecture and implementation notes.
- `templates/project`: starter project for recompiled ROM ports.
- `config`: example target configuration.

## Continuation Checklist

See `docs/CHECKLIST.md` for the implementation roadmap and `docs/BUILDING.md` for toolchain details.
