/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the colours of pictures and text (colour.h).
 */
#include <stddef.h>
#include "colour.h"

/* pens 0-15 as TOS and EmuTOS set them, measured on a Falcon (VGA) */
static const unsigned char std16[16][3] = {
	{ 0xFF, 0xFF, 0xFF }, { 0xFF, 0x00, 0x00 }, { 0x00, 0xFF, 0x00 }, { 0xFF, 0xFF, 0x00 },
	{ 0x00, 0x00, 0xFF }, { 0xFF, 0x00, 0xFF }, { 0x00, 0xFF, 0xFF }, { 0xBA, 0xBA, 0xBA },
	{ 0x8A, 0x8A, 0x8A }, { 0xAA, 0x00, 0x00 }, { 0x00, 0xAA, 0x00 }, { 0xAA, 0xAA, 0x00 },
	{ 0x00, 0x00, 0xAA }, { 0xAA, 0x00, 0xAA }, { 0x00, 0xAA, 0xAA }, { 0x00, 0x00, 0x00 }
};

/* VDI colours 0-15 to hardware pens (16 colours; VDI 1, black, is the last pen) */
static const unsigned char vdi_pen[16] = { 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };

const unsigned char colour_bayer[4][4] = {
	{ 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
};

static void set(crgb *c, unsigned char r, unsigned char g, unsigned char b)
{
	c->r = r;
	c->g = g;
	c->b = b;
}

/* pens 16-254 of 256 colours: 16 greys, then four rings of hues
   (red, magenta, blue, cyan, green, yellow) at falling brightness, in
   steps of 0x11 (EmuTOS's and TOS's palette) */
static void pen256(unsigned short pen, crgb *c)
{
	static const unsigned char levels[4] = { 15, 11, 7, 4 };
	unsigned short i = (unsigned short) (pen - 16), k;

	if (i < 16)
	{
		unsigned char v = (unsigned char) (0xFF - i * 0x11);

		set(c, v, v, v);
		return;
	}
	i -= 16;
	for (k = 0; k < 4; k++)
	{
		unsigned short n = levels[k];
		unsigned char l = (unsigned char) (n * 0x11), s, d;

		if (i >= 6 * n)
		{
			i = (unsigned short) (i - 6 * n);
			continue;
		}
		s = (unsigned char) ((i % n) * 0x11);    /* rising */
		d = (unsigned char) (l - s);             /* falling */
		switch (i / n)
		{
		case 0: set(c, l, 0, s); break;
		case 1: set(c, d, 0, l); break;
		case 2: set(c, 0, s, l); break;
		case 3: set(c, 0, l, d); break;
		case 4: set(c, s, l, 0); break;
		default: set(c, l, d, 0); break;
		}
		return;
	}
	set(c, 0xFF, 0xFF, 0xFF);                   /* pen 254 */
}

void colour_image_pen(unsigned short np, unsigned short pen, crgb *c)
{
	if (np == 1)
		pen = pen ? 15 : 0;                      /* white, black */
	else if (np == 2)
		pen = pen == 3 ? 15 : pen;               /* white, red, green, black */
	if (pen < 16)
		set(c, std16[pen][0], std16[pen][1], std16[pen][2]);
	else if (pen < 255)
		pen256(pen, c);
	else
		set(c, 0, 0, 0);                         /* VDI 1 of 256 colours */
}

unsigned short colour_pen_of_vdi(unsigned short ncol, unsigned short n)
{
	unsigned short pen;

	if (n == 1)
		pen = (unsigned short) (ncol - 1);
	else if (n < 16)
		pen = vdi_pen[n];
	else if (n < 255)
		pen = n;
	else
		pen = 15;
	return (unsigned short) (pen & (ncol - 1));
}

void colour_vdi_std(unsigned short n, crgb *c)
{
	colour_image_pen(8, colour_pen_of_vdi(256, n), c);
}

/* distances weigh green most and blue least, roughly as the eye does
   (red, green, blue: 2, 4, 3) */
#define WR 2
#define WG 4
#define WB 3

static long dist(const crgb *a, const crgb *b)
{
	long dr = (long) a->r - b->r, dg = (long) a->g - b->g, db = (long) a->b - b->b;

	return WR * dr * dr + WG * dg * dg + WB * db * db;
}

void colour_pick(const crgb *pal, unsigned short n, const crgb *c, int dither, cpick *out)
{
	unsigned short i, j;
	long best;

	out->a = out->b = 0;
	out->t = 0;
	best = -1;
	for (i = 0; i < n; i++)
	{
		long d = dist(&pal[i], c);

		if (best < 0 || d < best)
		{
			best = d;
			out->a = out->b = (unsigned char) i;
		}
	}
	if (!dither || best == 0 || n > 16)
		return;
	/* the nearest point on the line between two pens: c = a + t (b - a) */
	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
		{
			long ar = pal[i].r, ag = pal[i].g, ab = pal[i].b;
			long er = (long) pal[j].r - ar, eg = (long) pal[j].g - ag, eb = (long) pal[j].b - ab;
			long len = WR * er * er + WG * eg * eg + WB * eb * eb, dot, t, pr, pg, pb, d;

			if (i == j || len == 0)
				continue;
			dot = WR * ((long) c->r - ar) * er + WG * ((long) c->g - ag) * eg + WB * ((long) c->b - ab) * eb;
			if (dot <= 0 || dot >= len)
				continue;                        /* nearer to one of them */
			t = (dot * 16 + len / 2) / len;      /* in sixteenths */
			if (t <= 0 || t >= 16 || t > 8)
				continue;                        /* (j, i) has it, with 16 - t */
			pr = ar + er * t / 16 - c->r;
			pg = ag + eg * t / 16 - c->g;
			pb = ab + eb * t / 16 - c->b;
			d = WR * pr * pr + WG * pg * pg + WB * pb * pb;
			if (d < best)
			{
				best = d;
				out->a = (unsigned char) i;
				out->b = (unsigned char) j;
				out->t = (unsigned char) t;
			}
		}
}

static int is_white(const crgb *c)
{
	return c->r == 255 && c->g == 255 && c->b == 255;
}

static int is_grey(const crgb *c)
{
	return c->r == c->g && c->g == c->b;
}

int colour_text(const crgb *pal, unsigned short n, const crgb *c, int bg)
{
	unsigned short i, pass;
	int best = -1;
	long bd = 0;

	if (bg && is_white(c))
		return -1;
	/* first a grey for a grey, then any */
	for (pass = is_grey(c) && !is_white(c) ? 0 : 1; pass < 2 && best < 0; pass++)
		for (i = 0; i < n; i++)
		{
			long d;

			if (!bg && !is_white(c) && is_white(&pal[i]))
				continue;
			if (pass == 0 && !is_grey(&pal[i]))
				continue;
			d = dist(&pal[i], c);
			if (best < 0 || d < bd)
			{
				best = i;
				bd = d;
			}
		}
	if (best >= 0 && bg && is_white(&pal[best]))
		return -1;
	return best;
}

void colour_mix(const crgb *a, const crgb *b, crgb *out)
{
	out->r = (unsigned char) ((a->r + b->r + 1) / 2);
	out->g = (unsigned char) ((a->g + b->g + 1) / 2);
	out->b = (unsigned char) ((a->b + b->b + 1) / 2);
}

int colour_dark(const crgb *c)
{
	return 299L * c->r + 587L * c->g + 114L * c->b < 128000L;
}

/* bits 0-7 of a colour from the n bits a pixel keeps of it, the first at
   pixel bit at(0), the next at(1)...: the colour's top bits */
static void top_bits(signed char *bit, int n, const short *at, int lo)
{
	int b;

	for (b = 0; b < 8; b++)
	{
		int src = n >= 8 ? b + n - 8 : b - (8 - n);

		bit[b] = (signed char) (src < 0 || src >= 16 ? -1 : at != NULL ? at[src] : lo + src);
	}
}

int colour_bits_info(const short *info, cbits *b)
{
	int k;

	if (info[0] != 2)
		return 0;
	for (k = 0; k < 3; k++)
	{
		if (info[8 + k] <= 0 || info[8 + k] > 16)
			return 0;
		top_bits(b->bit[k], info[8 + k], &info[16 + 16 * k], 0);
	}
	return 1;
}

int colour_bits_read(const unsigned long *rgbwk, cbits *b)
{
	unsigned long seen = 0;
	int k;

	for (k = 0; k < 3; k++)
	{
		unsigned long m = rgbwk[k], run;
		int lo = 0, n = 0;

		if (m == 0 || (m & seen) != 0)
			return 0;
		seen |= m;
		while (!((m >> lo) & 1))
			lo++;
		run = m >> lo;
		if (run & (run + 1))
			return 0;                            /* not one run */
		while (n < 32 && (run >> n) != 0)
			n++;
		top_bits(b->bit[k], n, NULL, lo);
	}
	return rgbwk[3] == seen && rgbwk[4] == 0;
}

unsigned long colour_pack(const cbits *b, const crgb *c)
{
	const unsigned char v[3] = { c->r, c->g, c->b };
	unsigned long px = 0;
	int k, i;

	for (k = 0; k < 3; k++)
		for (i = 0; i < 8; i++)
			if (b->bit[k][i] >= 0 && (v[k] & (1 << i)))
				px |= 1UL << b->bit[k][i];
	return px;
}
