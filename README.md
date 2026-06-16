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
- `sgrecomp` CLI that can disassemble or generate a C++ instruction dispatcher.
- Smoke test for a tiny Z80 program.

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
build\Debug\sgrecomp.exe game.sg --model sg3000 -o generated_sg3000.cpp
```

Runtime smoke execution is useful before full video/audio support exists:

```powershell
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --steps 50000
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --steps 200 --trace
```

If execution reaches an unsupported opcode, the tool prints the instruction, PC, registers, interrupt state, and cycle count.

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
