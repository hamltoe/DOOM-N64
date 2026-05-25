// Shared compile-time debug controls for N64 platform code.

#ifndef N64_DEBUG_H
#define N64_DEBUG_H

#ifndef DEBUG
#define DEBUG 0
#endif

#if DEBUG
#define DOOM_N64_DEBUG 1
#else
#define DOOM_N64_DEBUG 0
#endif

#if DOOM_N64_DEBUG
#define N64_DEBUGF(...) debugf(__VA_ARGS__)
#else
#define N64_DEBUGF(...) ((void)0)
#endif

#endif
