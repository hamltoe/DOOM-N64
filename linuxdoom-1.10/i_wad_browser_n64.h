// N64 runtime browser for selecting IWAD before DOOM startup.

#ifndef __I_WAD_BROWSER_N64__
#define __I_WAD_BROWSER_N64__

void I_N64RunWadBrowser(void);
const char* I_N64GetSelectedWadPath(void);
const char* I_N64GetSelectedBaseIwadPath(void);

#endif
