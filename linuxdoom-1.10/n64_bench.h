// Deterministic A/B benchmark harness for the N64 DOOM renderer.
// Compiled in only when N64_BENCH is defined (make BENCH=1).
//
// Measures per-frame render cost in VR4300 ticks (get_ticks(), the CP0 cycle
// counter), which is deterministic under emulation and host-speed independent.
// Drives a fixed scripted ticcmd through the SHIPPING uncapped/interpolated
// render path, then freezes a large on-screen result the host can screenshot.

#ifndef N64_BENCH_H
#define N64_BENCH_H

#ifdef N64_BENCH

#include "d_ticcmd.h"

// Wired into D_DoomMain: forces autostart of the bench scenario and selects
// the uncapped single-player render path. Returns the map to warp to.
void N64Bench_Init(void);

// True while the bench is collecting samples (D_DoomLoop forces uncapped path).
int  N64Bench_Active(void);

// Called from D_DoomLoop around D_Display to time one rendered frame.
void N64Bench_FrameBegin(void);
void N64Bench_FrameEnd(void);

// --- per-phase profiling --------------------------------------------------
// All phase boundaries are CP0-cheap (get_ticks()); kept out of inner loops.
// Each Begin/End pair brackets one phase and accumulates ticks into the
// current frame's slot. Counts are latched at FrameEnd (or explicit setters).
//
// Phase indices for the per-frame breakdown.
typedef enum
{
    BPH_GAMETIC = 0,   // TryRunTics (sim) in the uncapped path
    BPH_BSP,           // R_RenderBSPNode (BSP walk + seg rendering)
    BPH_PLANES,        // R_DrawPlanes
    BPH_MASKED,        // R_DrawMasked (sprite sort + sprites + masked segs + psprites)
    BPH_HUD,           // D_Display work outside the 3D view (status bar/HUD/border/menu)
    BPH_PRESENT,       // I_FinishUpdate (page flip / buffer-busy spin)
    BPH_AUDIO,         // S_UpdateSounds + I_SubmitSound (post-display)
    BPH_COUNT
} bench_phase_id_t;

// Brackets the whole D_DoomLoop iteration (sim + audio + display). The
// per-phase breakdown's "frame total" is this loop-wall time; "leftover" is
// the wall minus all accounted phases. Kept distinct from FrameBegin/FrameEnd,
// which still bracket only D_Display for the unchanged BENCH_RESULT line.
void N64Bench_LoopBegin(void);
void N64Bench_LoopEnd(void);

// Bracket a phase. End attributes (now - the matching Begin) to phase `id`.
void N64Bench_PhaseBegin(int id);
void N64Bench_PhaseEnd(int id);

// One get_ticks() read that ends `end_id` and begins `begin_id` -- used at
// back-to-back phase boundaries (e.g. bsp->planes->masked, separated only by a
// single-player no-op NetUpdate) so the hot render path takes one CP0 read per
// boundary instead of two.
void N64Bench_PhaseSwitch(int end_id, int begin_id);

// Brackets the whole D_Display call. DisplayEnd derives BPH_HUD as the display
// wall time minus the in-view render phases (bsp/planes/masked) and present,
// i.e. status bar + HUD + border + menu + render setup/clears -- everything in
// D_Display that is not the 3D draw or the page flip.
void N64Bench_DisplayBegin(void);
void N64Bench_DisplayEnd(void);

// Record how many gametics TryRunTics advanced this frame (0 or more).
void N64Bench_SetTicsRan(int tics);

// Latch the renderer's per-frame work counts (vissprites/drawsegs/visplanes).
// Called from R_RenderPlayerView after R_DrawMasked, when the pools are full.
void N64Bench_SetCounts(int vissprites, int drawsegs, int visplanes);

// Called once per gametic from G_Ticker to advance scenario timing/phases.
void N64Bench_TicHook(void);

// Computes results and freezes the overlay; called when the window completes.
void N64Bench_Finish(void);

// Called at the end of G_BuildTiccmd to overwrite real input with the script.
void N64Bench_FillTiccmd(ticcmd_t* cmd);

// Drawn at the end of D_Display (over everything) once the bench finishes,
// holding the final numbers on screen indefinitely for screenshot capture.
void N64Bench_DrawOverlay(void);

#endif // N64_BENCH
#endif // N64_BENCH_H
