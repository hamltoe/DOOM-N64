# DOOM-N64
DOOM-N64 is a Windows-friendly porting project for running classic DOOM on the Nintendo 64. Play your favorite mods using the WAD browser to load IWAD and PWAD files at runtime. Try the original shareware demo on the [releases](https://github.com/hamltoe/DOOM-N64/releases) page.

# What This Project Does

- Builds a Nintendo 64 DOOM ROM on Windows using WSL.
- Lets you choose IWAD and PWAD files in an in-game WAD browser.
- Copies all .wad/.WAD files from WADs/ into ROM filesystem during build.


## You Need (One-Time)

1. Windows 10 or 11.
2. WSL2 with Ubuntu (or similar distro).
3. libdragon toolchain installed at /opt/libdragon.

Toolchain install docs:
https://github.com/DragonMinded/libdragon/wiki/Installing-libdragon

## Quick Start (5 Minutes)

### 1) Get project

If cloning new:

```bash
git clone --recurse-submodules <your-repo-url>
cd DOOM-N64
```

If already cloned:

```bash
git submodule update --init --recursive
```

### 2) Add your WAD files

Place your legally owned files into the `WADs/` folder.

Minimum setup:
- At least one IWAD (example: DOOM.WAD, DOOM1.WAD)

Custom mod setup:
- One IWAD + one or more PWAD files

Example:

```text
WADs/DOOM.WAD
WADs/plutonia.wad
```

### 3) Build ROM

Fastest in VS Code:
1. Run Task
2. Select build-doom

Debug build with logging:
1. Run Task
2. Select build-doom-debug

Post-build verification:
1. Run Task
2. Select verify-wsl-n64-env

Important:
- Run build and verify tasks sequentially (not concurrently).
- All project tasks are pinned to `N64_INST=/opt/libdragon` and `N64_GCCPREFIX=/opt/libdragon`.

Command-line build in WSL (same result):

```bash
export N64_INST=/opt/libdragon
export N64_GCCPREFIX=/opt/libdragon
make -j"$(nproc)"
echo rom_bytes="$(stat -c%s Doom-N64.z64)"
```

### 4) Find output

ROM output:

```text
Doom-N64.z64
```

### 5) Launch and play

At startup, WAD browser appears:
- Select IWAD to play base game.
- Select PWAD to play mod. Browser asks for base IWAD.

Load order for mods is always:
1. Base IWAD
2. Selected PWAD

## Simple PWAD Workflow (Every Time)

1. Drop new PWAD into WADs/.
2. Keep one compatible IWAD in WADs/.
3. Build again.
4. Boot Doom-N64.z64.
5. Pick PWAD, then pick base IWAD when prompted.

## WAD Browser Controls

- Up/Down (D-Pad or Analog): move selection
- Left/Right (D-Pad) or C-Up/C-Down: page up/down
- A or Start: select
- B: quick launch default IWAD (when available)

## Common Problems (Fast Fixes)

### Error: Missing WADs

Fix:
- Put at least one .wad/.WAD file in WADs/.
- Rebuild.

### Error: /opt/libdragon/bin/mkdfs: not found

Fix (one-time):

```bash
make -C libdragon tools -j"$(nproc)"
make -C libdragon tools-install N64_INST="$(pwd)/libdragon" N64_GCCPREFIX=/opt/libdragon
```

Then rerun `verify-wsl-n64-env`.

### Error: Permission denied on Doom-N64.z64

Fix:
- Close emulator or flash-cart tool that is using ROM file.
- Build again.

### PWAD returns to browser

Fix:
- Make sure at least one valid IWAD exists in WADs/.
- Select PWAD again and choose base IWAD.

## Legal

- Bring your legally owned IWAD/PWAD files.
- This Repository does not redistribute commercial IWAD content.

## Credits

See Credits.md for acknowledgments and licensing.