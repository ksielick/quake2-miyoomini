/* The Miyoo Mini (Plus) platform layer of the Quake II port (miyoo/platform/): the part of SDL2
 * the client and ref_soft call, on the device's Linux directly. */
#ifndef MIYOO_PLATFORM_H
#define MIYOO_PLATFORM_H

#include <stdint.h>

/* miyoo_fb.c: the panel */
int MiyooFb_Open(void);
void MiyooFb_Present(const uint32_t *argb, int w, int h, int pitch);
void MiyooFb_Close(void);
/* frames for the renderer: in memory the 2D engine reads, when it is there (open the panel first) */
uint32_t *MiyooFb_AllocFrame(int w, int h);
void MiyooFb_FreeFrame(uint32_t *px);
/* the video menu's max fps (vid_maxfps): <= 45 two refreshes a frame, <= 100 one, else
 * unlimited; called by src/common/frame.c every frame */
void MiyooFb_SetMaxFps(float fps);
/* 8-bit frames converted by the 2D engine (MI_GFX I8; MIYOO_GFX_I8=0: never) */
int MiyooFb_I8Usable(void);
int MiyooFb_PresentI8(const uint8_t *idx, const uint32_t *pal, int w, int h);

/* miyoo_sdl.c, for ref_soft (exported): the 8-bit frame the next SDL_RenderPresent shows
 * through MiyooFb_PresentI8; 0 if that path isn't there (then the renderer fills its
 * texture as usual). pal: 256 entries 0x00RRGGBB. */
__attribute__((visibility("default")))
int Miyoo_SetIndexedFrame(const uint8_t *idx, const uint32_t *pal, int w, int h);

#endif
