# ROM Testing Notes

Commercial ROM images and other copyrighted dumps must not be committed.

Use a local ignored directory for private smoke tests:

```powershell
mkdir local-roms
```

`local-roms/` is ignored by git. Put only your own legally obtained test images there.

Useful commands:

```powershell
$env:PATH="$env:APPDATA\Python\Python314\Scripts;$env:PATH"
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 300000
.\build\zig-debug\sgrecomp.exe ".\local-roms\your-test-rom.sms" --run-smoke --steps 200 --trace
```

Current result from private local smoke testing:

- Multiple Master System ROMs can load and execute hundreds of thousands of Z80 steps.
- They still do not render playable output. The next blockers are hardware behavior: VDP timing/rendering, interrupt accuracy, mapper details, input, and audio.
- Recent private tests helped prioritize V counter reads, mapper RAM preservation, IX/IY handling, indexed CB operations, and additional ED instructions.
