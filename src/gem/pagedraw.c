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
#include "hypfile.h"
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
static short ui_pen[16] = { -1 };      /* ui_colour(): -1 not read */

static unsigned char from1000(short v)
{
	return (unsigned char) (v < 0 ? 0 : v > 1000 ? 255 : (v * 255L + 500) / 1000);
}

/* true colour without vq_scrninfo: each VDI colour drawn on one pixel of
   the screen and read back, the pixel put back as it was (once) */
static void probe(void)
{
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
	text_noclip(vdi_h);
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
		short raw[256][3], top = 0, k;

		for (i = 0; i < ncol; i++)
		{
			if (scr.vdi[i] < 0)
				scr.vdi[i] = (short) i;
			vq_color(vdi_h, scr.vdi[i], raw[i]);
			for (k = 0; k < 3; k++)
				if (raw[i][k] > top)
					top = raw[i][k];
		}
		/* TOS 4.04 gives the Falcon's ST-compatible palette (4 colours,
		   ST medium) as level * 1000 / 16, 937 at the most, where the
		   screen shows 15 of 15: full (colour-test, falcon-4).  Such a
		   palette is read as level * 1000 / 15 */
		for (i = 0; i < ncol; i++)
			for (k = 0; k < 3; k++)
				if (top >= 900 && top < 1000)
					raw[i][k] = (short) ((raw[i][k] * 1000L + top / 2) / top);
		for (i = 0; i < ncol; i++)
		{
			scr.rgb[i].r = from1000(raw[i][0]);
			scr.rgb[i].g = from1000(raw[i][1]);
			scr.rgb[i].b = from1000(raw[i][2]);
		}
		if (ncol <= 16)
		{
			/* for the tests: each pen's colour, as the VDI says it is */
			char a[16 * 7 + 1], *o = a;

			for (i = 0; i < ncol; i++)
				o += sprintf(o, "%02X%02X%02X ", scr.rgb[i].r, scr.rgb[i].g, scr.rgb[i].b);
			o[o > a ? -1 : 0] = '\0';
			log_event("pens", a, NULL);
		}
	}
text:
	for (i = 0; i < 256; i++)
		scr.text[0][i] = scr.text[1][i] = -1;
	ui_pen[0] = -1;
}

/* the VDI colour that draws text colour n (bg: behind the text; -1 for
   none, the page's white): TOS's colour n matched to what VDI colours
   0-15 are now, never 16 and up, which may hold anything whatever the
   VDI's table says (guidelines 5.2) */
static short text_colour(unsigned short n, int bg)
{
	short *v = &scr.text[bg ? 1 : 0][n & 255];

	if (*v == -1)
	{
		static crgb pal[256];
		static unsigned char pen[256];
		crgb c;
		unsigned short i, m = 0;
		int k;

		for (i = 0; i < scr.n; i++)
			if (scr.vdi[i] >= 0 && scr.vdi[i] < 16)
			{
				pal[m].r = scr.rgb[i].r;       /* not pal[m] = ...: a 68000 */
				pal[m].g = scr.rgb[i].g;       /* faults on its word moves at */
				pal[m].b = scr.rgb[i].b;       /* an odd address */
				pen[m++] = (unsigned char) i;
			}
		colour_vdi_std(n, &c);
		k = colour_text(pal, m, &c, bg);
		*v = (short) (k < 0 ? -2 : scr.vdi[pen[k]]);
	}
	return *v == -2 ? -1 : *v;
}

/* the same for the windows' own greys and accents, from VDI colours
   0-15 alone (read once, and again with the page's colours) */
short ui_colour(unsigned short n)
{
	if (ui_pen[0] < 0)
	{
		crgb pal[16], c;
		short rgb[3], i;

		for (i = 0; i < 16; i++)
		{
			vq_color(vdi_h, i, rgb);
			pal[i].r = from1000(rgb[0]);
			pal[i].g = from1000(rgb[1]);
			pal[i].b = from1000(rgb[2]);
		}
		for (i = 0; i < 16; i++)
		{
			colour_vdi_std((unsigned short) i, &c);
			ui_pen[i] = (short) colour_text(pal, 16, &c, 0);
		}
	}
	return ui_pen[n & 15];
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
 * A HYP image (planar, standard format) converted for the screen.  Each
 * pixel value is a standard colour (colour.h), drawn with the screen's
 * nearest: dithered between two pens with 4 colours, black or white on
 * mono (through the hcp source's dither mask when it gave one, else by
 * brightness).  Where pixels are twice as tall as wide (ST medium), two
 * rows make one, so that pictures keep their shape.
 *
 * It is done a piece at a time between event calls (the guidelines'
 * Speed: the page's text shows at once, each picture as it is ready, a
 * grey box holding its place until then): the header at once (the
 * picture's size, for the layout), then the object unpacked a piece at a
 * time, the colours worked out, and the rows converted a band at a time,
 * each band made the screen's own format straight into the picture.
 */
#define UNPACK_STEP 2048               /* bytes unpacked a step */
#define KEYS_STEP   32                 /* colours worked out a step */
#define BAND        8                  /* rows converted a step */

enum { PIC_UNPACK, PIC_KEYS, PIC_ROWS };

static int kept_drop_oldest(void);

typedef struct picjob {
	int stage;
	const hyp_gfx *g;
	hyp_unpack *u;
	unsigned short w, h, oh, np, present, onoff, keys, k, y;
	unsigned long wdw, plane, oplane;
	int halve, same, pairs;
	unsigned char *std, *pl[8], *kbuf, *band, (*pens)[256];
	cpick *pick;
	unsigned long *dev;
} picjob;

static void pic_free(picjob *j)
{
	if (j == NULL)
		return;
	hyp_unpack_free(j->u);
	free(j->std);
	free(j->kbuf);
	free(j->band);
	free(j->pens);
	free(j->pick);
	free(j->dev);
	free(j);
}

/* the picture given up: none shown */
static void pic_fail(gimage *im)
{
	if (im->w > 0)
		log_event("picture-lost", im->form.fd_addr == NULL ? "memory" : "data", NULL);
	pic_free(im->job);
	im->job = NULL;
	st_free(im->form.fd_addr);
	im->form.fd_addr = NULL;
	im->w = 0;
}

/* the job begun: the header read at once (the picture's size) */
static void pic_begin(hyp_doc *doc, const hyp_gfx *g, gimage *im)
{
	picjob *j;
	unsigned char d[8];
	int rc;

	memset(im, 0, sizeof(*im));
	if ((j = calloc(1, sizeof(*j))) == NULL)
		return;
	j->g = g;
	j->u = hyp_unpack_begin(doc, g->image, &rc);
	im->job = j;
	if (j->u == NULL)
	{
		pic_fail(im);
		return;
	}
	if (hyp_unpack_head(j->u, d, sizeof(d)) != HYP_OK)
	{
		pic_fail(im);
		return;
	}
	j->w = (unsigned short) ((d[0] << 8) | d[1]);
	j->h = (unsigned short) ((d[2] << 8) | d[3]);
	j->np = d[4];
	j->present = d[5];
	j->onoff = d[6];
	if (j->w == 0 || j->h == 0 || j->np == 0 || j->np > 8)
	{
		pic_fail(im);
		return;
	}
	j->wdw = (j->w + 15UL) / 16;
	j->plane = j->wdw * 2 * j->h;
	j->halve = tall_pixels && j->h > 1;
	j->oh = (unsigned short) (j->halve ? (j->h + 1) / 2 : j->h);
	j->oplane = j->wdw * 2 * j->oh;
	j->keys = (unsigned short) (j->halve && j->np <= 4 ? 256 : 1 << j->np);	/* pairs: 16 x 16 */
	j->pairs = j->halve && j->np <= 4;
	j->same = !j->halve && planes <= 8 && j->np <= planes;
	im->w = (short) j->w;
	im->h = (short) j->oh;
	im->planes = planes;
	im->mono = planes == 1;
	im->form.fd_w = (short) (j->wdw * 16);
	im->form.fd_h = (short) j->oh;
	im->form.fd_wdwidth = (short) j->wdw;
	im->form.fd_stand = 0;
	im->form.fd_nplanes = planes;
}

/* unpacked: the planes (filling in the ones not stored) and the room */
static int pic_unpacked(picjob *j, gimage *im)
{
	unsigned char *data;
	unsigned long size, p = 8;
	int i;

	data = hyp_unpack_take(j->u, &size);
	j->u = NULL;
	if (data == NULL)
		return -1;
	j->std = malloc(j->plane * j->np);
	if (j->std == NULL)
	{
		free(data);
		return -1;
	}
	for (i = 0; i < j->np; i++)
	{
		unsigned char *dst = j->std + j->plane * (unsigned long) i;

		if (j->present & (1 << i))
		{
			if (p + j->plane > size)
			{
				free(data);
				return -1;
			}
			memcpy(dst, data + p, j->plane);
			p += j->plane;
		} else
			memset(dst, (j->onoff & (1 << i)) ? 0xff : 0x00, j->plane);
	}
	free(data);
	if (spread[0x80] == 0)
		make_spread();
	for (i = 0; i < j->np; i++)
		j->pl[i] = j->std + j->plane * (unsigned long) i;
	j->kbuf = malloc(j->wdw * 16);                 /* a row's pixel values */
	j->pick = malloc(j->keys * sizeof(cpick));
	if (scr.bytes)
		j->dev = malloc(j->keys * sizeof(unsigned long));
	else
		j->band = malloc(j->wdw * 2 * BAND * planes);
	/* copied to the screen: in ST RAM; the pictures kept for going
	   back let go of when there is none */
	while ((im->form.fd_addr = st_alloc((long) (scr.bytes ? j->wdw * 16 * j->oh * scr.bytes :
						    j->oplane * planes))) == NULL && kept_drop_oldest())
		;
	if (j->kbuf == NULL || j->pick == NULL || (scr.bytes ? j->dev == NULL : j->band == NULL) ||
	    im->form.fd_addr == NULL)
		return -1;
	return 0;
}

/* how each pixel value (or, two rows made one, each pair of them up to
   16 colours; above, the upper row's) is drawn: up to KEYS_STEP of them */
static void pic_keys(picjob *j)
{
	const hyp_gfx *g = j->g;
	unsigned short k, end = (unsigned short) (j->k + KEYS_STEP < j->keys ? j->k + KEYS_STEP : j->keys);

	for (k = j->k; k < end; k++)
	{
		unsigned short p1 = j->pairs ? (unsigned short) (k >> 4) : k;
		unsigned short p2 = j->pairs ? (unsigned short) (k & 15) : k;
		crgb c1, c2, c;
		cpick *pk = &j->pick[k];

		colour_image_pen(j->np, p1, &c1);
		colour_image_pen(j->np, p2, &c2);
		colour_mix(&c1, &c2, &c);
		if (planes == 1)
		{
			/* black: the dither mask's bit (pens 0-15), or dark */
			int b1 = g->has_dither ? (g->dithermask >> (p1 & 15)) & 1 : colour_dark(&c1);
			int b2 = g->has_dither ? (g->dithermask >> (p2 & 15)) & 1 : colour_dark(&c2);

			pk->a = (unsigned char) (b1 && b2);
			pk->b = 1;
			pk->t = (unsigned char) (b1 != b2 ? 8 : 0);
		} else if (scr.bytes && scr.packs)
		{
			j->dev[k] = colour_pack(&scr.bits, &c);
			continue;
		} else
		{
			colour_pick(scr.rgb, scr.n, &c, scr.n < 16, pk);
			if (scr.bytes)
				j->dev[k] = scr.dev[pk->a];
		}
		if (pk->t != 0 || pk->a != k)
			j->same = 0;
	}
	j->k = end;
	if (j->k < j->keys || scr.bytes || j->same || planes > 4)
		return;
	/* each key's pen at each place in the Bayer matrix (a table, as the
	   rest: an ST does each pixel in a few instructions) */
	if ((j->pens = malloc(16 * 256)) != NULL)
	{
		int i;

		for (i = 0; i < 16; i++)
			for (k = 0; k < j->keys; k++)
				j->pens[i][k] = j->pick[k].t > colour_bayer[i >> 2][i & 3] ? j->pick[k].b : j->pick[k].a;
	}
}

/* row y of the picture as it is shown (y of oh) into the band's row r
   (standard format), or, in high and true colour, into the picture */
static void pic_row(picjob *j, gimage *im, unsigned short y, unsigned short r, unsigned short n)
{
	unsigned long row = (unsigned long) (j->halve ? 2 * y : y) * j->wdw * 2;
	unsigned long next = j->halve && 2 * y + 1 < j->h ? row + j->wdw * 2 : row;
	unsigned long rb = j->wdw * 2;
	unsigned char *opl[8];
	unsigned short bx, x;
	int i;

	if (scr.bytes)
	{
		/* high and true colour: the screen's own pixels, packed */
		unsigned char *o = (unsigned char *) im->form.fd_addr + (unsigned long) y * j->wdw * 16 * scr.bytes;

		row_keys(j->pl, j->np, row, next, (unsigned short) rb, j->pairs, j->kbuf);
		for (x = 0; x < j->wdw * 16; x++)
		{
			unsigned long px = x < j->w ? j->dev[j->kbuf[x]] : 0;
			short b;

			for (b = scr.bytes; b-- > 0;)
				*o++ = (unsigned char) (px >> (8 * b));
		}
		return;
	}
	for (i = 0; i < planes; i++)
		opl[i] = j->band + (unsigned long) (i * n + r) * rb;
	if (j->same)
	{
		/* the screen's pens are the image's: its planes as they are */
		for (i = 0; i < planes; i++)
			if (i < j->np)
				memcpy(opl[i], j->pl[i] + row, rb);
			else
				memset(opl[i], 0, rb);
		return;
	}
	row_keys(j->pl, j->np, row, next, (unsigned short) rb, j->pairs, j->kbuf);
	if (j->pens != NULL)
	{
		/* up to 4 planes: 4 pixels' pens in a word, then each plane's 4
		   bits gathered from it */
		const unsigned char *t0 = j->pens[(y & 3) * 4], *t1 = t0 + 256, *t2 = t1 + 256, *t3 = t2 + 256;
		const unsigned char *kb = j->kbuf;

		for (bx = 0; bx < rb; bx++, kb += 8)
		{
			unsigned short hi = (unsigned short) (t0[kb[0]] << 12 | t1[kb[1]] << 8 | t2[kb[2]] << 4 | t3[kb[3]]);
			unsigned short lo = (unsigned short) (t0[kb[4]] << 12 | t1[kb[5]] << 8 | t2[kb[6]] << 4 | t3[kb[7]]);

			for (i = 0; i < planes; i++, hi >>= 1, lo >>= 1)
			{
				unsigned short a = hi & 0x1111, b = lo & 0x1111;

				a = (unsigned short) ((a | a >> 3 | a >> 6 | a >> 9) & 15);
				b = (unsigned short) ((b | b >> 3 | b >> 6 | b >> 9) & 15);
				opl[i][bx] = (unsigned char) (a << 4 | b);
			}
		}
		return;
	}
	for (bx = 0, x = 0; bx < rb; bx++)
	{
		unsigned char ob[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }, m;

		for (m = 0x80; m != 0 && x < j->w; m >>= 1, x++)
		{
			unsigned short pen = COLOUR_AT(&j->pick[j->kbuf[x]], x, y);

			for (i = 0; i < planes; i++)
				if (pen & (1 << i))
					ob[i] |= m;
		}
		for (i = 0; i < planes; i++)
			opl[i][bx] = ob[i];
	}
}

/* the next piece: 1 the picture is done (ready, or given up), 0 more */
static int pic_step(gimage *im)
{
	picjob *j = im->job;

	if (j == NULL)
		return 1;
	if (j->stage == PIC_UNPACK)
	{
		int r = hyp_unpack_more(j->u, UNPACK_STEP);

		if (r < 0 || (r == 1 && pic_unpacked(j, im) < 0))
		{
			pic_fail(im);
			return 1;
		}
		if (r == 1)
			j->stage = PIC_KEYS;
		return 0;
	}
	if (j->stage == PIC_KEYS)
	{
		pic_keys(j);
		if (j->k == j->keys)
			j->stage = PIC_ROWS;
		return 0;
	}
	{
		unsigned short n = (unsigned short) (j->oh - j->y < BAND ? j->oh - j->y : BAND), r;

		for (r = 0; r < n; r++)
			pic_row(j, im, (unsigned short) (j->y + r), r, n);
		if (!scr.bytes)
		{
			/* the band made the screen's own format, in its place */
			MFDB src, dst;

			src = im->form;
			src.fd_addr = j->band;
			src.fd_h = (short) n;
			src.fd_stand = 1;
			dst = im->form;
			dst.fd_addr = (unsigned char *) im->form.fd_addr + (unsigned long) j->y * j->wdw * 2 * planes;
			dst.fd_h = (short) n;
			vr_trnfm(vdi_h, &src, &dst);
		}
		j->y = (unsigned short) (j->y + n);
	}
	if (j->y < j->oh)
		return 0;
	pic_free(j);
	im->job = NULL;
	return 1;
}

/* ---------------------------------------------------------------- */
/* pictures kept: the last pages' pictures, ready, for going back      */

#define KEPT 6

static struct {
	char *path;                    /* NULL: free */
	unsigned short num;
	gimage im;
	unsigned long when;
} kept[KEPT];
static unsigned long kept_clock;

static void kept_drop(int i)
{
	st_free(kept[i].im.form.fd_addr);
	free(kept[i].path);
	memset(&kept[i], 0, sizeof(kept[i]));
}

/* the one kept longest let go: 0 when none is kept */
static int kept_drop_oldest(void)
{
	int i, old = -1;

	for (i = 0; i < KEPT; i++)
		if (kept[i].path != NULL && (old < 0 || kept[i].when < kept[old].when))
			old = i;
	if (old < 0)
		return 0;
	kept_drop(old);
	return 1;
}

static void kept_put(const char *path, unsigned short num, const gimage *im)
{
	int i, slot = -1;

	for (i = 0; i < KEPT && slot < 0; i++)
		if (kept[i].path == NULL)
			slot = i;
	if (slot < 0)
	{
		kept_drop_oldest();
		for (i = 0; i < KEPT && slot < 0; i++)
			if (kept[i].path == NULL)
				slot = i;
	}
	if (slot < 0 || (kept[slot].path = malloc(strlen(path) + 1)) == NULL)
	{
		st_free(im->form.fd_addr);
		return;
	}
	strcpy(kept[slot].path, path);
	kept[slot].num = num;
	kept[slot].im = *im;
	kept[slot].when = ++kept_clock;
}

static int kept_take(const char *path, unsigned short num, gimage *im)
{
	int i;

	for (i = 0; i < KEPT; i++)
		if (kept[i].path != NULL && kept[i].num == num && strcmp(kept[i].path, path) == 0)
		{
			*im = kept[i].im;
			free(kept[i].path);
			memset(&kept[i], 0, sizeof(kept[i]));
			return 1;
		}
	return 0;
}

void layout_free(playout *L)
{
	unsigned short i;

	for (i = 0; L->images != NULL && i < L->nimages; i++)
	{
		gimage *im = &L->images[i];

		if (im->job != NULL)
			pic_fail(im);
		else if (im->form.fd_addr != NULL && L->path != NULL)
			kept_put(L->path, L->pg->gfx[i].image, im);     /* for going back */
		else
			st_free(im->form.fd_addr);
	}
	free(L->images);
	free(L->line_y);
	free(L->gfx_y);
	free(L->gfx_at);
	free(L->path);
	memset(L, 0, sizeof(*L));
}

/* the pictures' memory let go of, the ones kept for going back too */
void layout_forget_kept(void)
{
	while (kept_drop_oldest())
		;
}

/* the pictures' log line, for the tests: how many, and in which RAM */
static void log_pictures(const playout *L)
{
	unsigned short i, shown = 0, in_st = 0;
	char num[8];

	for (i = 0; i < L->nimages; i++)
		if (L->images[i].form.fd_addr != NULL)
		{
			shown++;
			in_st = (unsigned short) (in_st + st_ram_addr(L->images[i].form.fd_addr));
		}
	if (shown == 0)
		return;
	/* (a blitter reads only ST RAM) */
	sprintf(num, "%u", shown);
	log_event("pictures", num, in_st == shown ? "st-ram" : "fast-ram");
}

int layout_busy(const playout *L)
{
	unsigned short i;

	for (i = 0; L->images != NULL && i < L->nimages; i++)
		if (L->images[i].job != NULL)
			return 1;
	return 0;
}

int layout_work(playout *L, long *done)
{
	unsigned short i;

	*done = -1;
	for (i = 0; L->images != NULL && i < L->nimages; i++)
		if (L->images[i].job != NULL)
		{
			if (pic_step(&L->images[i]) == 1)
			{
				*done = i;
				if (!layout_busy(L))
					log_pictures(L);
				return layout_busy(L);
			}
			return 1;
		}
	return 0;
}

void layout_finish(playout *L)
{
	long done;

	while (layout_work(L, &done))
		;
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
	L->path = malloc(strlen(doc->path) + 1);
	if (L->line_y == NULL || L->gfx_y == NULL || L->images == NULL || L->gfx_at == NULL || L->path == NULL)
	{
		layout_free(L);
		return;
	}
	strcpy(L->path, doc->path);
	L->pg = pg;
	L->nimages = pg->ngfx;
	screen_colours();
	{
		int now = 0;

		for (i = 0; i < pg->ngfx; i++)
			if (pg->gfx[i].type == HYP_GFX_IMAGE)
			{
				if (kept_take(L->path, pg->gfx[i].image, &L->images[i]))
					now++;                     /* kept from before: ready */
				else
					pic_begin(doc, &pg->gfx[i], &L->images[i]);
			}
		if (now > 0 && !layout_busy(L))
			log_pictures(L);
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

int layout_picture_at(const playout *L, unsigned short i, short ox, long oy, long cols,
                      long *x, long *y, short *w, short *h)
{
	const hyp_gfx *g;
	const gimage *im;

	if (L->pg == NULL || i >= L->nimages)
		return 0;
	g = &L->pg->gfx[i];
	im = &L->images[i];
	if (g->type != HYP_GFX_IMAGE || im->w <= 0)
		return 0;
	*x = ox + (long) (g->x > 0 ? g->x - 1 : 0) * cw;
	if (g->x == 0)
	{
		long tx = (cols * cw - im->w) / 2;

		*x = ox + (tx > 0 ? tx : 0);
	}
	*y = oy + L->gfx_at[i];
	*w = im->w;
	*h = im->h;
	return 1;
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
		if (im->job != NULL)
		{
			/* still being made: a grey box holds its place */
			pxy[0] = (short) x;
			pxy[1] = (short) y;
			pxy[2] = (short) (x + im->w - 1);
			pxy[3] = (short) (y + im->h - 1);
			vswr_mode(vdi_h, MD_REPLACE);
			vsf_interior(vdi_h, FIS_PATTERN);
			vsf_style(vdi_h, 1);
			vsf_color(vdi_h, 1);
			vsf_perimeter(vdi_h, 1);
			v_bar(vdi_h, pxy);
			vsf_perimeter(vdi_h, 0);
			vsf_interior(vdi_h, FIS_SOLID);
			break;
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
	int plain;
	short mode, colour, effects;

	if (pg == NULL)
		return;
	/* graphics first, text on top */
	{
		unsigned short i;

		for (i = 0; i < pg->ngfx; i++)
			draw_gfx(L, &pg->gfx[i], &L->images[i], ox, oy, cy, (long) cy + ch_, cols);
	}

	/* the text over the white ground: drawn replacing it, the VDI's fast
	   way (transparent text is many times slower on an ST), unless a
	   picture is under it; the VDI told a mode, colour or effect only
	   when it changes */
	plain = pg->ngfx == 0;
	mode = colour = effects = -1;
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
			short fx = run->attr & 0x3f, fg, want = plain ? MD_REPLACE : MD_TRANS;

			if (x + w > cx && x < cx + cw_)
			{
				if ((long) (ln->first_run + r) == sel)
				{
					/* the selected link: reverse video */
					fill(link_color(), (short) x, (short) y, (short) (x + w - 1), (short) (y + ch - 1));
					mode = MD_REPLACE;
					want = MD_TRANS;
					fg = 0;
				} else if (run->kind == HYP_RUN_LINK)
				{
					fg = link_color();
					fx |= 8 | (ncolors >= 16 ? 0 : 1);   /* underlined (and bold in mono) */
				} else
				{
					/* hcp's @{fg n} and @{bg n} */
					short bg = run->bg != HYP_COLOR_DEFAULT ? text_colour(run->bg, 1) : -1;

					fg = run->fg != HYP_COLOR_DEFAULT ? text_colour(run->fg, 0) : 1;
					if (bg >= 0)
					{
						fill(bg, (short) x, (short) y, (short) (x + w - 1), (short) (y + ch - 1));
						mode = MD_REPLACE;
						want = MD_TRANS;
						if (planes == 1 && fg == 1)
							fg = 0;            /* black on black: white */
					}
				}
				{
					/* bold and underline on the white ground: drawn
					   the cheap way, the same pixels (text_fx_after) */
					short by_hand = want == MD_REPLACE && text_fx_fast(fx, want) ? fx : 0;
					/* bold any other way (NVDI, a GDOS font, a ground
					   of its own): struck twice, a pixel apart. The
					   VDI's bold widens each letter (NVDI by a pixel
					   or more), and a run of it ran past its cells,
					   the next run covering its last letters (Erez's
					   photo, 27 Oct 2026); the layout's cells are
					   fixed */
					short twice = !by_hand && (fx & 1) ? 1 : 0;

					if (mode != want)
						vswr_mode(vdi_h, mode = want);
					if (colour != fg)
						vst_color(vdi_h, colour = fg);
					if (effects != (fx & ~by_hand & ~twice))
						vst_effects(vdi_h, effects = (short) (fx & ~by_hand & ~twice));
					text_at((short) x, (short) y, run->text, run->len);
					if (by_hand)
					{
						text_fx_after((short) x, (short) y, run->text, run->len, by_hand, fg);
						vswr_mode(vdi_h, mode);
					}
					if (twice)
					{
						if (mode != MD_TRANS)
							vswr_mode(vdi_h, mode = MD_TRANS);
						if (effects & 8)
							vst_effects(vdi_h, effects = (short) (effects & ~8));   /* one underline */
						text_at((short) (x + 1), (short) y, run->text, run->len);
					}
				}
			}
			x += w;
		}
	}
	vst_effects(vdi_h, 0);
	vst_color(vdi_h, 1);
	vswr_mode(vdi_h, MD_TRANS);
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
