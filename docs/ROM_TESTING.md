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
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --mapper auto --dump-frame-bmp ".\out\frame-auto.bmp"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --mapper plain --dump-frame-bmp ".\out\frame-plain.bmp"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --mapper cmapper --dump-frame-bmp ".\out\frame-cmapper.bmp"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --dump-io-log ".\out\io.csv" --dump-tilemap ".\out\tilemap.csv" --dump-sprites ".\out\sprites.csv"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --dump-memory-log ".\out\memory.csv" --watch 0xc000-0xdfff --dump-vdp-log ".\out\vdp.csv" --watch-vdp 0x0000-0x03ff --io-port 0xbe
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --bios ".\local-bios\your-test-bios.sms" --load-sram ".\local-saves\your-test-save.sav" --save-sram ".\local-saves\your-test-save.sav"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --save-state ".\local-saves\your-test-state.sgstate"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --load-state ".\local-saves\your-test-state.sgstate" --steps 300000
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000 --bios ".\local-bios\your-test-bios.sms" --dump-coverage ".\out\coverage.csv"
.\build\zig-debug\sgrecomp.exe ".\local-fm-roms\your-fm-test-rom.sms" --run-smoke --enable-fm --steps 300000 --bios ".\local-bios\your-test-bios.sms" --dump-audio ".\out\fm.wav" --dump-fm-log ".\out\fm-writes.csv"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --dump-analysis ".\out\analysis.txt"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 200 --trace
.\build\zig-debug\sgrecomp_host.exe ".\local-roms\your-test-rom.sms" --print-hash
.\build\zig-debug\sgrecomp_host.exe ".\local-roms\your-test-rom.sms" --bios ".\local-bios\your-test-bios.sms" --profile ".\out\profiles\your-local-profile.txt"
```

`--bios` is intended for local smoke testing and disassembly only. Generated C++ still embeds only the ROM image, never the BIOS file.
ROM files with a generic 512-byte copier header are normalized before loading.
`--dump-analysis` reports the detected `TMR SEGA` header offset, region/size byte, stored checksum, and checksum over the declared ROM size excluding the 16-byte header block. Use it before mapper/BIOS debugging to catch wrong-platform images or surprising size metadata.
If the declared size exceeds the loaded image, analysis reports that the image does not fit and does not claim checksum validity. Use `--rebuild-header output.sms` to preserve existing metadata while repairing the checksum, or `--generate-header output.sms --header-region sms-export --product-code 00000 --header-version 0` to create a new header. These operations never overwrite the input image.
If the detected platform is Game Gear, treat the result as diagnostic only for now. The project can support Game Gear in the future, but the current faithful target is still SMS/SG-3000.
`--save-state` writes only mutable emulator state, not ROM or BIOS data. Always load the same ROM/BIOS combination before `--load-state`. New state files validate ROM hash and console model; use `--force-state` only when intentionally debugging mismatches.
The smoke runner also reports visited PCs, lit framebuffer pixels, the current PSG/FM sample, and can dump the current framebuffer, VRAM, CRAM, SRAM, PC coverage, PSG VGM writes, FM CSV writes, generic I/O access logs, RAM/cart/mapper writes, VDP writes, tilemap entries, and sprite table entries.

Use `--mapper auto|plain|smapper|cmapper|kmapper|k8k` when a ROM reaches code but stays blank. `auto` keeps small linear ROMs as `plain` and uses SMapper for larger banked SMS ROMs. The explicit modes are useful for local reverse-engineering matrices. Keep generated screenshots, logs, and per-ROM notes under ignored local folders such as `out/`.

In `auto`, the first recognized mapper-register family locks the selection for the session and save state. This prevents later writes to unrelated ROM-space addresses from changing an already demonstrated Sega mapper. Use an explicit profile only when the boot trace provides no conclusive mapper writes or hardware is genuinely ambiguous.

Local hash profiles accept the same mapper names through `mapper = "..."`. A matched profile overrides the CLI mapper, which makes the per-title choice deterministic across launches without storing the ROM path.

FM support status: the runtime now exposes the expected local FM control path and can log writes to `$F0/$F1/$F2`. The current synthesis is still an approximation for plumbing and diagnostics, not a faithful YM2413/OPLL implementation.

For enhanced-port research, keep per-game notes in ignored local folders and identify games by `fnv1a64` hash. The public repository should contain only generic tools, synthetic tests, and neutral profile examples. See `docs/ENHANCED_PORT_ROADMAP.md` for the staged workflow.

Current result from private local smoke testing:

- A small private local set is available in ignored folders for smoke testing.
- Multiple SMS-sized ROMs can load and execute hundreds of thousands of Z80 steps.
- A private BIOS plus ROM smoke test reaches the configured step limit without hitting an unsupported opcode.
- One private image now reaches gameplay through deterministic input and completed a five-minute NTSC regression with continuous audio, save-state round-trip, and accurate/enhanced comparison. This remains a single-title result rather than a general compatibility claim.
- The next compatibility blockers are additional-title replay coverage, external Z80/timing conformance, remaining mapper variants, VDP edge cases, and faithful FM audio.
- Recent private tests helped prioritize V counter reads, mapper RAM preservation, RAM mirroring, cartridge SRAM, local BIOS overlay, IX/IY handling, indexed CB operations, `IXH/IXL/IYH/IYL`, `daa`, delayed `ei`, NMI service, and additional ED instructions.
