/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - a page laid out and drawn on the character grid of the text
 * font (see pagedraw.h).  Moved here from ehypview.c so that a pop-up
 * draws its page the same way as the viewer window.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gem.h"
#include "colour.h"
#include "gemapp.h"
#include "pagedraw.h"
#include "stmem.h"

/* ---------------------------------------------------------------- */
/* the screen's colours                                              */

/*
 * What the screen can show: its pens (up to 8 planes) or, in high and
 * true colour, the VDI's 256 colours, each with its RGB and the VDI
 * colour that draws it.  In true colour each also has its pixel as the
 * screen keeps it: worked out from vq_scrninfo where the VDI has it
 * (NVDI), else read back from the screen once (TOS 4, EmuTOS).
 */
static struct {
	unsigned short n;              /* targets */
	crgb rgb[256];
	short vdi[256];
	unsigned long dev[256];        /* true colour: the pixel */
	short bytes;                   /* true colour: bytes per pixel; 0 else */
	int info;                      /* vq_scrninfo answered */
	int probed;                    /* dev[] read from the screen */
	int packs;                     /* bits known: any colour can be drawn */
	cbits bits;
	short text[2][256];            /* VDI colour for text colour n, fg and bg; -1 unknown */
} scr;

static short scrinfo[272];

static unsigned char from1000(short v)
{
	return (unsigned char) (v < 0 ? 0 : v > 1000 ? 255 : (v * 255L + 500) / 1000);
}

/* true colour without vq_scrninfo: each VDI colour drawn on one pixel of
   the screen and read back, the pixel put back as it was (once) */
static void probe(void)
{
	static const short none[4] = { 0, 0, 0, 0 };
	short pxy[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	MFDB screen, keep, got;
	unsigned char *mem = st_alloc(4L * planes);
	unsigned short i;

	scr.probed = 1;
	if (mem == NULL)
		return;
	memset(&screen, 0, sizeof(screen));
	memset(&keep, 0, sizeof(keep));
	keep.fd_addr = mem;
	keep.fd_w = 16;
	keep.fd_h = 1;
	keep.fd_wdwidth = 1;
	keep.fd_nplanes = planes;
	got = keep;
	got.fd_addr = mem + 2L * planes;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, NULL);
	vs_clip(vdi_h, 0, none);
	vro_cpyfm(vdi_h, 3, pxy, &screen, &keep);
	for (i = 0; i < 256; i++)
	{
		const unsigned char *p = mem + 2L * planes;
		unsigned long px = 0;
		short b;

		fill(i, 0, 0, 0, 0);
		vro_cpyfm(vdi_h, 3, pxy, &screen, &got);
		for (b = 0; b < scr.bytes; b++)
			px = px << 8 | p[b];
		scr.dev[i] = px;
	}
	vro_cpyfm(vdi_h, 3, pxy, &keep, &screen);
	graf_mouse(M_ON, NULL);
	wind_update(END_UPDATE);
	st_free(mem);
}

/* read the screen's colours again (a program may have set others) */
static void screen_colours(void)
{
	static int asked;
	unsigned short i;
	short rgb[3];

	if (!asked)
	{
		asked = 1;
		scr.info = vq_scrninfo(vdi_h, scrinfo);
		if (scr.info && planes > 8)
			scr.packs = colour_bits_info(scrinfo, &scr.bits);	/* else read back */
	}
	if (planes > 8)
	{
		if (scr.n == 256)
			goto text;                         /* true colour: the same for good */
		scr.n = 256;
		scr.bytes = (short) ((planes + 7) / 8);
		for (i = 0; i < 256; i++)
		{
			vq_color(vdi_h, (short) i, rgb);
			scr.rgb[i].r = from1000(rgb[0]);
			scr.rgb[i].g = from1000(rgb[1]);
			scr.rgb[i].b = from1000(rgb[2]);
			scr.vdi[i] = (short) i;
		}
		if (scr.packs)
			for (i = 0; i < 256; i++)
				scr.dev[i] = colour_pack(&scr.bits, &scr.rgb[i]);
		else if (!scr.probed)
		{
			/* VDI red, green, blue, white, black */
			unsigned long rgbwk[5];

			probe();
			rgbwk[0] = scr.dev[2];
			rgbwk[1] = scr.dev[3];
			rgbwk[2] = scr.dev[4];
			rgbwk[3] = scr.dev[0];
			rgbwk[4] = scr.dev[1];
			scr.packs = colour_bits_read(rgbwk, &scr.bits);
		}
	} else
	{
		unsigned short ncol = (unsigned short) (1 << planes);

		scr.n = ncol;
		for (i = 0; i < ncol; i++)
			scr.vdi[i] = -1;
		/* each pen's VDI colour: the first that draws with it */
		for (i = (unsigned short) (ncol == 256 ? 256 : 16); i-- > 0;)
		{
			unsigned short pen = scr.info ? (unsigned short) (scrinfo[16 + i] & (ncol - 1))
				: colour_pen_of_vdi(ncol, i);

			scr.vdi[pen] = (short) i;
		}
		for (i = 0; i < ncol; i++)
		{
			if (scr.vdi[i] < 0)
				scr.vdi[i] = (short) i;
			vq_color(vdi_h, scr.vdi[i], rgb);
			scr.rgb[i].r = from1000(rgb[0]);
			scr.rgb[i].g = from1000(rgb[1]);
			scr.rgb[i].b = from1000(rgb[2]);
		}
	}
text:
	for (i = 0; i < 256; i++)
		scr.text[0][i] = scr.text[1][i] = -1;
}

/* the VDI colour that draws text colour n (bg: behind the text; -1 for
   none, the page's white) */
static short text_colour(unsigned short n, int bg)
{
	short *v = &scr.text[bg ? 1 : 0][n & 255];

	if (*v == -1)
	{
		crgb c;
		int i;

		colour_vdi_std(n, &c);
		i = colour_text(scr.rgb, scr.n, &c, bg);
		*v = (short) (i < 0 ? -2 : scr.vdi[i]);
	}
	return *v == -2 ? -1 : *v;
}

/* ---------------------------------------------------------------- */
/* pictures                                                          */

/* a plane's byte spread out: its 8 pixels' bits, 4 bits apart, the first
   pixel at the top (bits 28-31) */
static unsigned long spread[256];

static void make_spread(void)
{
	unsigned short b, k;

	for (b = 0; b < 256; b++)
		for (spread[b] = 0, k = 0; k < 8; k++)
			if (b & (0x80 >> k))
				spread[b] |= 1UL << (28 - 4 * k);
}

/* 8 pixel values, 4 bits each, of planes first.. (at most 4) of the bytes
   at off */
static inline unsigned long nibbles(unsigned char *const *pl, unsigned short first, unsigned short np,
                             unsigned long off)
{
	unsigned char *const *p = pl + first;
	unsigned long v = 0;

	switch (np - first)
	{
	default: v = spread[p[3][off]] << 3;   /* fall through */
	case 3: v |= spread[p[2][off]] << 2;   /* fall through */
	case 2: v |= spread[p[1][off]] << 1;   /* fall through */
	case 1: v |= spread[p[0][off]];
	}
	return v;
}

/* the pixel values of nbytes * 8 pixels of the rows at row and next
   (bytes into each plane pl[i]); pairs: both, as one key each.  Eight
   pixels at a time through spread[], two to a byte of the result (the
   Atari is big-endian): a 68000 tests bits, and shifts far, slowly */
static void row_keys(unsigned char *const *pl, unsigned short np, unsigned long row, unsigned long next,
                     unsigned short nbytes, int pairs, unsigned char *keys)
{
	union {
		unsigned long l;
		unsigned char b[4];
	} lo, hi;
	unsigned short bx, k;

	for (bx = 0; bx < nbytes; bx++)
	{
		lo.l = nibbles(pl, 0, np, row + bx);
		if (pairs)
			hi.l = nibbles(pl, 0, np, next + bx);  /* the row below */
		else if (np > 4)
			hi.l = nibbles(pl, 4, np, row + bx);   /* planes 4-7 */
		else
		{
			for (k = 0; k < 4; k++)
			{
				*keys++ = (unsigned char) (lo.b[k] >> 4);
				*keys++ = (unsigned char) (lo.b[k] & 15);
			}
			continue;
		}
		for (k = 0; k < 4; k++)
		{
			unsigned char a = lo.b[k], b = hi.b[k];

			if (pairs)
			{
				*keys++ = (unsigned char) ((a & 0xF0) | b >> 4);
				*keys++ = (unsigned char) ((a & 15) << 4 | (b & 15));
			} else
			{
				*keys++ = (unsigned char) ((b & 0xF0) | a >> 4);
				*keys++ = (unsigned char) ((b & 15) << 4 | (a & 15));
			}
		}
	}
}

/*
 * Convert a HYP image (planar, standard format) for the screen.  Each
 * pixel value is a standard colour (colour.h), drawn with the screen's
 * nearest: dithered between two pens with 4 colours, black or white on
 * mono (through the hcp source's dither mask when it gave one, else by
 * brightness).  Where pixels are twice as tall as wide (ST medium), two
 * rows make one, so that pictures keep their shape.
 */
static void load_image(hyp_doc *doc, const hyp_gfx *g, gimage *im)
{
	unsigned char *data;
	unsigned long size, plane, wdw, p, oplane;
	unsigned short w, h, oh, np, present, onoff, keys, k;
	unsigned char *std, *out;
	int i, halve, same, pairs;
	cpick *pick;
	unsigned long *dev = NULL;
	unsigned char *pl[8], *kbuf, (*pens)[256] = NULL;
	MFDB src;

	memset(im, 0, sizeof(*im));
	if (hyp_load(doc, g->image, &data, &size) != HYP_OK)
		return;
	if (size < 8)
	{
		free(data);
		return;
	}
	w = (unsigned short) ((data[0] << 8) | data[1]);
	h = (unsigned short) ((data[2] << 8) | data[3]);
	np = data[4];
	present = data[5];
	onoff = data[6];
	wdw = (w + 15UL) / 16;
	plane = wdw * 2 * h;
	if (w == 0 || h == 0 || np == 0 || np > 8)
	{
		free(data);
		return;
	}

	/* all planes, filling in the ones that are not stored */
	std = malloc(plane * np);
	if (std == NULL)
	{
		free(data);
		return;
	}
	p = 8;
	for (i = 0; i < np; i++)
	{
		unsigned char *dst = std + plane * (unsigned long) i;

		if (present & (1 << i))
		{
			if (p + plane > size)
			{
				free(std);
				free(data);
				return;
			}
			memcpy(dst, data + p, plane);
			p += plane;
		} else
		{
			memset(dst, (onoff & (1 << i)) ? 0xff : 0x00, plane);
		}
	}
	free(data);

	if (spread[0x80] == 0)
		make_spread();
	for (i = 0; i < np; i++)
		pl[i] = std + plane * (unsigned long) i;
	kbuf = malloc(wdw * 16);                 /* a row's pixel values */

	/* how each pixel value (or, two rows made one, each pair of them up
	   to 16 colours; above, the upper row's) is drawn */
	halve = tall_pixels && h > 1;
	oh = (unsigned short) (halve ? (h + 1) / 2 : h);
	keys = (unsigned short) (halve && np <= 4 ? 256 : 1 << np);	/* pairs: 16 x 16 */
	pick = malloc(keys * sizeof(cpick));
	if (scr.bytes)
		dev = malloc(keys * sizeof(unsigned long));
	if (pick == NULL || (scr.bytes && dev == NULL))
	{
		free(kbuf);
		free(pick);
		free(dev);
		free(std);
		return;
	}
	same = !halve && planes <= 8 && np <= planes;
	pairs = halve && np <= 4;
	for (k = 0; k < keys; k++)
	{
		unsigned short p1 = pairs ? (unsigned short) (k >> 4) : k;
		unsigned short p2 = pairs ? (unsigned short) (k & 15) : k;
		crgb c1, c2, c;

		colour_image_pen(np, p1, &c1);
		colour_image_pen(np, p2, &c2);
		colour_mix(&c1, &c2, &c);
		if (planes == 1)
		{
			/* black: the dither mask's bit (pens 0-15), or dark */
			int b1 = g->has_dither ? (g->dithermask >> (p1 & 15)) & 1 : colour_dark(&c1);
			int b2 = g->has_dither ? (g->dithermask >> (p2 & 15)) & 1 : colour_dark(&c2);

			pick[k].a = (unsigned char) (b1 && b2);
			pick[k].b = 1;
			pick[k].t = (unsigned char) (b1 != b2 ? 8 : 0);
		} else if (scr.bytes && scr.packs)
		{
			dev[k] = colour_pack(&scr.bits, &c);
			continue;
		} else
		{
			colour_pick(scr.rgb, scr.n, &c, scr.n < 16, &pick[k]);
			if (scr.bytes)
				dev[k] = scr.dev[pick[k].a];
		}
		if (pick[k].t != 0 || pick[k].a != k)
			same = 0;
	}

	im->w = (short) w;
	im->h = (short) oh;
	im->planes = planes;
	im->mono = planes == 1;
	src.fd_w = (short) (wdw * 16);
	src.fd_h = (short) oh;
	src.fd_wdwidth = (short) wdw;
	src.fd_stand = 1;
	src.fd_nplanes = planes;
	src.fd_r1 = src.fd_r2 = src.fd_r3 = 0;
	im->form = src;
	im->form.fd_stand = 0;
	oplane = wdw * 2 * oh;

	if (scr.bytes)
	{
		/* high and true colour: the screen's own pixels, packed */
		unsigned char *o = st_alloc((long) (wdw * 16 * oh * scr.bytes));
		unsigned short x, y;

		im->form.fd_addr = o;
		if (o != NULL && kbuf != NULL)
			for (y = 0; y < oh; y++)
			{
				unsigned long row = (unsigned long) (halve ? 2 * y : y) * wdw * 2;
				unsigned long next = halve && 2 * y + 1 < h ? row + wdw * 2 : row;

				row_keys(pl, np, row, next, (unsigned short) (wdw * 2), pairs, kbuf);
				for (x = 0; x < wdw * 16; x++)
				{
					unsigned long px = x < w ? dev[kbuf[x]] : 0;
					short b;

					for (b = scr.bytes; b-- > 0;)
						*o++ = (unsigned char) (px >> (8 * b));
				}
			}
		else if (o != NULL)
		{
			st_free(o);
			im->form.fd_addr = NULL;
		}
		free(kbuf);
		free(dev);
		free(pick);
		free(std);
		if (im->form.fd_addr == NULL)
			im->w = 0;
		return;
	}

	if (same)
	{
		/* the screen's pens are the image's: its planes as they are */
		free(kbuf);
		out = realloc(std, plane * planes);
		if (out == NULL)
		{
			free(pick);
			free(std);
			return;
		}
		memset(out + plane * np, 0, plane * (unsigned long) (planes - np));
	} else
	{
		unsigned short x, y;
		unsigned char *opl[8];

		out = calloc(1, oplane * planes);
		if (out == NULL || kbuf == NULL)
		{
			free(out);
			free(kbuf);
			free(pick);
			free(std);
			return;
		}
		for (i = 0; i < planes; i++)
			opl[i] = out + oplane * (unsigned long) i;
		/* each key's pen at each place in the Bayer matrix (a table, as
		   the rest: an ST does each pixel in a few instructions) */
		pens = planes <= 4 ? malloc(16 * 256) : NULL;
		if (pens != NULL)
			for (i = 0; i < 16; i++)
				for (k = 0; k < keys; k++)
					pens[i][k] = pick[k].t > colour_bayer[i >> 2][i & 3] ? pick[k].b : pick[k].a;
		for (y = 0; y < oh; y++)
		{
			unsigned long row = (unsigned long) (halve ? 2 * y : y) * wdw * 2;
			unsigned long next = halve && 2 * y + 1 < h ? row + wdw * 2 : row;
			unsigned long at = (unsigned long) y * wdw * 2;
			unsigned short bx;

			row_keys(pl, np, row, next, (unsigned short) (wdw * 2), pairs, kbuf);
			if (pens != NULL)
			{
				/* up to 4 planes: 4 pixels' pens in a word, then each
				   plane's 4 bits gathered from it */
				const unsigned char *t0 = pens[(y & 3) * 4], *t1 = t0 + 256, *t2 = t1 + 256, *t3 = t2 + 256;
				const unsigned char *kb = kbuf;

				for (bx = 0; bx < wdw * 2; bx++, kb += 8)
				{
					unsigned short hi = (unsigned short) (t0[kb[0]] << 12 | t1[kb[1]] << 8 | t2[kb[2]] << 4 | t3[kb[3]]);
					unsigned short lo = (unsigned short) (t0[kb[4]] << 12 | t1[kb[5]] << 8 | t2[kb[6]] << 4 | t3[kb[7]]);

					for (i = 0; i < planes; i++, hi >>= 1, lo >>= 1)
					{
						unsigned short a = hi & 0x1111, b = lo & 0x1111;

						a = (unsigned short) ((a | a >> 3 | a >> 6 | a >> 9) & 15);
						b = (unsigned short) ((b | b >> 3 | b >> 6 | b >> 9) & 15);
						opl[i][at + bx] = (unsigned char) (a << 4 | b);
					}
				}
				continue;
			}
			for (bx = 0, x = 0; bx < wdw * 2; bx++)
			{
				unsigned char ob[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }, m;

				for (m = 0x80; m != 0 && x < w; m >>= 1, x++)
				{
					unsigned short pen = COLOUR_AT(&pick[kbuf[x]], x, y);

					for (i = 0; i < planes; i++)
						if (pen & (1 << i))
							ob[i] |= m;
				}
				for (i = 0; i < planes; i++)
					opl[i][at + bx] = ob[i];
			}
		}
		free(pens);
		free(kbuf);
		free(std);
	}
	free(pick);

	src.fd_addr = out;
	im->form.fd_addr = st_alloc((long) (oplane * planes));   /* copied to the screen: ST RAM */
	if (im->form.fd_addr == NULL)
	{
		free(out);
		im->w = 0;
		return;
	}
	vr_trnfm(vdi_h, &src, &im->form);
	free(out);
}

void layout_free(playout *L)
{
	unsigned short i;

	for (i = 0; L->images != NULL && i < L->nimages; i++)
		st_free(L->images[i].form.fd_addr);
	free(L->images);
	free(L->line_y);
	free(L->gfx_y);
	free(L->gfx_at);
	memset(L, 0, sizeof(*L));
}

void layout_make(playout *L, hyp_doc *doc, const hyp_page *pg)
{
	unsigned short i;
	long y = 0;
	long l, n;

	layout_free(L);
	if (pg == NULL)
		return;
	n = pg->nlines + 1;
	L->line_y = malloc((size_t) n * sizeof(long));
	L->gfx_y = malloc((size_t) n * sizeof(long));
	L->images = calloc(pg->ngfx ? pg->ngfx : 1, sizeof(gimage));
	L->gfx_at = malloc((pg->ngfx ? pg->ngfx : 1) * sizeof(long));
	if (L->line_y == NULL || L->gfx_y == NULL || L->images == NULL || L->gfx_at == NULL)
	{
		layout_free(L);
		return;
	}
	L->pg = pg;
	L->nimages = pg->ngfx;
	screen_colours();
	{
		unsigned short shown = 0, in_st = 0;

		for (i = 0; i < pg->ngfx; i++)
			if (pg->gfx[i].type == HYP_GFX_IMAGE)
			{
				load_image(doc, &pg->gfx[i], &L->images[i]);
				if (L->images[i].form.fd_addr != NULL)
				{
					shown++;
					in_st = (unsigned short) (in_st + st_ram_addr(L->images[i].form.fd_addr));
				}
			}
		if (shown > 0)
		{
			/* for the tests: where the pictures are (a blitter reads
			   only ST RAM) */
			char num[8];

			sprintf(num, "%u", shown);
			log_event("pictures", num, in_st == shown ? "st-ram" : "fast-ram");
		}
	}

	/* an @limage pushes the text of its line down (as ST-Guide does);
	   several on one line, one below the other (as HypView does) */
	for (l = 0; l < n; l++)
	{
		L->gfx_y[l] = y;
		for (i = 0; i < pg->ngfx; i++)
		{
			const hyp_gfx *g = &pg->gfx[i];

			if (g->type == HYP_GFX_IMAGE && g->limage && (g->y == l || (l == n - 1 && g->y >= l)) &&
				L->images[i].h > 0)
			{
				L->gfx_at[i] = y;
				y += ((L->images[i].h + ch - 1) / ch) * ch;
				if (L->images[i].h % ch == 0)
					y += ch;
			}
		}
		L->line_y[l] = y;
		y += ch;
	}
	L->height = L->line_y[n - 1];
	for (i = 0; i < pg->ngfx; i++)
		if (!(pg->gfx[i].type == HYP_GFX_IMAGE && pg->gfx[i].limage && L->images[i].h > 0))
			L->gfx_at[i] = L->gfx_y[pg->gfx[i].y < pg->nlines ? pg->gfx[i].y : pg->nlines];
}

/* draw one graphic; oy is the page origin, top/bottom the redrawn rows */
static void draw_gfx(const playout *L, const hyp_gfx *g, const gimage *im, short ox, long oy,
                     long top, long bottom, long cols)
{
	short pxy[8];
	long x = ox + (long) (g->x > 0 ? g->x - 1 : 0) * cw;
	long y = oy + L->gfx_at[g - L->pg->gfx];
	long h = g->type == HYP_GFX_IMAGE ? (im ? im->h : 0) : (long) g->h * ch;

	/* pages can be taller than screen coordinates reach: skip what is
	   not in the redrawn rows before converting to short (heights are
	   never negative; only a line's width can be) */
	if (y > bottom || y + h < top)
		return;

	vswr_mode(vdi_h, MD_TRANS);
	vsl_color(vdi_h, 1);
	vsf_color(vdi_h, 1);
	switch (g->type)
	{
	case HYP_GFX_IMAGE:
		if (im == NULL || im->w <= 0)
			return;
		if (g->x == 0)
		{
			/* centred on the page */
			long tx = (cols * cw - im->w) / 2;

			x = ox + (tx > 0 ? tx : 0);
		}
		{
			MFDB screen;

			memset(&screen, 0, sizeof(screen));
			pxy[0] = 0;
			pxy[1] = 0;
			pxy[2] = (short) (im->w - 1);
			pxy[3] = (short) (im->h - 1);
			pxy[4] = (short) x;
			pxy[5] = (short) y;
			pxy[6] = (short) (x + im->w - 1);
			pxy[7] = (short) (y + im->h - 1);
			{
				/* for the tests: where it is (tests/run_colour.py) */
				char at[24], size[16];

				sprintf(at, "%ld,%ld", x, y);
				sprintf(size, "%dx%d", im->w, im->h);
				log_event("picture", at, size);
			}
			if (im->mono)
			{
				short colors[2];

				colors[0] = 1;
				colors[1] = 0;
				vrt_cpyfm(vdi_h, MD_REPLACE, pxy, (MFDB *) &im->form, &screen, colors);
			} else
			{
				vro_cpyfm(vdi_h, 3, pxy, (MFDB *) &im->form, &screen);
			}
		}
		break;
	case HYP_GFX_LINE:
		{
			long w = (long) g->w * cw, h = (long) g->h * ch;

			pxy[0] = (short) x;
			pxy[1] = (short) y;
			pxy[2] = (short) (x + w + (w < 0 ? 1 : w > 0 ? -1 : 0));
			pxy[3] = (short) (y + h - (h ? 1 : 0));
			vsl_type(vdi_h, g->style);
			vsl_ends(vdi_h, (g->arrows & HYP_ARROW_START) ? 1 : 0, (g->arrows & HYP_ARROW_END) ? 1 : 0);
			v_pline(vdi_h, 2, pxy);
			vsl_ends(vdi_h, 0, 0);
			vsl_type(vdi_h, 1);
		}
		break;
	case HYP_GFX_BOX:
	case HYP_GFX_RBOX:
		pxy[0] = (short) x;
		pxy[1] = (short) y;
		pxy[2] = (short) (x + (long) g->w * cw - 1);
		pxy[3] = (short) (y + (long) g->h * ch - 1);
		if (g->pattern == 0)
			vsf_interior(vdi_h, FIS_HOLLOW);
		else if (g->pattern > 36)
			vsf_interior(vdi_h, FIS_SOLID);
		else if (g->pattern > 24)
		{
			vsf_interior(vdi_h, FIS_HATCH);
			vsf_style(vdi_h, (short) (g->pattern - 24));
		} else
		{
			vsf_interior(vdi_h, FIS_PATTERN);
			vsf_style(vdi_h, g->pattern);
		}
		vsf_perimeter(vdi_h, 1);
		if (g->type == HYP_GFX_BOX)
			v_bar(vdi_h, pxy);
		else if (g->pattern != 0)
			v_rfbox(vdi_h, pxy);
		else
			v_rbox(vdi_h, pxy);
		vsf_perimeter(vdi_h, 0);
		break;
	}
}

void layout_draw(const playout *L, short ox, long oy, long top,
                 short cx, short cy, short cw_, short ch_, long sel, long cols)
{
	const hyp_page *pg = L->pg;
	long l;

	if (pg == NULL)
		return;
	/* graphics first, text on top (all drawn transparently) */
	{
		unsigned short i;

		for (i = 0; i < pg->ngfx; i++)
			draw_gfx(L, &pg->gfx[i], &L->images[i], ox, oy, cy, (long) cy + ch_, cols);
	}

	vswr_mode(vdi_h, MD_TRANS);
	for (l = top; l < pg->nlines; l++)
	{
		const hyp_line *ln = &pg->lines[l];
		long y = oy + L->line_y[l];
		long x = ox;
		unsigned short r;

		if (y >= cy + ch_)
			break;
		if (y + ch <= cy)
			continue;
		for (r = 0; r < ln->nruns; r++)
		{
			const hyp_run *run = &pg->runs[ln->first_run + r];
			long w = (long) run->len * cw;
			short fx = run->attr & 0x3f;

			if (x + w > cx && x < cx + cw_)
			{
				if ((long) (ln->first_run + r) == sel)
				{
					/* the selected link: reverse video */
					fill(link_color(), (short) x, (short) y, (short) (x + w - 1), (short) (y + ch - 1));
					vswr_mode(vdi_h, MD_TRANS);
					vst_color(vdi_h, 0);
				} else if (run->kind == HYP_RUN_LINK)
				{
					vst_color(vdi_h, link_color());
					fx |= 8 | (ncolors >= 16 ? 0 : 1);   /* underlined (and bold in mono) */
				} else
				{
					/* hcp's @{fg n} and @{bg n} */
					short fg = run->fg != HYP_COLOR_DEFAULT ? text_colour(run->fg, 0) : 1;
					short bg = run->bg != HYP_COLOR_DEFAULT ? text_colour(run->bg, 1) : -1;

					if (bg >= 0)
					{
						fill(bg, (short) x, (short) y, (short) (x + w - 1), (short) (y + ch - 1));
						vswr_mode(vdi_h, MD_TRANS);
						if (planes == 1 && fg == 1)
							fg = 0;            /* black on black: white */
					}
					vst_color(vdi_h, fg);
				}
				vst_effects(vdi_h, fx);
				text_at((short) x, (short) y, run->text, run->len);
			}
			x += w;
		}
	}
	vst_effects(vdi_h, 0);
	vst_color(vdi_h, 1);
}

long layout_line_at(const playout *L, long py, long top)
{
	long line;

	if (L->pg == NULL || L->line_y == NULL)
		return -1;
	for (line = top < 0 ? 0 : top; line < L->pg->nlines; line++)
		if (py >= L->line_y[line] && py < L->line_y[line] + ch)
			return line;
	return -1;
}

long layout_width(const playout *L, long cols)
{
	long w = cols * cw;
	unsigned short i;

	for (i = 0; L->pg != NULL && i < L->pg->ngfx; i++)
	{
		const hyp_gfx *g = &L->pg->gfx[i];

		if (g->type == HYP_GFX_IMAGE && L->images[i].w > 0)
		{
			long right = (g->x > 0 ? (long) (g->x - 1) * cw : 0) + L->images[i].w;

			if (right > w)
				w = right;
		}
	}
	return w;
}

long layout_height(const playout *L)
{
	long h = L->height;
	unsigned short i;

	for (i = 0; L->pg != NULL && i < L->pg->ngfx; i++)
	{
		const hyp_gfx *g = &L->pg->gfx[i];
		long y = L->gfx_at[i];
		long bottom = y + (g->type == HYP_GFX_IMAGE ? L->images[i].h : (long) (g->h > 0 ? g->h : 0) * ch);

		if (bottom > h)
			h = bottom;
	}
	return h;
}
