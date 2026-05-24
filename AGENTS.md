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

# Project Purpose
This project is a software port of the original DOOM game for PC modified to run on the N64. The original WAD files can be found in this project WADs/DOOM.WAD

# Building The Project
This project use (Windows Subsystem for LInux) WSL instead of developing natively on Linux. USe the "WSL" command in the terminal to enter linux subsystem terminal. Once a buld runs on the N64, a debug.log file is created in the project root

# Target Platform Hardware
N64 game console witha RISC based MIPS R4300i-series processor. 8KB of L1 cache and no L2 cache. Lacks branch prediction and will always executes the first instruction after any IF or IF ELSE statement, so make the first instruction is simple and does not call into RAM. Whenever possible avoid using IF statements.

Graphic processors: SGI 62.5 MHz 64-bit RCP (Reality Co-Processor), with 2 sub-processors:
- RSP (Reality Signal Processor) Controls 3D graphics and audio functionalities
- RDP (Reality Drawing Processor) Rasterizer handles all pixel drawing operations in hardware

## Platform Bottleneck
The biggest bottleneck is the 250MB/s bandwidth of the Rambus DRAM (RDRAM). It is very expensive to fetch data from RAM with a latency of around 640ns for a single call. When possible, use Direct Memory Accees (DMA), the bandwidth can be as high as 562.5 MB/s

## RAM Budget
4 MB RDRAM base (8 MB with Expansion Pak). Use the stack instead of the heap whenever possible


## Variable Ordering
Be aware of the "padding" required to align variables of different types in CPU cache memory. Group variables of the same type together so that we reduce the memory footprint of the executing code. For example: a struct that has 3 variables: (char, int, char), will take up 12 bytes of cache even though there is only 6 bytes of actual data. If that same struct is rearranged to: (char, char, int) it will only take up 8 bytes of cache memory wasting only 2 bytes for padding. This is due to 32-bit (4-byte) natural alignment on the MIPS R4300i

## Coding Libraries Used
we are building our project on the follwing libraries developed by the N64 homebrew community. Each one has examples that are a good resource for best practices:
- Libdragon 
- Tiny3D

## Asset Pipeline
Some* source assets are converted by Makefile rules using libdragon tools:
- `.glb` → `.t3dm` (mkmodel)
- `.png` → `.sprite` (mksprite)
- `.xm` / `.ym` → `.xm64` / `.ym64` (audioconv64)

Built assets land in `filesystem/`

## Error handling
When errors occur in the buiild then we should gate them with assertions to be sure functions are being used correctly

## Data Type Considerations
The N64's FPU flushes denormalized floats to zero instead of following IEEE 754, meaning very small values near zero get silently discarded. This can cause division by zero, NaN propagation, or values snapping unexpectedly, especially in physics and audio calculations. The fix is to clamp small values, add epsilon guards before division, or use fixed-point math instead

## Key N64 Example Paths
- libdragon/ — External lib (DO NOT modify)
- tiny3d/ — External lib (DO NOT modify)
- Docs/PORTING_PLAN.md — Our plan to port this application

# Reference examples for best practices on using library functionality
- `libdragon/examples/` — Audio, input, rendering, filesystem demos
- `tiny3d/examples/` — 3D model loading, animation, lighting demos