# Playable Runtime Status

This document records the current minimum-playability gate so work can resume after an interruption without relying on local ROM names or paths.

## Verified Foundation

- The Win32 host loads a local ROM, opens a video window, polls keyboard input, and streams audio.
- Accurate and enhanced sprite-limit modes share the same runtime and can be selected by CLI/profile.
- SMS work RAM starts cleared while unmapped cartridge space remains open bus (`0xFF`).
- Mode 4 background patterns use the fixed `0x0000` pattern table; register 4 does not relocate them.
- Private local regression after these fixes produced multicolor frames for 12 of 13 test images and non-zero audio for 5 of 13 after 300 frames.
- Several private frames reached recognizable title/opening screens. No ROM, BIOS, filename, screenshot, hash, or local path is versioned.

## Minimum Playability Gate

Do not call a title playable until all items below are demonstrated together:

- [x] ROM reaches a stable, recognizable multicolor screen.
- [x] Host window renders continuously without runtime exceptions.
- [x] Keyboard input is wired to player 1 and Pause/NMI at the runtime boundary.
- [ ] A private title advances from title screen into gameplay using host input.
- [ ] Gameplay remains stable for at least five minutes without stack, mapper, or VDP corruption.
- [ ] PSG output is non-silent during gameplay for the selected title.
- [ ] Accurate mode and at least one sprite enhancement are compared in the same gameplay scene.
- [ ] Save RAM/state round-trip is validated during gameplay, not only in synthetic tests.

## Next Diagnostic Order

1. Add deterministic headless input scripting so title-screen progression is testable without manual timing.
2. Capture per-frame framebuffer hashes, PC ranges, mapper state, and non-zero audio counts.
3. Fix remaining mapper/VDP differences using the first title that reaches gameplay.
4. Validate accurate versus enhanced sprite rendering in a scene with visible flicker.
5. Only then broaden the compatibility matrix and GUI workflow.
