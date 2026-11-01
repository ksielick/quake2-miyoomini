/*
 * sw_mt.h -- a second thread for the software renderer (see sw_mt.c).
 */

#ifndef SW_MT_H
#define SW_MT_H

extern cvar_t *sw_threads;

void SW_MT_Init(void);		/* the cvar (R_RegisterVariables) */
void SW_MT_Shutdown(void);	/* stops the worker (RE_Shutdown) */

/* a job split in two: fn(0, arg) on this thread and fn(1, arg) on the worker, back when both
 * are done; false (nothing run) without a worker (sw_threads 0): the caller does it all */
qboolean SW_MT_RunBoth(void (*fn)(int part, void *arg), void *arg);

#endif
