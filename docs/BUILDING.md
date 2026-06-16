# Building

This project can be built with Visual Studio C++ tools or with the portable Zig-based preset.

## Portable Local Toolchain

The `zig-debug` preset is useful when Visual Studio is installed as an IDE but the C++ build tools are not visible to `cmake`.

Install the local tools:

```powershell
python -m pip install --user cmake ninja ziglang
```

Then build:

```powershell
$env:PATH="$env:APPDATA\Python\Python314\Scripts;$env:PATH"
cmake --preset zig-debug
cmake --build --preset zig-debug
ctest --preset zig-debug
```

This uses:

- `cmake` from the Python user scripts directory.
- `ninja` from the Python user scripts directory.
- `python-zig c++` as the C++ compiler.

## Visual Studio

If Visual Studio has the Desktop development with C++ workload installed and registered, this preset should work:

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

If that fails with “generator could not find Visual Studio”, open the Visual Studio Installer and install or repair the C++ workload.

## Build Outputs

- `sgrecomp_runtime`: static runtime library.
- `sgrecomp`: ROM analysis and C++ generation CLI.
- `sgrecomp_tests`: runtime smoke tests.

## ROM Diagnostics

Before the project has a full host window/audio loop, use the CLI smoke runner:

```powershell
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --steps 50000
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --steps 200 --trace
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --steps 50000 --bios bios.sms
```

BIOS files are only for local testing. Keep them in `local-bios/`; that directory is ignored and generated C++ does not embed BIOS data.

The runner reports unsupported opcodes with disassembly and register state, which is the fastest way to decide the next CPU or hardware feature to implement for a real ROM.
