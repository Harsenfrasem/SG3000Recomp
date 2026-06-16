# Architecture

SG3000Recomp is an ahead-of-time recompilation toolkit for Z80-based 8-bit systems.

## Target Machines

SMS and SG-3000 share a Z80 CPU family and a similar I/O style, but they differ in cartridge expectations and video behavior. The runtime therefore keeps the model explicit instead of hiding it behind one generic console.

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

## Static Analysis Report

The CLI can write a local analysis file with `--dump-analysis`. The report is intentionally text-first so it can be inspected quickly during bring-up. It lists the scan limit, reachable blocks, instructions per block, static successors, direct-emitted instructions, fallback instructions, and indirect exits such as register jumps.

The report does not include ROM or BIOS assets and should normally be written under an ignored local output directory while testing private software.

## Near-Term Milestones

1. Complete documented Z80 base opcode coverage.
2. Add CB-prefixed rotate/shift/bit instructions.
3. Add ED block I/O, interrupt mode, and 16-bit arithmetic details.
4. Add IX/IY prefixed addressing.
5. Implement SMS VDP modes, line timing, VBlank, and pause/NMI behavior.
6. Implement common SMapper variants and SG-3000 cartridge layouts.
7. Produce per-ROM generated project templates with assets and host loop.
