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
// sw_surf.c: surface-related refresh code

#include "header/local.h"
#include "header/sw_mt.h"

void R_BuildLightMap (drawsurf_t *drawsurf);

static int	sc_size;
static surfcache_t	*sc_rover;
surfcache_t	*sc_base;

/*
 * Color light apply is not required
 */
static int
R_GreyscaledLight(const light3_t light)
{
	light3_t light_masked;

	light_masked[0] = light[0] & LIGHTMASK;
	light_masked[1] = light[1] & LIGHTMASK;
	light_masked[2] = light[2] & LIGHTMASK;

	if (light_masked[0] == light_masked[1] && light_masked[0] == light_masked[2])
		return light_masked[0];

	return LIGHTMASK;
}

static void
R_DrawSurfaceBlock_Light (pixel_t *prowdest, const pixel_t *psource, size_t size,
						int level, const light3_t lightleft, const light3_t lightright,
						const pixel_t *colormap, const byte *c16to8)
{
	int light_masked_right, light_masked_left;

	light_masked_right = R_GreyscaledLight(lightright);
	if (light_masked_right != LIGHTMASK)
	{
		light_masked_left = R_GreyscaledLight(lightleft);
	}

	// Full same light from both side
	if (light_masked_right != LIGHTMASK && light_masked_left == light_masked_right)
	{
		size_t b;

		for (b = 0; b < size; b++)
		{
			prowdest[b] = colormap[psource[b] + light_masked_right];
		}

		return;
	}

	/* (Miyoo Mini port) the same grey light in all three components at both ends, to the last
	   bit: the steps are equal and the components stay equal along the row, so R_ApplyLight
	   below would give vid_colormap[pix + (light & LIGHTMASK)] for every texel: the same
	   pixels, without the per-texel work (as the original's surface block loop) */
	if (lightleft[0] == lightleft[1] && lightleft[0] == lightleft[2] &&
		lightright[0] == lightright[1] && lightright[0] == lightright[2])
	{
		const int lightstep = (lightleft[0] - lightright[0]) >> level;
		int light = lightright[0];
		int b;

		for (b = (int)(size - 1); b >= 0; b--)
		{
			prowdest[b] = colormap[psource[b] + (light & LIGHTMASK)];
			light += lightstep;
		}

		return;
	}

	// same color light shades
	{
		int b;
		const int lstep0 = ((int)lightleft[0] - (int)lightright[0]) >> level;
		const int lstep1 = ((int)lightleft[1] - (int)lightright[1]) >> level;
		const int lstep2 = ((int)lightleft[2] - (int)lightright[2]) >> level;
		int l0 = lightright[0];
		int l1 = lightright[1];
		int l2 = lightright[2];

		for (b = (int)(size - 1); b >= 0; b--)
		{
			pixel_t pix = psource[b];
			prowdest[b] = R_ApplyLightTab(pix, l0, l1, l2, colormap, c16to8);

			l0 += lstep0;
			l1 += lstep1;
			l2 += lstep2;
		}
	}
}


static __attribute__((noinline)) void
R_DrawSurfaceBlockRGB(pixel_t *prowdest, const pixel_t *psource, int blocksize, int blockdivshift,
	int sourcetstep, int surfrowbytes, const light_t *r_lightptr, const light_t *next_lightptr,
	const pixel_t *colormap, const byte *c16to8)
{
	light3_t lightleft, lightright;
	light3_t lightleftstep, lightrightstep;
	int i;

	memcpy(lightleft, r_lightptr, sizeof(light3_t));
	memcpy(lightright, r_lightptr + 3, sizeof(light3_t));
	for (i = 0; i < 3; i++)
	{
		lightleftstep[i] = (next_lightptr[i] - lightleft[i]) >> blockdivshift;
		lightrightstep[i] = (next_lightptr[i + 3] - lightright[i]) >> blockdivshift;
	}

	for (i = 0; i < blocksize; i++)
	{
		int j;

		R_DrawSurfaceBlock_Light(prowdest, psource, blocksize, blockdivshift,
			lightleft, lightright, colormap, c16to8);

		psource += sourcetstep;

		for (j = 0; j < 3; j++)
		{
			lightright[j] += lightrightstep[j];
			lightleft[j] += lightleftstep[j];
		}

		prowdest += surfrowbytes;
	}
}

/*
===============
R_DrawSurface
===============
*/
void
R_DrawSurface (drawsurf_t *drawsurf)
{
	unsigned char	*basetptr;
	int		smax, tmax, twidth;
	int		u;
	int		soffset, basetoffset, texwidth;
	int		blocksize;
	unsigned char	*pcolumndest;
	image_t		*mt;
	int		blockdivshift;
	int		r_numhblocks, r_numvblocks;
	int		r_lightwidth, sourcetstep, r_stepback;
	unsigned char	*r_source, *r_sourcemax;
	const int	 surfrowbytes = drawsurf->rowbytes;
	const pixel_t	* __restrict const colormap = vid_colormap;
	const light_t	* __restrict const blights = blocklights;
	const light_t	* const blight_max = blocklight_max;

	mt = drawsurf->image;

	r_source = mt->pixels[drawsurf->surfmip];

	// the fractional light values should range from 0 to (VID_GRADES - 1) << 16
	// from a source range of 0 - 255

	texwidth = mt->width >> drawsurf->surfmip;

	blocksize = 16 >> drawsurf->surfmip;
	blockdivshift = NUM_MIPS - drawsurf->surfmip;

	r_lightwidth = (drawsurf->surf->extents[0]>>4)+1;

	r_numhblocks = drawsurf->surfwidth >> blockdivshift;
	r_numvblocks = drawsurf->surfheight >> blockdivshift;

	//==============================

	smax = mt->width >> drawsurf->surfmip;
	twidth = texwidth;
	tmax = mt->height >> drawsurf->surfmip;
	sourcetstep = texwidth;
	r_stepback = tmax * twidth;

	r_sourcemax = r_source + (tmax * smax);

	soffset = drawsurf->surf->texturemins[0];
	basetoffset = drawsurf->surf->texturemins[1];

	// << 16 components are to guarantee positive values for %
	soffset = ((soffset >> drawsurf->surfmip) + (smax << SHIFT16XYZ)) % smax;
	basetptr = &r_source[((((basetoffset >> drawsurf->surfmip)
		+ (tmax << SHIFT16XYZ)) % tmax) * twidth)];

	pcolumndest = drawsurf->surfdat;

	for (u=0 ; u<r_numhblocks; u++)
	{
		const light_t * __restrict r_lightptr = blights + u * 3;
		pixel_t * __restrict prowdest;
		const pixel_t * __restrict psource;
		int v;

		if (r_lightptr >= blight_max)
		{
			r_outoflights = true;
			continue;
		}

		prowdest = pcolumndest;
		psource = basetptr + soffset;

		for (v = 0; v < r_numvblocks; v++)
		{
			const light_t * __restrict const next_lightptr = r_lightptr + r_lightwidth * 3;
			const light_t tl0 = r_lightptr[0], tr0 = r_lightptr[3];
			const light_t bl0 = next_lightptr[0], br0 = next_lightptr[3];

			if (tl0 == r_lightptr[1] && tl0 == r_lightptr[2] &&
			    tr0 == r_lightptr[4] && tr0 == r_lightptr[5] &&
			    bl0 == next_lightptr[1] && bl0 == next_lightptr[2] &&
			    br0 == next_lightptr[4] && br0 == next_lightptr[5])
			{
				int ll = (int)tl0;
				int lr = (int)tr0;
				const int llstep = ((int)bl0 - ll) >> blockdivshift;
				const int lrstep = ((int)br0 - lr) >> blockdivshift;
				int i;

				r_lightptr = next_lightptr;
				for (i = 0; i < blocksize; i++)
				{
					const int lmask = lr & LIGHTMASK;

					if ((ll & LIGHTMASK) == lmask)
					{
						int b;
						for (b = 0; b < blocksize; b++)
						{
							prowdest[b] = colormap[psource[b] + lmask];
						}
					}
					else
					{
						const int lightstep = (ll - lr) >> blockdivshift;
						int light = lr;
						int b;
						for (b = blocksize - 1; b >= 0; b--)
						{
							prowdest[b] = colormap[psource[b] + (light & LIGHTMASK)];
							light += lightstep;
						}
					}

					psource += sourcetstep;
					lr += lrstep;
					ll += llstep;
					prowdest += surfrowbytes;
				}
			}
			else
			{
				R_DrawSurfaceBlockRGB(prowdest, psource, blocksize, blockdivshift,
					sourcetstep, surfrowbytes, r_lightptr, next_lightptr,
					colormap, d_16to8table);
				r_lightptr = next_lightptr;
				psource += sourcetstep * blocksize;
				prowdest += surfrowbytes * blocksize;
			}

			if (psource >= r_sourcemax)
				psource -= r_stepback;
		}

		soffset = soffset + blocksize;
		if (soffset >= smax)
			soffset = 0;

		pcolumndest += blocksize;
	}
}


//=============================================================================

/*
================
R_InitCaches

================
*/
void
R_InitCaches (void)
{
	int		size;

	// calculate size to allocate
	int pix;
	// surface cache size at 320X240
	size = 1024*768;

	pix = vid_buffer_width*vid_buffer_height;
	if (pix > 64000)
		size += (pix-64000)*3;

	if (r_farsee->value > 0)
		size *= 2;

	if (sw_surfcacheoverride->value > size)
	{
		size = sw_surfcacheoverride->value;
	}

	// round up to page size
	size = (size + 8191) & ~8191;

	Com_DPrintf("%ik surface cache.\n", size / 1024);

	sc_size = size;
	sc_base = (surfcache_t *)malloc(size);
	if (!sc_base)
	{
		Com_Error(ERR_FATAL, "%s: Can't allocate cache.", __func__);
		// code never returns after ERR_FATAL
		return;
	}
	sc_rover = sc_base;

	sc_base->next = NULL;
	sc_base->owner = NULL;
	sc_base->size = sc_size;
	sc_base->mtpass = 0;
}


/*
==================
D_FlushCaches
==================
*/
void
D_FlushCaches (void)
{
	surfcache_t     *c;

	if (!sc_base)
		return;

	for (c = sc_base ; c ; c = c->next)
	{
		if (c->owner)
			*c->owner = NULL;
	}

	sc_rover = sc_base;
	sc_base->next = NULL;
	sc_base->owner = NULL;
	sc_base->size = sc_size;
	sc_base->mtpass = 0;
}

/*
=================
D_SCAlloc
=================
*/
static surfcache_t *
D_SCAlloc (int width, int size)
{
	surfcache_t	*new;

	if ((width < 0) || (width > 256))
	{
		Com_Error(ERR_FATAL, "%s: bad cache width %d\n", __func__, width);
	}

	if ((size <= 0) || (size > 0x10000))
	{
		Com_Error(ERR_FATAL, "%s: bad cache size %d\n", __func__, size);
	}

	// Add header size
	size += ((char*)sc_base->data - (char*)sc_base);
	size = (size + 3) & ~3;
	if (size > sc_size)
	{
		Com_Error(ERR_FATAL, "%s: %i > cache size of %i", __func__, size, sc_size);
	}

	// if there is not size bytes after the rover, reset to the start
	if ( !sc_rover || (byte *)sc_rover - (byte *)sc_base > sc_size - size)
	{
		sc_rover = sc_base;
	}

	// colect and free surfcache_t blocks until the rover block is large enough
	new = sc_rover;
	SW_MT_CacheOverwrite(sc_rover);	// (sw_mt.c: draws the records that use it first)
	if (sc_rover->owner)
		*sc_rover->owner = NULL;

	while (new->size < size)
	{
		// free another
		sc_rover = sc_rover->next;
		if (!sc_rover)
		{
			Com_Error(ERR_FATAL, "%s: hit the end of memory", __func__);
		}
		SW_MT_CacheOverwrite(sc_rover);
		if (sc_rover->owner)
			*sc_rover->owner = NULL;

		new->size += sc_rover->size;
		new->next = sc_rover->next;
	}

	// create a fragment out of any leftovers
	if (new->size - size > 256)
	{
		sc_rover = (surfcache_t *)( (byte *)new + size);
		sc_rover->size = new->size - size;
		sc_rover->next = new->next;
		sc_rover->width = 0;
		sc_rover->owner = NULL;
		sc_rover->mtpass = 0;
		new->next = sc_rover;
		new->size = size;
	}
	else
		sc_rover = new->next;

	new->width = width;
	// DEBUG
	if (width > 0)
		new->height = (size - sizeof(*new) + sizeof(new->data)) / width;

	new->owner = NULL; // should be set properly after return

	return new;
}

//=============================================================================

static drawsurf_t	r_drawsurf;

/*
================
D_CacheSurface
================
*/
surfcache_t *
D_CacheSurface (const entity_t *currententity, msurface_t *surface, int miplevel)
{
	surfcache_t	*cache;
	float		surfscale;

	//
	// if the surface is animating or flashing, flush the cache
	//
	r_drawsurf.image = R_TextureAnimation (currententity, surface->texinfo);
	r_drawsurf.lightadj[0] = r_newrefdef.lightstyles[surface->styles[0]].white*128;
	r_drawsurf.lightadj[1] = r_newrefdef.lightstyles[surface->styles[1]].white*128;
	r_drawsurf.lightadj[2] = r_newrefdef.lightstyles[surface->styles[2]].white*128;
	r_drawsurf.lightadj[3] = r_newrefdef.lightstyles[surface->styles[3]].white*128;

	//
	// see if the cache holds apropriate data
	//
	cache = surface->cachespots[miplevel];

	if (cache && !cache->dlight && surface->dlightframe != r_framecount
			&& cache->image == r_drawsurf.image
			&& cache->lightadj[0] == r_drawsurf.lightadj[0]
			&& cache->lightadj[1] == r_drawsurf.lightadj[1]
			&& cache->lightadj[2] == r_drawsurf.lightadj[2]
			&& cache->lightadj[3] == r_drawsurf.lightadj[3] )
		return cache;

	//
	// determine shape of surface
	//
	surfscale = 1.0 / (1<<miplevel);
	r_drawsurf.surfmip = miplevel;
	r_drawsurf.surfwidth = surface->extents[0] >> miplevel;
	r_drawsurf.rowbytes = r_drawsurf.surfwidth;
	r_drawsurf.surfheight = surface->extents[1] >> miplevel;

	//
	// allocate memory if needed
	//
	if (cache) // rebuilt in place: draw the records that use it first (sw_mt.c)
	{
		SW_MT_CacheOverwrite(cache);
	}
	else // if a texture just animated, don't reallocate it
	{
		cache = D_SCAlloc (r_drawsurf.surfwidth,
						   r_drawsurf.surfwidth * r_drawsurf.surfheight);
		surface->cachespots[miplevel] = cache;
		cache->owner = &surface->cachespots[miplevel];
		cache->mipscale = surfscale;
	}

	if (surface->dlightframe == r_framecount)
		cache->dlight = 1;
	else
		cache->dlight = 0;

	r_drawsurf.surfdat = (pixel_t *)cache->data;

	cache->image = r_drawsurf.image;
	cache->lightadj[0] = r_drawsurf.lightadj[0];
	cache->lightadj[1] = r_drawsurf.lightadj[1];
	cache->lightadj[2] = r_drawsurf.lightadj[2];
	cache->lightadj[3] = r_drawsurf.lightadj[3];

	//
	// draw and light the surface texture
	//
	r_drawsurf.surf = surface;

	c_surf++;

	// Miyoo Mini: while D_DrawSurfaces records, built later on both threads (sw_mt.c)
	if (SW_MT_DeferBuild(&r_drawsurf))
	{
		return cache;
	}

	// calculate the lightings
	R_BuildLightMap (&r_drawsurf);

	// rasterize the surface into the cache
	R_DrawSurface (&r_drawsurf);

	return cache;
}
