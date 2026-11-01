/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// sw_polyset.c: routines for drawing sets of polygons sharing the same
// texture (used for Alias models)

#include "header/local.h"
#include "header/sw_mt.h"
#include <limits.h>

typedef struct {
	int	numleftedges;
	compactvert_t	*pleftedgevert0;
	compactvert_t	*pleftedgevert1;
	compactvert_t	*pleftedgevert2;
	int	numrightedges;
	compactvert_t	*prightedgevert0;
	compactvert_t	*prightedgevert1;
	compactvert_t	*prightedgevert2;
} edgetable;

/* (per thread: the models are drawn on both, sw_mt.c) */
static SW_TLS int	ubasestep, errorterm, erroradjustup, erroradjustdown;

static SW_TLS compactvert_t r_p[3];
#define r_p0 r_p[0]
#define r_p1 r_p[1]
#define r_p2 r_p[2]

static SW_TLS int	d_xdenom;

static SW_TLS edgetable *pedgetable;

/* the edge tables as indices into r_p (a thread's r_p has no constant address): each thread
   builds the one it uses (R_UseEdgeTable); the same vertices as the original's tables */
static const unsigned char edgetables[12][8] = {
	{1, 0, 2, 2, 2, 0, 1, 2},
	{2, 1, 0, 2, 1, 1, 2, 2},
	{1, 0, 2, 2, 1, 1, 2, 2},	// flat top
	{1, 1, 0, 0, 2, 1, 2, 0},
	{2, 0, 2, 1, 1, 0, 1, 1},
	{1, 2, 1, 1, 1, 2, 0, 0},
	{1, 2, 1, 1, 2, 2, 0, 1},
	{2, 2, 1, 0, 1, 2, 0, 0},
	{1, 1, 0, 0, 1, 1, 2, 2},
	{1, 2, 1, 1, 1, 0, 1, 1},	// flat top
	{1, 1, 0, 0, 1, 2, 0, 0},	// flat top
	{1, 0, 2, 2, 1, 0, 1, 1},
};
static SW_TLS edgetable s_edgetable;

static void
R_UseEdgeTable(int i)
{
	const unsigned char *e = edgetables[i];

	s_edgetable.numleftedges = e[0];
	s_edgetable.pleftedgevert0 = &r_p[e[1]];
	s_edgetable.pleftedgevert1 = &r_p[e[2]];
	s_edgetable.pleftedgevert2 = &r_p[e[3]];
	s_edgetable.numrightedges = e[4];
	s_edgetable.prightedgevert0 = &r_p[e[5]];
	s_edgetable.prightedgevert1 = &r_p[e[6]];
	s_edgetable.prightedgevert2 = &r_p[e[7]];
	pedgetable = &s_edgetable;
}

// FIXME: some of these can become statics
static SW_TLS int	a_sstepxfrac, a_tstepxfrac, a_ststepxwhole;
static SW_TLS int	r_sstepx, r_tstepx, r_sstepy, r_tstepy;
static SW_TLS light3_t	r_lstepx, r_lstepy;
static SW_TLS zvalue_t	r_zistepx, r_zistepy;
static SW_TLS int	d_aspancount;

static SW_TLS spanpackage_t	*d_pedgespanpackage;

SW_TLS spanpackage_t	*triangle_spans, *triangles_max;	// (the worker has its own: sw_mt.c)

static SW_TLS int	d_sfrac, d_tfrac;
static SW_TLS light3_t d_light;
static SW_TLS zvalue_t	d_zi;
static SW_TLS int	d_ptexextrastep, d_sfracextrastep;
static SW_TLS int	d_tfracextrastep, d_ptexbasestep;
static SW_TLS light3_t	d_lightbasestep, d_lightextrastep;
static SW_TLS int	d_sfracbasestep, d_tfracbasestep;
static SW_TLS zvalue_t	d_ziextrastep, d_zibasestep;

SW_TLS void	(*d_pdrawspans)(const entity_t *currententity, spanpackage_t *pspanpackage);

static void R_PolysetSetEdgeTable(void);
static void R_RasterizeAliasPolySmooth(const entity_t *currententity);

// ======================
// 64 65 66 67 68 69 70 71   72 73 74 75 76 77 78 79
static const byte irtable[256] = {
	79, 78, 77, 76, 75, 74, 73, 72,		// black/white
	71, 70, 69, 68, 67, 66, 65, 64,
	64, 65, 66, 67, 68, 69, 70, 71,		// dark taupe
	72, 73, 74, 75, 76, 77, 78, 79,

	64, 65, 66, 67, 68, 69, 70, 71,		// slate grey
	72, 73, 74, 75, 76, 77, 78, 79,
	208, 208, 208, 208, 208, 208, 208, 208,	// unused?'
	64, 66, 68, 70, 72, 74, 76, 78,		// dark yellow

	64, 65, 66, 67, 68, 69, 70, 71,		// dark red
	72, 73, 74, 75, 76, 77, 78, 79,
	64, 65, 66, 67, 68, 69, 70, 71,		// grey/tan
	72, 73, 74, 75, 76, 77, 78, 79,

	64, 66, 68, 70, 72, 74, 76, 78,		// chocolate
	68, 67, 66, 65, 64, 65, 66, 67,		// mauve / teal
	68, 69, 70, 71, 72, 73, 74, 75,
	76, 76, 77, 77, 78, 78, 79, 79,

	64, 65, 66, 67, 68, 69, 70, 71,		// more mauve
	72, 73, 74, 75, 76, 77, 78, 79,
	64, 65, 66, 67, 68, 69, 70, 71,		// olive
	72, 73, 74, 75, 76, 77, 78, 79,

	64, 65, 66, 67, 68, 69, 70, 71,		// maroon
	72, 73, 74, 75, 76, 77, 78, 79,
	64, 65, 66, 67, 68, 69, 70, 71,		// sky blue
	72, 73, 74, 75, 76, 77, 78, 79,

	64, 65, 66, 67, 68, 69, 70, 71,		// olive again
	72, 73, 74, 75, 76, 77, 78, 79,
	64, 65, 66, 67, 68, 69, 70, 71,		// nuclear green
	64, 65, 66, 67, 68, 69, 70, 71,		// bright yellow

	64, 65, 66, 67, 68, 69, 70, 71,		// fire colors
	72, 73, 74, 75, 76, 77, 78, 79,
	208, 208, 64, 64, 70, 71, 72, 64,		// mishmash1
	66, 68, 70, 64, 65, 66, 67, 68};		// mishmash2

// ======================

/*
================
R_DrawTriangle
================
*/
void
R_DrawTriangle(const entity_t *currententity, const finalvert_t *a, const finalvert_t *b, const finalvert_t *c)
{
	int dv1_ab, dv0_ac;
	int dv0_ab, dv1_ac;

	/*
	d_xdenom = ( a->v[1] - b->v[1] ) * ( a->v[0] - c->v[0] ) -
			   ( a->v[0] - b->v[0] ) * ( a->v[1] - c->v[1] );
	*/

	dv0_ab = a->cv.u - b->cv.u;
	dv1_ab = a->cv.v - b->cv.v;

	if ( !( dv0_ab | dv1_ab ) )
		return;

	dv0_ac = a->cv.u - c->cv.u;
	dv1_ac = a->cv.v - c->cv.v;

	if ( !( dv0_ac | dv1_ac ) )
		return;

	d_xdenom = ( dv0_ac * dv1_ab ) - ( dv0_ab * dv1_ac );

	if ( d_xdenom < 0 && SW_MT_RecordTriangle(currententity, a, b, c) )
	{
		return;	/* drawn later on both threads (sw_mt.c) */
	}

	if ( d_xdenom < 0 )
	{
		memcpy(&r_p0, &a->cv, sizeof(compactvert_t));
		memcpy(&r_p1, &b->cv, sizeof(compactvert_t));
		memcpy(&r_p2, &c->cv, sizeof(compactvert_t));

		R_PolysetSetEdgeTable ();
		R_RasterizeAliasPolySmooth(currententity);
	}
}

static void
R_PushEdgesSpan(int u, int v, int count,
		pixel_t* d_ptex, int d_sfrac, int d_tfrac, light3_t d_light, zvalue_t d_zi)
{
	d_pedgespanpackage->u = u;
	d_pedgespanpackage->v = v;
	d_pedgespanpackage->count = count;
	d_pedgespanpackage->ptex = d_ptex;

	d_pedgespanpackage->sfrac = d_sfrac;
	d_pedgespanpackage->tfrac = d_tfrac;

	// FIXME: need to clamp l, s, t, at both ends?
	memcpy(d_pedgespanpackage->light, d_light, sizeof(light3_t));
	d_pedgespanpackage->zi = d_zi;

	d_pedgespanpackage++;
}

/*
===================
R_PolysetScanLeftEdge_C
====================
*/
static void
R_PolysetScanLeftEdge_C(int height, pixel_t *d_ptex, int u, int v)
{
	/* (Miyoo Mini port: the scan state in locals, written back at the end. The stores into the
	 * span packages may alias every one of these __thread variables, so gcc reloaded and
	 * stored them for every row. The same steps in the same order: R_PushEdgesSpan, then the
	 * extra or the base step.) */
	spanpackage_t	*pkg = d_pedgespanpackage;
	int		aspancount = d_aspancount, err = errorterm;
	int		sfrac = d_sfrac, tfrac = d_tfrac;
	int		l0 = d_light[0], l1 = d_light[1], l2 = d_light[2];
	zvalue_t	zi = d_zi;
	const int	basestep = ubasestep, errup = erroradjustup, errdown = erroradjustdown;
	const int	skinwidth = r_affinetridesc.skinwidth;
	const int	ptexextra = d_ptexextrastep, sfracextra = d_sfracextrastep;
	const int	tfracextra = d_tfracextrastep;
	const int	ptexbase = d_ptexbasestep, sfracbase = d_sfracbasestep;
	const int	tfracbase = d_tfracbasestep;
	const int	lextra0 = d_lightextrastep[0], lextra1 = d_lightextrastep[1];
	const int	lextra2 = d_lightextrastep[2];
	const int	lbase0 = d_lightbasestep[0], lbase1 = d_lightbasestep[1];
	const int	lbase2 = d_lightbasestep[2];
	const zvalue_t	ziextra = d_ziextrastep, zibase = d_zibasestep;

	do
	{
		pkg->u = u;
		pkg->v = v;
		pkg->count = aspancount;
		pkg->ptex = d_ptex;
		pkg->sfrac = sfrac;
		pkg->tfrac = tfrac;
		pkg->light[0] = l0;
		pkg->light[1] = l1;
		pkg->light[2] = l2;
		pkg->zi = zi;
		pkg++;

		v ++;
		u += basestep;
		aspancount += basestep;

		err += errup;
		if (err >= 0)
		{
			// addtional step for compensate error
			u ++;
			aspancount ++;

			d_ptex += ptexextra;
			sfrac += sfracextra;
			d_ptex += sfrac >> SHIFT16XYZ;

			sfrac &= 0xFFFF;
			tfrac += tfracextra;
			if (tfrac & 0x10000)
			{
				d_ptex += skinwidth;
				tfrac &= 0xFFFF;
			}

			l0 += lextra0;
			l1 += lextra1;
			l2 += lextra2;

			zi += ziextra;
			err -= errdown;
		}
		else
		{
			d_ptex += ptexbase;
			sfrac += sfracbase;
			d_ptex += sfrac >> SHIFT16XYZ;
			sfrac &= 0xFFFF;
			tfrac += tfracbase;
			if (tfrac & 0x10000)
			{
				d_ptex += skinwidth;
				tfrac &= 0xFFFF;
			}

			l0 += lbase0;
			l1 += lbase1;
			l2 += lbase2;

			zi += zibase;
		}
	} while (--height);

	d_pedgespanpackage = pkg;
	d_aspancount = aspancount;
	errorterm = err;
	d_sfrac = sfrac;
	d_tfrac = tfrac;
	d_light[0] = l0;
	d_light[1] = l1;
	d_light[2] = l2;
	d_zi = zi;
}

/*
===================
FloorDivMod

Returns mathematically correct (floor-based) quotient and remainder for
numer and denom, both of which should contain no fractional part. The
quotient must fit in 32 bits.
FIXME: GET RID OF THIS! (FloorDivMod)
====================
*/
static void
FloorDivMod(int numer, int denom, int *quo, int *rem)
{
	int q, r;

// just exclude ARM32 with FPU, e.g. Pi 1
#if defined(__i386__) || defined(__amd64__)
	q = numer / denom;
	r = numer - q * denom;

	if (-1/2 || 1/-2 || -1/-2) {
		// long live C89
		if (r < 0 && r < denom) {
			q += 1;
			r -= denom;
		}
	}
	else
	{
		// C99 always truncates to 0
		if ((numer ^ denom) < 0 && r != 0)
		{
			q -= 1;
			r += denom;
		}
	}

	if ((numer < 0) ^ (denom < 0))
	{
		assert(q <= 0);
	}
	else
	{
		assert(q >= 0);
	}

	if (denom < 0)
	{
		assert(r > denom && r <= 0);
	}
	else
	{
		assert(r >= 0 && r < denom);
	}
#else
	float num = numer, den = denom;
	float	x;

	if (numer >= 0)
	{

		x = floor(num / den);
		q = (int)x;
		r = (int)floor(num - (x * den));
	}
	else
	{
		//
		// perform operations with positive values, and fix mod to make floor-based
		//
		x = floor(-num / den);
		q = -(int)x;
		r = (int)floor(-num - (x * den));
		if (r != 0)
		{
			q--;
			r = denom - r;
		}
	}
#endif
	*quo = q;
	*rem = r;
}

/*
===================
R_PolysetSetUpForLineScan
====================
*/
static void
R_PolysetSetUpForLineScan(fixed8_t startvertu, fixed8_t startvertv,
		fixed8_t endvertu, fixed8_t endvertv)
{
	int tm, tn;

	tm = endvertu - startvertu;
	tn = endvertv - startvertv;

	errorterm = -1;
	erroradjustdown = tn;

	FloorDivMod (tm, tn, &ubasestep, &erroradjustup);
}

/*
================
R_PolysetCalcGradients
================
*/
static void
R_PolysetCalcGradients (int skinwidth)
{
	float	xstepdenominv, ystepdenominv, t0, t1;
	float	p01_minus_p21, p11_minus_p21, p00_minus_p20, p10_minus_p20;
	int i;

	p00_minus_p20 = r_p0.u - r_p2.u;
	p01_minus_p21 = r_p0.v - r_p2.v;
	p10_minus_p20 = r_p1.u - r_p2.u;
	p11_minus_p21 = r_p1.v - r_p2.v;

	xstepdenominv = 1.0 / (float)d_xdenom;

	ystepdenominv = -xstepdenominv;

	// ceil () for light so positive steps are exaggerated, negative steps
	// diminished,  pushing us away from underflow toward overflow. Underflow is
	// very visible, overflow is very unlikely, because of ambient lighting
	for (i=0; i<3; i++)
	{
		t0 = r_p0.l[i] - r_p2.l[i];
		t1 = r_p1.l[i] - r_p2.l[i];
		r_lstepx[i] = (int)
				ceil((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv);
		r_lstepy[i] = (int)
				ceil((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv);
	}

	t0 = r_p0.s - r_p2.s;
	t1 = r_p1.s - r_p2.s;
	r_sstepx = (int)((t1 * p01_minus_p21 - t0 * p11_minus_p21) *
			xstepdenominv);
	r_sstepy = (int)((t1 * p00_minus_p20 - t0* p10_minus_p20) *
			ystepdenominv);

	t0 = r_p0.t - r_p2.t;
	t1 = r_p1.t - r_p2.t;
	r_tstepx = (int)((t1 * p01_minus_p21 - t0 * p11_minus_p21) *
			xstepdenominv);
	r_tstepy = (int)((t1 * p00_minus_p20 - t0 * p10_minus_p20) *
			ystepdenominv);

	t0 = r_p0.zi - r_p2.zi;
	t1 = r_p1.zi - r_p2.zi;
	r_zistepx = (int)((t1 * p01_minus_p21 - t0 * p11_minus_p21) *
			xstepdenominv);
	r_zistepy = (int)((t1 * p00_minus_p20 - t0 * p10_minus_p20) *
			ystepdenominv);

	{
		a_sstepxfrac = r_sstepx & 0xFFFF;
		a_tstepxfrac = r_tstepx & 0xFFFF;
	}

	a_ststepxwhole = skinwidth * (r_tstepx >> SHIFT16XYZ) + (r_sstepx >> SHIFT16XYZ);
}


/*
================
R_PolysetDrawSpans8
================
*/
/*
 * (Miyoo Mini port: the span drawers as two templates. A store through the pixel or the z
 * pointer (char / int) may alias any global, so gcc reloaded every step, table pointer and the
 * span state (errorterm, d_aspancount: __thread here) for every pixel. The templates take them
 * into locals once per call and write the span state back at the end; the arithmetic, the
 * order and the pixels are those of Yamagi's R_PolysetDrawSpans8_33 / _66 / _Opaque and
 * R_PolysetDrawSpansConstant8_33 / _66.)
 */
enum { SPANS_OPAQUE, SPANS_33, SPANS_66 };

static inline __attribute__((always_inline)) void
R_PolysetDrawSpansTex(const entity_t *currententity, spanpackage_t *pspanpackage,
	const int mode)
{
	const zvalue_t	zistepx = r_zistepx;
	const int	lstep0 = r_lstepx[0], lstep1 = r_lstepx[1], lstep2 = r_lstepx[2];
	const int	ststepxwhole = a_ststepxwhole;
	const int	sstepxfrac = a_sstepxfrac, tstepxfrac = a_tstepxfrac;
	const int	skinwidth = r_affinetridesc.skinwidth;
	const int	errup = erroradjustup, errdown = erroradjustdown, basestep = ubasestep;
	const int	width = vid_buffer_width;
	pixel_t *const	viewbuffer = d_viewbuffer;
	zvalue_t *const	zbuffer = d_pzbuffer;
	const pixel_t *const colormap = vid_colormap;
	const byte *const c16to8 = d_16to8table;
	const pixel_t *const alphamap = vid_alphamap;
	const qboolean	ir = mode == SPANS_OPAQUE &&
		(r_newrefdef.rdflags & RDF_IRGOGGLES) && (currententity->flags & RF_IR_VISIBLE);
	int	aspancount = d_aspancount, err = errorterm;

	do
	{
		int lcount;

		lcount = aspancount - pspanpackage->count;
		err += errup;
		aspancount += basestep;

		if (err >= 0)
		{
			// addtional step for compensate error
			aspancount ++;
			err -= errdown;
		}

		if (lcount > 0 && !SW_SPAN_OUTSIDE_BAND(pspanpackage))	/* (sw_mt.c) */
		{
			const int	pos_shift = (pspanpackage->v * width) + pspanpackage->u;
			pixel_t		*lpdest = viewbuffer + pos_shift;
			zvalue_t	*lpz = zbuffer + pos_shift;
			const pixel_t	*lptex = pspanpackage->ptex;
			int		lsfrac = pspanpackage->sfrac;
			int		ltfrac = pspanpackage->tfrac;
			int		l0 = pspanpackage->light[0];
			int		l1 = pspanpackage->light[1];
			int		l2 = pspanpackage->light[2];
			zvalue_t	lzi = pspanpackage->zi;
			qboolean	zdamaged = false;

			do
			{
				if ((lzi >> SHIFT16XYZ) >= *lpz)
				{
					if (mode == SPANS_OPAQUE)
					{
						if (ir)
							*lpdest = colormap[irtable[*lptex]];
						else
							*lpdest = R_ApplyLightTab(*lptex, l0, l1, l2, colormap, c16to8);
					}
					else
					{
						int temp = R_ApplyLightTab(*lptex, l0, l1, l2, colormap, c16to8);

						if (mode == SPANS_33)
							*lpdest = alphamap[temp + *lpdest*256];
						else
							*lpdest = alphamap[temp*256 + *lpdest];
					}

					if (mode != SPANS_33)
					{
						*lpz = lzi >> SHIFT16XYZ;
						zdamaged = true;
					}
				}
				lpdest++;
				lzi += zistepx;
				lpz++;
				l0 += lstep0;
				l1 += lstep1;
				l2 += lstep2;
				lptex += ststepxwhole;
				lsfrac += sstepxfrac;
				lptex += lsfrac >> SHIFT16XYZ;
				lsfrac &= 0xFFFF;
				ltfrac += tstepxfrac;
				if (ltfrac & 0x10000)
				{
					lptex += skinwidth;
					ltfrac &= 0xFFFF;
				}
			} while (--lcount);

			if (zdamaged)
			{
				// damaged only current line
				VID_DamageZBuffer(pspanpackage->u, pspanpackage->v);
				VID_DamageZBuffer(
					pspanpackage->u + aspancount - pspanpackage->count,
					pspanpackage->v);
			}
		}

		pspanpackage++;
	} while (pspanpackage->count != INT_MIN);

	d_aspancount = aspancount;
	errorterm = err;
}

static inline __attribute__((always_inline)) void
R_PolysetDrawSpansConst(spanpackage_t *pspanpackage, const int mode)
{
	const zvalue_t	zistepx = r_zistepx;
	const int	errup = erroradjustup, errdown = erroradjustdown, basestep = ubasestep;
	const int	width = vid_buffer_width;
	pixel_t *const	viewbuffer = d_viewbuffer;
	zvalue_t *const	zbuffer = d_pzbuffer;
	const pixel_t *const alphamap = vid_alphamap;
	const int	blendcolor = r_aliasblendcolor;
	int	aspancount = d_aspancount, err = errorterm;

	do
	{
		int lcount;

		lcount = aspancount - pspanpackage->count;
		err += errup;
		aspancount += basestep;

		if (err >= 0)
		{
			// addtional step for compensate error
			aspancount ++;
			err -= errdown;
		}

		if (lcount > 0 && !SW_SPAN_OUTSIDE_BAND(pspanpackage))	/* (sw_mt.c) */
		{
			const int	pos_shift = (pspanpackage->v * width) + pspanpackage->u;
			pixel_t		*lpdest = viewbuffer + pos_shift;
			zvalue_t	*lpz = zbuffer + pos_shift;
			zvalue_t	lzi = pspanpackage->zi;
			qboolean	zdamaged = false;

			do
			{
				if ((lzi >> SHIFT16XYZ) >= *lpz)
				{
					if (mode == SPANS_33)
					{
						*lpdest = alphamap[blendcolor + *lpdest*256];
					}
					else
					{
						*lpdest = alphamap[blendcolor*256 + *lpdest];
						zdamaged = true;
					}
				}
				lpdest++;
				lzi += zistepx;
				lpz++;
			} while (--lcount);

			if (zdamaged)
			{
				// damaged only current line
				VID_DamageZBuffer(pspanpackage->u, pspanpackage->v);
				VID_DamageZBuffer(
					pspanpackage->u + aspancount - pspanpackage->count,
					pspanpackage->v);
			}
		}

		pspanpackage++;
	} while (pspanpackage->count != INT_MIN);

	d_aspancount = aspancount;
	errorterm = err;
}

void
R_PolysetDrawSpans8_33(const entity_t *currententity, spanpackage_t *pspanpackage)
{
	R_PolysetDrawSpansTex(currententity, pspanpackage, SPANS_33);
}

void
R_PolysetDrawSpansConstant8_33(const entity_t *currententity, spanpackage_t *pspanpackage)
{
	R_PolysetDrawSpansConst(pspanpackage, SPANS_33);
}

void
R_PolysetDrawSpans8_66(const entity_t *currententity, spanpackage_t *pspanpackage)
{
	R_PolysetDrawSpansTex(currententity, pspanpackage, SPANS_66);
}

void
R_PolysetDrawSpansConstant8_66(const entity_t *currententity, spanpackage_t *pspanpackage)
{
	R_PolysetDrawSpansConst(pspanpackage, SPANS_66);
}

void
R_PolysetDrawSpans8_Opaque (const entity_t *currententity, spanpackage_t *pspanpackage)
{
	R_PolysetDrawSpansTex(currententity, pspanpackage, SPANS_OPAQUE);
}

static void
R_ProcessLeftEdge(const compactvert_t *plefttop, const compactvert_t *prighttop,
		const compactvert_t *pleftbottom)
{
	light3_t working_lstepx;
	pixel_t	*d_ptex;
	int u, v;
	int s, t;
	int height;
	int i;

	u = plefttop->u;
	v = plefttop->v;
	d_aspancount = plefttop->u - prighttop->u;

	s = plefttop->s * r_affinetridesc.scalewidth;
	t = plefttop->t * r_affinetridesc.scaleheight;
	i = (s >> SHIFT16XYZ) + (t >> SHIFT16XYZ) * r_affinetridesc.skinwidth;
	d_ptex = &r_affinetridesc.pskin[i];
	d_sfrac = s & 0xFFFF;
	d_tfrac = t & 0xFFFF;

	memcpy(d_light, plefttop->l, sizeof(light3_t));
	d_zi = plefttop->zi;

	height = pleftbottom->v - plefttop->v;
	if (height == 1)
	{
		// skip calculations needed for 2+ spans
		R_PushEdgesSpan(u, v, d_aspancount,
				d_ptex, d_sfrac, d_tfrac, d_light, d_zi);
		return;
	}
	R_PolysetSetUpForLineScan(plefttop->u, plefttop->v,
				  pleftbottom->u, pleftbottom->v);

	// for negative steps in x along left edge, bias toward overflow rather than
	// underflow (sort of turning the floor () we did in the gradient calcs into
	// ceil (), but plus a little bit)
	t = ubasestep < 0;
	for (i = 0; i < 3; i++)
	{
		working_lstepx[i] = r_lstepx[i] - t;
	}

	// base steps for drawers
	s = r_sstepy + r_sstepx * ubasestep;
	t = r_tstepy + r_tstepx * ubasestep;
	d_ptexbasestep = (s >> SHIFT16XYZ) +
			 (t >> SHIFT16XYZ) * r_affinetridesc.skinwidth;
	d_sfracbasestep = s & 0xFFFF;
	d_tfracbasestep = t & 0xFFFF;

	for (i = 0; i < 3; i++)
	{
		d_lightbasestep[i] = r_lstepy[i] + working_lstepx[i] * ubasestep;
	}

	d_zibasestep = r_zistepy + r_zistepx * ubasestep;

	// extra steps for drawers
	s += r_sstepx;
	t += r_tstepx;
	d_ptexextrastep = (s >> SHIFT16XYZ) +
			  (t >> SHIFT16XYZ) * r_affinetridesc.skinwidth;
	d_sfracextrastep = s & 0xFFFF;
	d_tfracextrastep = t & 0xFFFF;

	for (i = 0; i < 3; i++)
	{
		d_lightextrastep[i] = d_lightbasestep[i] + working_lstepx[i];
	}

	d_ziextrastep = d_zibasestep + r_zistepx;

	R_PolysetScanLeftEdge_C(height, d_ptex, u, v);
}

/*
================
R_RasterizeAliasPolySmooth
================
*/
static void
R_RasterizeAliasPolySmooth(const entity_t *currententity)
{
	const compactvert_t *pleftbottom, *prightbottom;
	const compactvert_t *plefttop, *prighttop;
	spanpackage_t *pstart;
	int originalcount;
	int leftheight, rightheight;

	// set the s, t, and light gradients, which are consistent across
	// the triangle because being a triangle, things are affine
	R_PolysetCalcGradients (r_affinetridesc.skinwidth);

	plefttop = pedgetable->pleftedgevert0;
	prighttop = pedgetable->prightedgevert0;

	pleftbottom = pedgetable->pleftedgevert1;
	prightbottom = pedgetable->prightedgevert1;

	// make sure we have enough spans for the full height
	// plus 1 more for the list terminator.
	// vert2 in the edgetable is always the lowest.
	// (this is silly anyway, just allocate vid.height+1)
	leftheight = pedgetable->pleftedgevert2->v - plefttop->v;
	rightheight = pedgetable->prightedgevert2->v - prighttop->v;
	if (leftheight < rightheight)
		leftheight = rightheight;
	if (&triangle_spans[leftheight] >= triangles_max)
	{
		r_outoftriangles = true;
		return;
	}
	// init span allocation ptr
	d_pedgespanpackage = triangle_spans;

	// 1. scan out the top (and possibly only) part of the left edge
	R_ProcessLeftEdge(plefttop, prighttop, pleftbottom);

	// 2. scan out the bottom part of the left edge, if it exists
	if (pedgetable->numleftedges == 2)
	{
		plefttop = pleftbottom;
		pleftbottom = pedgetable->pleftedgevert2;

		R_ProcessLeftEdge(plefttop, prighttop, pleftbottom);
	}
	assert(d_pedgespanpackage <= triangles_max);

	// 3. scan out the top (and possibly only) part of the right edge,
	//  drawing spans that terminate on it
	rightheight = prightbottom->v - prighttop->v;

	// reset u delta before drawing the top
	d_aspancount = 0;

	// save for step 4
	pstart = &triangle_spans[rightheight];
	originalcount = pstart->count;
	// mark end of the spanpackages
	pstart->count = INT_MIN;

	R_PolysetSetUpForLineScan(prighttop->u, prighttop->v,
				  prightbottom->u, prightbottom->v);
	(*d_pdrawspans) (currententity, triangle_spans);

	// 4. scan out the bottom part of the right edge, if it exists
	if (pedgetable->numrightedges == 2)
	{
		// 4.1 restore the starting span count
		pstart->count = originalcount;

		d_aspancount = prightbottom->u - prighttop->u;

		// 4.2 shift verts like in step 2
		prighttop = prightbottom;
		prightbottom = pedgetable->prightedgevert2;

		// 4.3 increase right height
		rightheight += prightbottom->v - prighttop->v;

		// mark end of the spanpackages
		triangle_spans[rightheight].count = INT_MIN;

		R_PolysetSetUpForLineScan(prighttop->u, prighttop->v,
					  prightbottom->u, prightbottom->v);
		(*d_pdrawspans) (currententity, pstart);
	}
}

/*
================
R_PolysetSetEdgeTable
================
*/
static void
R_PolysetSetEdgeTable(void)
{
	int			edgetableindex;

	edgetableindex = 0;	// assume the vertices are already in
				//  top to bottom order

	//
	// determine which edges are right & left, and the order in which
	// to rasterize them
	//
	if (r_p0.v >= r_p1.v)
	{
		if (r_p0.v == r_p1.v)
		{
			if (r_p0.v < r_p2.v)
				R_UseEdgeTable(2);
			else
				R_UseEdgeTable(5);

			return;
		}
		else
		{
			edgetableindex = 1;
		}
	}

	if (r_p0.v == r_p2.v)
	{
		if (edgetableindex)
			R_UseEdgeTable(8);
		else
			R_UseEdgeTable(9);

		return;
	}
	else if (r_p1.v == r_p2.v)
	{
		if (edgetableindex)
			R_UseEdgeTable(10);
		else
			R_UseEdgeTable(11);

		return;
	}

	if (r_p0.v > r_p2.v)
		edgetableindex += 2;

	if (r_p1.v > r_p2.v)
		edgetableindex += 4;

	R_UseEdgeTable(edgetableindex);
}
