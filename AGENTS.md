# Project Purpose
This project is a software port of the original DOOM game for PC modified to run on the N64.

# Building The Project
This project can use (Windows Subsystem for LInux) WSL instead of developing natively on Linux. Use the "WSL" command in the terminal to enter linux subsystem terminal. Once a buld runs on the N64, a debug.log file is created in the project root
- Ignore unused warnings

## Build Flags
- DEBUG=1 — Debug mode allows on screen printing and writes logging data to debug.log file to be generated
- UPLOAD=1 — Invoke the upload script to load the built ROM onto the N64 Flash Cart (not yet implemented)

# Target Platform Hardware
N64 game console witha RISC based MIPS R4300i-series processor. 8KB of L1 cache and no L2 cache. Lacks branch prediction and will always executes the first instruction after any IF or IF ELSE statement, so make the first instruction is simple and does not call into RAM
Graphic processors: SGI 62.5 MHz 64-bit RCP (Reality Co-Processor), with 2 sub-processors:
- RSP (Reality Signal Processor) Controls 3D graphics and audio functionalities
- RDP (Reality Drawing Processor) Rasterizer handles all pixel drawing operations in hardware

## Platform Bottleneck
The biggest bottleneck is the 250MB/s bandwidth of the Rambus DRAM (RDRAM). It is very expensive to fetch data from RAM with a latency of around 640ns for a single call. When possible, use Direct Memory Accees (DMA), the bandwidth can be as high as 562.5 MB/s

## Asset Pipeline
Built assets consist of original .WAD file copied to `filesystem/` that is used by libdragon to read assets at runtime

## Data Type Considerations
The N64's FPU flushes denormalized floats to zero instead of following IEEE 754, meaning very small values near zero get silently discarded. This can cause division by zero, NaN propagation, or values snapping unexpectedly, especially in physics and audio calculations. The fix is to clamp small values, add epsilon guards before division, or use fixed-point math instead

## Key Project Paths
- `linuxdoom-1.10/` — Core DOOM project files
- `libdragon/` — External lib (DO NOT modify)
- `tiny3d/` — External lib (DO NOT modify)
- `Docs/` — Project related documents and instructions

# Coding Libraries Used
we are building our project on the follwing libraries developed by the N64 homebrew community. Each one has examples that are a good resource for best practices:
- Libdragon — https://github.com/DragonMinded/libdragon.git
- Tiny3D — https://github.com/HailToDodongo/tiny3d.git

## Reference examples for best practices on using library functionality
- `libdragon/examples/` — Audio, input, rendering, filesystem demos
- `tiny3d/examples/` — 3D model loading, animation, lighting demos

# Smart Caveman Speaking Mode
## Core Rules
Respond like you Smart Caveman. Cut articles, filler, pleasantries. But keep all technical substance

## Grammar
- Drop articles (a, an, the)
- Drop filler (just, really, basically, actually, simply)
- Drop pleasantries (sure, basically, actually, simply)
- Short synonyms (big not extensive, fix not "implement a solution for")
- Fragments fine. No need full sentence
- Technical terms stay exact: "Polymorphism" stays "Polymorphism"
- Code blocks unchanged. Smart Caveman speak around code, not in code
- Error message quoted exactly. "Smart Caveman" only for explanation