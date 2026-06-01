# DOOM-N64
DOOM-N64 is a Windows-friendly porting project for running classic DOOM on the Nintendo 64. Play your favorite mods using the WAD browser to load IWAD and PWAD files at runtime.

This repository contains:
- Original DOOM source base under linuxdoom-1.10
- N64 platform layer and build integration
- Bundled libdragon and tiny3d sources
- Asset and filesystem pipeline for ROM builds

## Status

Current build produces:
- doom.z64
- build/doom.elf
- build/doom.dfs

WAD handling (IWAD + PWAD):
- Place your legally owned .WAD files in WADs/
- Build syncs all .wad/.WAD files from WADs/ into filesystem/
- Keep at least one IWAD (for example DOOM.WAD, DOOM2.WAD, or DOOM1.WAD)
- PWAD files are supported; selecting a PWAD prompts for a base IWAD
- Runtime load order for mods is: base IWAD first, selected PWAD second

## Windows Build (WSL)

These steps are for Windows + WSL2.

### 1) Prerequisites

Install:
- Windows 10/11 with WSL2
- Ubuntu (or similar) distro in WSL
- Basic build tools in WSL (make, gcc, g++, python3)
- libdragon toolchain installed at /opt/libdragon

If you need toolchain install docs, see:
- https://github.com/DragonMinded/libdragon/wiki/Installing-libdragon

### 2) Clone with submodules

```bash
git clone --recurse-submodules <your-repo-url>
cd DOOM-N64
```

If already cloned without submodules:

```bash
git submodule update --init --recursive
```

### 3) Put WAD files in place

Copy your legally owned WAD files to the WADs folder. For PWAD mods, include both the mod PWAD and at least one compatible base IWAD:

```text
WADs/DOOM2.WAD
WADs/MYMOD.WAD
```

### 4) Set environment variables in WSL

From repository root:

```bash
export PROJECT_ROOT="$(pwd)"
export N64_INST="$PROJECT_ROOT/libdragon"
export N64_GCCPREFIX=/opt/libdragon
export PATH="$N64_INST/bin:$PATH"
```

This project uses:
- N64_GCCPREFIX=/opt/libdragon for compiler tools
- N64_INST=<repo>/libdragon for local libdragon install and host tools

### 5) One-time local libdragon install inside repo

```bash
make -C libdragon libdragon tools -j"$(nproc)" N64_INST="$N64_INST" N64_GCCPREFIX="$N64_GCCPREFIX"
make -C libdragon install tools-install install-mk -j"$(nproc)" N64_INST="$N64_INST" N64_GCCPREFIX="$N64_GCCPREFIX"
```

### 6) Build DOOM-N64

```bash
make -j"$(nproc)"
```

Output ROM:

```text
doom.z64
```

## Optional: Persist variables in ~/.bashrc

Append this block to ~/.bashrc and reopen WSL:

```bash
export N64_GCCPREFIX=/opt/libdragon
export N64_INST=/mnt/c/Users/<your-user>/DOOM-N64/libdragon
export PATH="$N64_INST/bin:$PATH"
```

## Common Errors

### /opt/libdragon/bin/mkdfs: not found

Cause:
- /opt/libdragon has compiler binaries but not host asset tools

Fix:
- Run the one-time local libdragon install step above
- Confirm PATH contains $N64_INST/bin

### Missing IWAD: place your legally-owned WAD files in WADs/ ...

Cause:
- No compatible IWAD available in WADs/

Fix:
- Add at least one legally owned IWAD to WADs/ and rebuild
- Keep PWADs alongside an IWAD (PWAD-only set is not bootable)

### PWAD selected but game returns to browser

Cause:
- Selected WAD is PWAD and no compatible IWAD is available
- Base IWAD picker was canceled

Fix:
- Ensure at least one valid IWAD exists in WADs/
- Select the PWAD, then choose a base IWAD when prompted

## WAD Browser Controls

At startup, browser classifies each WAD:
- IWAD: can be launched directly
- PWAD: requires selecting a base IWAD
- BAD/ERR: invalid or unreadable WAD and cannot be launched

Controls:
- D-Pad Up/Down or Analog Up/Down: Move selection
- D-Pad Left/Right or C-Up/C-Down: Page up/down
- A or Start: Select highlighted entry
- B: Quick-select default IWAD (DOOM.WAD if available, otherwise first compatible IWAD)

When a PWAD is selected:
- A or Start: Confirm base IWAD in base picker
- B: Cancel base IWAD picker and return to WAD list

## Repository Notes

- Original id source release text is preserved in README_Original.TXT
- Porting roadmap is in Docs/PORTING_PLAN.md
- External libraries:
  - libdragon
  - tiny3d

## License

- Source code remains under original project licenses (see LICENSE.TXT and component licenses)
- DOOM.WAD is not redistributed by this repository

## N64 Controller Mappings

Current default bindings from the N64 platform input layer:

- Start: Open/close menu
- Analog Thumbstick: Move forward/back, turn left/right
- D-Pad: Move forward/back, turn left/right
- C-Left: Strafe left
- C-Right: Strafe right
- C-Up: Toggle automap
- C-Down: Use/Open
- Z: Fire
- L: Fire
- R: Run
- A: Previous weapon, Confirm/Enter
- B: Next weapon, Back

# Credits
`Credits.md` contains licensing and acknowledgment to contributing members