/* The Miyoo Mini (Plus) platform layer of the Quake II port (miyoo/platform/): the part of SDL2
 * the client and ref_soft call, on the device's Linux directly. */
#ifndef MIYOO_PLATFORM_H
#define MIYOO_PLATFORM_H

#include <stdint.h>

/* miyoo_fb.c: the panel */
int MiyooFb_Open(void);
void MiyooFb_Present(const uint32_t *argb, int w, int h, int pitch);
void MiyooFb_Close(void);

#endif
