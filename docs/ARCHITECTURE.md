# Architecture

SG3000Recomp is an ahead-of-time recompilation toolkit for Z80-based Sega 8-bit systems.

## Target Machines

Master System and SG-3000 share a Z80 CPU family and a similar I/O style, but they differ in cartridge expectations and video behavior. The runtime therefore keeps the model explicit instead of hiding it behind one generic console.

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
- Add test ROMs and opcode-level tests as each family is promoted to generated code.

This keeps ROMs debuggable from the first milestone while allowing the static code path to grow safely.

## Near-Term Milestones

1. Complete documented Z80 base opcode coverage.
2. Add CB-prefixed rotate/shift/bit instructions.
3. Add ED block I/O, interrupt mode, and 16-bit arithmetic details.
4. Add IX/IY prefixed addressing.
5. Implement SMS VDP modes, line timing, VBlank, and pause/NMI behavior.
6. Implement common Sega mapper variants and SG-3000 cartridge layouts.
7. Produce per-ROM generated project templates with assets and host loop.
