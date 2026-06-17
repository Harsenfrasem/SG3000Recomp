# ROM Testing Notes

Commercial ROM images and other copyrighted dumps must not be committed.

Use a local ignored directory for private smoke tests:

```powershell
mkdir local-roms
mkdir local-bios
mkdir local-fm-roms
```

`local-roms/`, `local-fm-roms/`, and `local-bios/` are ignored by git. Put only your own legally obtained test images there.

Useful commands:

```powershell
$env:PATH="$env:APPDATA\Python\Python314\Scripts;$env:PATH"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --bios ".\local-bios\your-test-bios.sms"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --bios ".\local-bios\your-test-bios.sms" --dump-frame ".\out\frame.ppm"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --bios ".\local-bios\your-test-bios.sms" --dump-vram ".\out\vram.bin" --dump-cram ".\out\cram.bin"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --dump-io-log ".\out\io.csv" --dump-tilemap ".\out\tilemap.csv" --dump-sprites ".\out\sprites.csv"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --dump-memory-log ".\out\memory.csv" --watch 0xc000-0xdfff --dump-vdp-log ".\out\vdp.csv" --watch-vdp 0x0000-0x03ff --io-port 0xbe
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --bios ".\local-bios\your-test-bios.sms" --load-sram ".\local-saves\your-test-save.sav" --save-sram ".\local-saves\your-test-save.sav"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --bios ".\local-bios\your-test-bios.sms" --dump-coverage ".\out\coverage.csv"
.\build\zig-debug\sgrecomp.exe ".\local-fm-roms\your-fm-test-rom.sms" --run-smoke --enable-fm --steps 300000 --bios ".\local-bios\your-test-bios.sms" --dump-audio ".\out\fm.wav" --dump-fm-log ".\out\fm-writes.csv"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 200 --trace
.\build\zig-debug\sgrecomp_host.exe ".\local-roms\your-test-rom.sms" --print-hash
.\build\zig-debug\sgrecomp_host.exe ".\local-roms\your-test-rom.sms" --bios ".\local-bios\your-test-bios.sms" --profile ".\out\profiles\your-local-profile.txt"
```

`--bios` is intended for local smoke testing and disassembly only. Generated C++ still embeds only the ROM image, never the BIOS file.
ROM files with a generic 512-byte copier header are normalized before loading.
The smoke runner also reports visited PCs, lit framebuffer pixels, the current PSG/FM sample, and can dump the current framebuffer, VRAM, CRAM, SRAM, PC coverage, PSG VGM writes, FM CSV writes, generic I/O access logs, RAM/cart/mapper writes, VDP writes, tilemap entries, and sprite table entries.

FM support status: the runtime now exposes the expected local FM control path and can log writes to `$F0/$F1/$F2`. The current synthesis is still an approximation for plumbing and diagnostics, not a faithful YM2413/OPLL implementation.

For enhanced-port research, keep per-game notes in ignored local folders and identify games by `fnv1a64` hash. The public repository should contain only generic tools, synthetic tests, and neutral profile examples. See `docs/ENHANCED_PORT_ROADMAP.md` for the staged workflow.

Current result from private local smoke testing:

- A small private local set is available in ignored folders for smoke testing.
- Multiple SMS-sized ROMs can load and execute hundreds of thousands of Z80 steps.
- A private BIOS plus ROM smoke test reaches the configured step limit without hitting an unsupported opcode.
- They still do not render playable output. The next blockers are richer VDP rendering, exact timing, mapper edge cases, host input, and audio backend plumbing.
- Recent private tests helped prioritize V counter reads, mapper RAM preservation, RAM mirroring, cartridge SRAM, local BIOS overlay, IX/IY handling, indexed CB operations, `IXH/IXL/IYH/IYL`, `daa`, delayed `ei`, NMI service, and additional ED instructions.
