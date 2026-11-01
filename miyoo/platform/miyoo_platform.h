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

#endif
