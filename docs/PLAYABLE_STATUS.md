# Playable Runtime Status

This document records the current minimum-playability gate so work can resume after an interruption without relying on local ROM names or paths.

## Verified Foundation

- The Win32 host loads a local ROM, opens a video window, polls keyboard input, and streams audio.
- Accurate and enhanced sprite-limit modes share the same runtime and can be selected by CLI/profile.
- SMS work RAM starts cleared while unmapped cartridge space remains open bus (`0xFF`).
- Mode 4 background patterns use the fixed `0x0000` pattern table; register 4 does not relocate them.
- Private local regression after these fixes produced multicolor frames for 12 of 13 test images and non-zero audio for 5 of 13 after 300 frames.
- Several private frames reached recognizable title/opening screens. No ROM, BIOS, filename, screenshot, hash, or local path is versioned.
- A deterministic private replay now advances one title through its menu, introduction, player selection, stage presentation, and into active gameplay.
- Starting from a gameplay save state, an 18,000-frame NTSC run completed without exceptions. It produced 6,359 distinct framebuffer hashes and non-zero audio in every frame; the final ten seconds remained visually active.
- Reloading the final gameplay state reproduced both the serialized state and framebuffer byte-for-byte. The selected title did not enable cartridge RAM, so an SRAM-specific gameplay round-trip is not applicable to this validation.
- Accurate and `reduce_flicker` runs from the same gameplay state and input were compared for 1,200 frames. Both remained stable, and the enhanced path produced a distinct framebuffer in every compared frame.
- Mapper-family locking fixed another private image whose valid Sega paging sequence was previously overridden by a later incidental write; `auto` now matches explicit SMapper output frame-for-frame and reaches a correct title screen with audio.

## Minimum Playability Gate

Do not call a title playable until all items below are demonstrated together:

- [x] ROM reaches a stable, recognizable multicolor screen.
- [x] Host window renders continuously without runtime exceptions.
- [x] Keyboard input is wired to player 1 and Pause/NMI at the runtime boundary.
- [x] A private title advances from title screen into gameplay using host input.
- [x] Gameplay remains stable for at least five minutes without stack, mapper, or VDP corruption.
- [x] PSG output is non-silent during gameplay for the selected title.
- [x] Accurate mode and at least one sprite enhancement are compared in the same gameplay scene.
- [x] Save-state round-trip is validated during gameplay, not only in synthetic tests; SRAM is title-dependent.

## Next Diagnostic Order

1. Repeat the deterministic gameplay gate on additional private images and permitted homebrew fixtures.
2. Use replay diagnostics to isolate titles that remain static, silent, or blocked before gameplay.
3. Fix mapper/VDP differences only from reproducible traces, then add synthetic regressions.
4. Validate SRAM round-trips on a title that actually enables cartridge RAM.
5. Broaden the compatibility matrix before treating the current single-title result as general compatibility.
