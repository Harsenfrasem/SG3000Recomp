# Architecture

SG3000Recomp is an ahead-of-time recompilation toolkit for Z80-based 8-bit systems.

## Target Machines

SMS and SG-3000 share a Z80 CPU family and a similar I/O style, but they differ in cartridge expectations and video behavior. The runtime therefore keeps the model explicit instead of hiding it behind one generic console.

Game Gear is a planned future target because it can reuse much of the SMS core, including Z80 execution, cartridge metadata, PSG behavior, and parts of the VDP pipeline. It still needs its own explicit platform model for 160x144 output, 12-bit palette handling, Start/input differences, and compatibility heuristics. Until those pieces are validated, Game Gear headers are detected for diagnostics but SMS/SG-3000 remain the active compatibility targets.

## Pipeline

```text
ROM image
  -> cartridge loader and mapper model
  -> Z80 decoder
  -> basic block discovery
  -> C++ lifter
  -> generated ROM module
  -> native executable linked with runtime
```

## Runtime Boundaries

- `Z80State` stores registers, interrupt flip-flops, halt state, and cycle count.
- `Bus` owns the visible 64 KiB Z80 memory map and routes I/O ports.
- `Vdp` models VRAM, CRAM, registers, status, and framebuffer.
- `Psg` models SN76489-compatible writes and audio generation.
- `Joypad` exposes active-low controller reads.

## Compatibility Strategy

The project uses a hybrid path while the lifter matures:

- Lift decoded instructions to direct C++ when the generated code path is known.
- Call the fallback interpreter for unsupported opcodes.
- Discover reachable basic blocks from the reset PC and report static successors, fallback opcodes, and indirect exits.
- Add test ROMs and opcode-level tests as each family is promoted to generated code.

This keeps ROMs debuggable from the first milestone while allowing the static code path to grow safely.

## Enhancement Strategy

The runtime should keep accurate behavior as the default. Enhancements are optional runtime features layered on top of the compatible path, not replacements for it.

Planned execution modes:

- `accurate`: interpreter/recompiler runtime with original limits and timing goals.
- `hybrid`: generated C++ where available, interpreter fallback elsewhere.
- `enhanced`: accurate or hybrid execution plus explicit user-enabled improvements.

The first enhancement target is sprite flicker reduction through `disable_sprite_limit` and `reduce_flicker`. Accurate mode keeps the original 8 rendered sprites per scanline. `reduce_flicker` expands this conservatively to 16 while preserving the overflow status bit, and `disable_sprite_limit` renders all visible sprites on the scanline. Later enhancements can add host-facing features, FM audio options, and per-game profiles. Profiles must identify software by hashes or public metadata and must not require committing ROM, BIOS, save, or local path data.

## Static Analysis Report

The CLI can write a local analysis file with `--dump-analysis`. The report is intentionally text-first so it can be inspected quickly during bring-up. It lists the scan limit, default entry points, reachable blocks, instructions per block, static successors, direct-emitted instructions, fallback instructions, indirect exits such as register jumps, heuristic little-endian pointer tables, and static hardware accesses such as immediate I/O ports or direct mapper writes. Default entry points include reset, IM1 IRQ at `0x0038`, and NMI/Pause at `0x0066`.

The report does not include ROM or BIOS assets and should normally be written under an ignored local output directory while testing private software.

## Near-Term Milestones

1. Complete documented Z80 base opcode coverage.
2. Add CB-prefixed rotate/shift/bit instructions.
3. Add ED block I/O, interrupt mode, and 16-bit arithmetic details.
4. Add IX/IY prefixed addressing.
5. Complete SMS VDP modes on top of configurable scanline/frame timing, VBlank, and pause/NMI behavior.
6. Implement common SMapper variants and SG-3000 cartridge layouts.
7. Produce per-ROM generated project templates with assets and host loop.
