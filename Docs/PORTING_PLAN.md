# DOOM N64 Porting Plan

## 1. Goal and Scope

Primary goal for first playable milestone:
- Boot on original N64 hardware.
- Reach DOOM main menu.
- Load and render first level (E1M1) visually.
- Support controller input for movement and menu navigation.

Out of scope for first playable milestone:
- Sound effects and music.
- Netplay or serial multiplayer.
- Save/load persistence.
- Tiny3D renderer rewrite.

## 2. Platform Constraints (N64)

Hardware constraints that shape design:
- CPU: MIPS R4300i (big-endian, no branch prediction, small cache).
- RAM: 4 MB base, 8 MB with Expansion Pak.
- RDRAM latency/bandwidth bottleneck.
- GPU path: RSP + RDP pipeline, DMA-heavy workflow.

Project decisions:
- Require Expansion Pak (8 MB) for first playable.
- Keep classic DOOM software renderer first.
- Present software frame through libdragon RDP path.
- Defer Tiny3D usage until after baseline is stable.

## 3. Data, IWAD, and PWAD Strategy

The engine code is GPL, but IWAD data is not redistributable with project source.

Plan:
- User provides legally owned WAD files under `WADs/`.
- Build system stages all `.wad/.WAD` files from `WADs/` into `filesystem/` before `mkdfs`.
- Require at least one valid IWAD in staged set (PWAD-only set is not bootable).
- Keep guard rails in build scripts:
  - Fail with clear message if no WAD files are present.
  - Keep expected source location (`WADs/`) explicit.
- Add `.gitignore` entry to avoid committing proprietary WAD data.

Technical loading model:
- Store staged WADs in DragonFS image and scan them via `rom:/` paths.
- Browser classifies each WAD as `IWAD`, `PWAD`, `BAD`, or `ERR` from header and directory checks.
- Selecting an IWAD loads it directly as base content.
- Selecting a PWAD opens a base-IWAD picker; runtime loads base IWAD first, then PWAD.
- Keep lump cache behavior close to original DOOM for compatibility.

## 4. Key Technical Hurdles

1. Endianness mismatch
- WAD is little-endian, N64 is big-endian.
- All WAD headers/lumps must go through `SHORT()`/`LONG()` correctly.
- `m_swap` behavior must be verified for N64 compile path.

2. Memory pressure
- Original DOOM zone heap targets sizes that are tight on 4 MB.
- Expansion Pak required for first milestone.
- Need careful heap sizing for `Z_Init`, screen buffers, lump caching.

3. Linux platform dependencies
- Current code assumes X11, POSIX timing, Linux headers.
- Replace Linux-only `i_*` subsystems with N64 implementations.

4. Video format mismatch
- DOOM renders paletted 8bpp 320x200.
- N64 display typically uses RGBA16 framebuffers.
- Need CI8 surface + TLUT upload + RDP blit path.

5. Controller to key event translation
- DOOM input model is keyboard-centric.
- Need reliable controller-to-event mapping with edge transitions.

6. Timing model
- DOOM logic depends on stable 35 Hz ticks.
- N64 timing implementation must preserve deterministic tick behavior.

7. Stack and alignment risks on MIPS
- Recursive render paths may stress stack.
- Struct/alignment assumptions from PC build may fail on strict alignment.

## 5. Reference Material

Primary libdragon examples:
- `libdragon/examples/rdpqdemo/` for CI formats and TLUT flow.
- `libdragon/examples/dfsdemo/` for DragonFS file loading.
- `libdragon/examples/joypadtest/` for controller polling.
- `libdragon/examples/vtest/` and `customfont/` for frame/display basics.

Tiny3D examples for later phases:
- `tiny3d/examples/01_model/` onward for 3D pipeline patterns.
- Tiny3D considered optimization/renderer evolution after baseline port.

## 6. Multi-Phase Execution Plan

### Phase 0: Build Baseline and N64 Bootstrap

Goals:
- Create root N64 build target using libdragon `n64.mk`.
- Boot ROM on emulator/hardware with minimal loop.
- Initialize display, console/debug output, DragonFS.

Deliverables:
- Build artifact (`doom.z64`).
- Expansion Pak check and boot-time memory log.
- Basic on-screen debug text.

Exit criteria:
- ROM boots consistently.
- DragonFS initialization succeeds.
- Team can run repeatable build-test cycle.

### Phase 1: Source Integration and Platform Abstraction

Goals:
- Integrate `linuxdoom-1.10` core into N64 build.
- Fence Linux/X11 code with `#ifdef N64` paths.
- Provide N64 stubs/implementations for required `I_*` APIs.

Target files:
- `linuxdoom-1.10/i_main.c`
- `linuxdoom-1.10/i_video.c`
- `linuxdoom-1.10/i_system.c`
- `linuxdoom-1.10/i_sound.c` (stubs)
- `linuxdoom-1.10/i_net.c` (single-player stubs)
- `linuxdoom-1.10/d_main.c`
- `linuxdoom-1.10/m_swap.h`

Exit criteria:
- Full project links on N64 toolchain.
- `D_DoomMain()` enters initialization path without Linux/X11 linkage.

### Phase 2: WAD Loading (IWAD + PWAD) and Endian Validation

Goals:
- Load selected IWAD or selected PWAD + base IWAD from DragonFS.
- Validate WAD header parsing and lump directory integrity.
- Confirm endian conversion correctness across all critical reads.

Tasks:
- Replace POSIX file calls in WAD loader path.
- Add diagnostics for selected WAD/base IWAD, lump count, and selected known lump names.
- Verify no alignment faults while reading lump structures.

Exit criteria:
- `W_InitMultipleFiles` succeeds for direct IWAD and IWAD+PWAD launch paths.
- `numlumps` value is sane and stable for selected content.
- No fatal read/endian errors during startup.

### Phase 3: Video Output Path (Software Frame to N64 Display)

Goals:
- Keep original software renderer output buffer behavior.
- Connect `screens[0]` to CI8 surface buffer.
- Upload palette as TLUT and blit to framebuffer each frame.

Tasks:
- Implement `I_SetPalette` conversion to RGBA16 TLUT.
- Implement `I_FinishUpdate` with RDP copy/blit flow.
- Choose output mode:
  - Letterbox 320x200 in 320x240, or
  - Vertical scale with acceptable artifact level.

Exit criteria:
- Title screen renders correctly.
- Menu and HUD colors look correct.
- No tearing/corruption in steady update loop.

### Phase 4: Input and Menu Navigation

Goals:
- Poll N64 controller every tic.
- Translate controller events into DOOM key events.
- Navigate full menu flow and start E1M1.

Suggested default mapping:
- D-pad: movement/menu arrows
- A: use/confirm
- B: fire/back
- Start: menu
- L/R: strafe or turn modifiers
- Z: alternate fire/strafe modifier
- C buttons: quick look/weapon cycling (tunable)

Exit criteria:
- Stable menu navigation.
- Start new game and enter first map.

### Phase 5: First Level Visual Playable

Goals:
- Reach in-level gameplay loop (visual only).
- Keep deterministic game ticks at 35 Hz.
- Ensure player can move and world updates correctly.

Tasks:
- Finalize `I_GetTime` tick source.
- Validate player physics and collision in E1M1.
- Profile frame costs and detect biggest software render hotspots.

Exit criteria:
- E1M1 playable with visible world, enemies, and HUD.
- No startup crashes or fatal runtime assertions in normal play.

### Phase 6: Stabilization and Performance Pass

Goals:
- Harden assertions/error paths.
- Improve frame stability and memory safety.
- Prepare branch for sound integration.

Tasks:
- Add assert gates around platform boundaries.
- Tune memory allocation and lump caching behavior.
- Investigate DMA-friendly read patterns for large data pulls.

Exit criteria:
- Stable 10+ minute play sessions in first map without crash.
- Known crash vectors documented and reproducible.

### Phase 7: Sound Integration

Goals:
- Add SFX and music pipeline on libdragon audio stack.
- Use real background music playback through libdragon XM64/YM64 players.

Notes:
- SFX backend now active in `i_sound_n64.c` via mixer waveform callbacks.
- Music backend now active in `i_sound_n64.c` and resolves WAD music lumps
  (for example `D_E1M1`) to DragonFS assets.
- Place source music modules in `assets/music/` as `.xm`/`.ym` files.
- Build converts them to `filesystem/music/*.xm64` / `filesystem/music/*.ym64`
  via `audioconv64` rules in root `Makefile`.
- MUS fallback now uses example-driven interpretation with channel instrument,
  volume/pan, pitch wheel, note mapping, and sampled instrument playback when
  `MIDI_Instruments` is present
- Instrument bank search order at runtime: `rom:/MUS/MIDI_Instruments`, then
  `rom:/MUS/MIDI_Instruments`, then legacy fallback `rom:/MIDI_Instruments.bin`
- Build will stage `MUS/MIDI_Instruments` into
  `filesystem/MUS/MIDI_Instruments` when available.
- Runtime file lookup order per lump: `rom:/music/<lump>.xm64`,
  `rom:/music/<lump>.ym64`, then same names under `rom:/`.
- Recommended naming: lowercase 8-char lump names, eg `d_e1m1.xm`.

## 7. Last-Issue Review for Early Phases (0-3)

Critical issues to watch now:

1. WAD legal/distribution workflow (IWAD + PWAD)
- Risk: accidental repo commit of proprietary WAD.
- Mitigation: `.gitignore`, build-time existence checks, docs warning.

2. Endian bugs that look like random memory corruption
- Risk: malformed lump offsets/sizes from missed swap calls.
- Mitigation: assert header values and bounds-check every lump access early.

3. Zone heap overcommit at startup
- Risk: boot succeeds then dies during level setup.
- Mitigation: log all major allocations and leave headroom for transient buffers.

4. CI8 + TLUT rendering path edge cases
- Risk: wrong palette upload timing causing flicker/wrong colors.
- Mitigation: update TLUT only on palette change and verify first 16 palette entries with debug checks.

5. Tick pacing jitter
- Risk: unstable frame/tic relationship causing input lag or overrun.
- Mitigation: lock logic to 35 Hz accumulator and decouple draw frequency.

6. Controller event chattering
- Risk: repeated key down events and menu overshoot.
- Mitigation: edge-triggered transitions with held-state tracking.

7. Stack pressure in recursive render paths
- Risk: rare hard crash on specific map traversal.
- Mitigation: increase stack early, add watermark/debug probe.

8. Linux include leakage into N64 build
- Risk: compile churn and hidden platform dependencies.
- Mitigation: central `#ifdef N64` fences and strict build warnings.

9. Legacy C dialect friction (K&R style)
- Risk: implicit int/prototype assumptions break with modern compiler defaults.
- Mitigation: compile in C90-compatible mode first, then tighten warnings after baseline boots.

## 8. Validation Checklist by Milestone

Phase 0 check:
- Boots on emulator.
- Memory size printed.
- DragonFS init pass.

Phase 1 check:
- N64 build links clean.
- No X11/Linux symbol references.

Phase 2 check:
- Selected WAD opens from `rom:/` browser path.
- PWAD launch loads base IWAD first, then selected PWAD.
- Lump count sane and stable.
- No endian assertion failures.

Phase 3 check:
- Title screen visible.
- Palette transitions correct.
- Frame updates stable.

Phase 4 check:
- Menu navigation reliable.
- New game launch works.

Phase 5 check:
- E1M1 playable visually.
- Player movement and core loop stable.

## 9. Post-Playable Direction

After first playable is stable:
- Sound integration.
- Save/persistence.
- Performance tuning of software renderer.
- Evaluate optional Tiny3D-assisted rendering experiments without breaking baseline path.
