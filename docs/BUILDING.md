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

Após a build no Windows, `build\zig-debug\SG3000Recomp.exe` pode ser aberto diretamente. Sem argumentos, ele apresenta seletores gráficos para ROM e BIOS opcional. `sgrecomp_host.exe` é a cópia com nome técnico e mantém a interface avançada de linha de comando.

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

## Formatação e lint

O projeto fixa `clang-format` 19 para que a saída seja igual localmente e na CI:

```powershell
python -m pip install --user clang-format==19.1.7
$env:PATH="$env:APPDATA\Python\Python314\Scripts;$env:PATH"
cmake --preset lint
cmake --build --preset lint --target format-check
cmake --build --preset lint
ctest --preset lint
```

Use o alvo `format` para aplicar a formatação automaticamente. O preset `lint` também
compila com warnings tratados como erros.

## Build Outputs

- `sgrecomp_runtime`: static runtime library.
- `sgrecomp`: ROM analysis and C++ generation CLI.
- `SG3000Recomp.exe`: executável amigável para duplo clique, seleção de ROM/BIOS e jogo.
- `sgrecomp_host`: nome técnico equivalente do host Win32 para scripts e opções avançadas.
- `sgrecomp_tests`: runtime smoke tests.

## Pacote portátil Windows

Depois de compilar o preset, gere o ZIP redistribuível com:

```powershell
$env:PATH="$env:APPDATA\Python\Python314\Scripts;$env:PATH"
Set-Location build\zig-debug
cpack -G ZIP
```

O arquivo `SG3000Recomp-0.1.0-windows-x64-portable.zip` contém o executável, a DLL substituível
Nuked-OPN2, o guia, avisos de terceiros e as licenças LGPL/MIT dos núcleos de áudio. ROMs, BIOS,
saves, perfis locais e artefatos de diagnóstico não fazem parte do pacote. O CPack também gera
um arquivo `.sha256`.

## ROM Diagnostics

Before the project has a full host window/audio loop, use the CLI smoke runner:

```powershell
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --steps 50000
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --steps 200 --trace
.\build\zig-debug\sgrecomp.exe game.sms --run-smoke --steps 50000 --bios bios.sms
```

BIOS files are only for local testing. Keep them in `local-bios/`; that directory is ignored and generated C++ does not embed BIOS data.

The runner reports unsupported opcodes with disassembly and register state, which is the fastest way to decide the next CPU or hardware feature to implement for a real ROM.
