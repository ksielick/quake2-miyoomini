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

/*
 * The world's spans on both threads (tyrquake-miyoo's d_mt.c): D_DrawSurfaces (sw_edge.c)
 * does each surface's setup (gradients, surface cache) as before, but records the span
 * drawing with a copy of the drawing state; SW_MT_Flush then replays the records on both
 * threads, each for its band of rows (the span drawers skip spans outside it). The spans of
 * one D_DrawSurfaces never overlap, so the bands can be drawn in any order.
 * Yamagi's z-damage check (D_DrawZSpans writes z only where the last frame's models wrote,
 * and widens that area as it goes) depends on the order: it is made while recording, in the
 * original order, into each span's zwrite.
 * A surface cache block used by a record is never overwritten before the records are drawn:
 * D_SCAlloc and D_CacheSurface call SW_MT_CacheOverwrite first, which draws them.
 */
enum
{
	SW_OP_SPANS,	/* D_DrawSpansPow2 + D_DrawZSpans */
	SW_OP_SKY,		/* D_DrawSpansPow2 + D_DrawZSpans at infinity */
	SW_OP_TURB,		/* TurbulentPow2 + D_DrawZSpans */
	SW_OP_NONTURB,	/* NonTurbulentPow2 + D_DrawZSpans */
	SW_OP_FLAT		/* a colour fill + D_DrawZSpans at infinity (the background) */
};

typedef struct
{
	int op, color;
	espan_t *spans;
	float ziorigin, zistepu, zistepv;
	float sdivzstepu, tdivzstepu, sdivzstepv, tdivzstepv, sdivzorigin, tdivzorigin;
	int sadjust, tadjust, bbextents, bbextentt;
	pixel_t *cacheblock;
	int cachewidth;
} sw_drawcmd_t;

/* the rows this thread draws, [sw_band_v0, sw_band_v1), while replaying */
extern SW_TLS int sw_band_v0, sw_band_v1;
extern SW_TLS int sw_mt_replaying;
#define SW_SPAN_OUTSIDE_BAND(p) \
	((unsigned)((p)->v - sw_band_v0) >= (unsigned)(sw_band_v1 - sw_band_v0))

/* D_DrawSurfaces: start recording (true) or draw as before (false: sw_threads 0) */
qboolean SW_MT_BeginRecord(int v0, int v1);
/* a new record (the caller fills it in) */
sw_drawcmd_t *SW_MT_NewCmd(void);
/* draw what was recorded on both threads, and stop recording */
void SW_MT_EndRecord(void);
/* surface caches: used by a record / about to be overwritten */
void SW_MT_CacheUse(surfcache_t *c);
void SW_MT_CacheOverwrite(surfcache_t *c);
/* sw_edge.c: draw one record (the drawing state already set) */
void SW_MT_Exec(const sw_drawcmd_t *c);

/* surface caches built on both threads: D_CacheSurface hands the build over while recording
 * (true: it will be built before the records are drawn; false: build it now) */
qboolean SW_MT_DeferBuild(const drawsurf_t *ds);
void R_BuildLightMap(drawsurf_t *drawsurf);	/* sw_light.c */
void R_DrawSurface(drawsurf_t *drawsurf);	/* sw_surf.c */

/*
 * Models on both threads (tyrquake-miyoo's D_PolysetBeginRecord & co): between
 * SW_MT_PolyBegin and SW_MT_PolyEnd, R_DrawTriangle records each triangle that faces the
 * view (its vertices, the skin, the span drawer, the shell colour); SW_MT_PolyFlush (also
 * End) draws them on both threads, each rasterizing all of them in order but drawing only the
 * rows of its band. Flush before drawing anything else that uses the z-buffer (sprites,
 * beams). The z-damage area the drawers widen is kept per thread while replaying
 * (sw_zdamage) and merged after.
 */
void SW_MT_PolyBegin(void);
void SW_MT_PolyFlush(void);
void SW_MT_PolyEnd(void);
qboolean SW_MT_RecordTriangle(const entity_t *e, const finalvert_t *a, const finalvert_t *b,
	const finalvert_t *c, int xdenom);
void R_DrawTriangle(const entity_t *currententity, const finalvert_t *a, const finalvert_t *b,
	const finalvert_t *c);
void R_DrawRecordedTriangle(const entity_t *currententity, const compactvert_t *v, int xdenom);
/* while replaying models: this thread's z-damage box (minu, maxu, minv, maxv), else NULL */
extern SW_TLS int *sw_zdamage;

#endif
