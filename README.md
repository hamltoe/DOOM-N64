# DOOM-N64

DOOM-N64 is a Windows-friendly porting project for running classic DOOM on the Nintendo 64 using libdragon.

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

IWAD handling:
- Place your legally owned .WAD files in WADs/
- Build syncs all .wad/.WAD files from WADs/ into filesystem/
- Keep at least one IWAD (for example DOOM.WAD)

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

Copy your legally owned WAD files to the WADs folder:

```text
WADs/DOOM1.WAD
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
- No IWAD available in WADs/ and filesystem/doom.wad missing

Fix:
- Add at least one legally owned IWAD to WADs/ and rebuild

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
- Analog Stick: Move forward/back and turn left/right
- C-Left: Strafe left
- C-Right: Strafe right
- C-Up: Run (speed modifier)
- C-Down: Use/Open
- L: Toggle automap
- R: Fire
- D-Pad Up: Pause
- D-Pad Down: Cycle thru selectable weapons
- D-Pad Left: Select previous weapon
- D-Pad Right: Select next weapon
- A : Fire & Confirm/Enter (Menu)
- B : Use/Open & Back (Menu)
- Z : Fire
