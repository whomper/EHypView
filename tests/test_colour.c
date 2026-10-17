/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView tests for the colours of pictures and text (src/gem/colour.c):
 * the standard pens and VDI colours, the pens VDI colours draw with, the
 * nearest pen and the dither between two, and the Bayer matrix.
 *
 *   test_colour
 */
#include <stdio.h>
#include <string.h>
#include "colour.h"

static int failures, checks;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

static unsigned long rgb_of(const crgb *c)
{
	return (unsigned long) c->r << 16 | (unsigned long) c->g << 8 | c->b;
}

static unsigned long pen(unsigned short np, unsigned short p)
{
	crgb c;

	colour_image_pen(np, p, &c);
	return rgb_of(&c);
}

static unsigned long vdi(unsigned short n)
{
	crgb c;

	colour_vdi_std(n, &c);
	return rgb_of(&c);
}

static void set(crgb *c, unsigned long v)
{
	c->r = (unsigned char) (v >> 16);
	c->g = (unsigned char) (v >> 8);
	c->b = (unsigned char) v;
}

/* the image pens: 16 as measured, 2 and 4 planes as their screens, 256 */
static void test_pens(void)
{
	static const unsigned long std[16] = {
		0xFFFFFF, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xBABABA,
		0x8A8A8A, 0xAA0000, 0x00AA00, 0xAAAA00, 0x0000AA, 0xAA00AA, 0x00AAAA, 0x000000
	};
	unsigned short i;

	for (i = 0; i < 16; i++)
	{
		CHECK(pen(4, i) == std[i]);
		CHECK(pen(8, i) == std[i]);
	}
	CHECK(pen(1, 0) == 0xFFFFFF && pen(1, 1) == 0x000000);
	CHECK(pen(2, 0) == 0xFFFFFF && pen(2, 1) == 0xFF0000 && pen(2, 2) == 0x00FF00 && pen(2, 3) == 0x000000);
	/* 256: greys, then the rings, white and (VDI 1) black at the end */
	CHECK(pen(8, 16) == 0xFFFFFF && pen(8, 17) == 0xEEEEEE && pen(8, 31) == 0x000000);
	CHECK(pen(8, 32) == 0xFF0000 && pen(8, 33) == 0xFF0011 && pen(8, 47) == 0xFF00FF);
	CHECK(pen(8, 48) == 0xEE00FF && pen(8, 62) == 0x0000FF && pen(8, 121) == 0xFF1100);
	CHECK(pen(8, 122) == 0xBB0000 && pen(8, 188) == 0x770000 && pen(8, 230) == 0x440000);
	CHECK(pen(8, 253) == 0x441100 && pen(8, 254) == 0xFFFFFF && pen(8, 255) == 0x000000);
}

/* VDI colours: their standard colours, and the pen each draws with */
static void test_vdi(void)
{
	static const unsigned long std[16] = {
		0xFFFFFF, 0x000000, 0xFF0000, 0x00FF00, 0x0000FF, 0x00FFFF, 0xFFFF00, 0xFF00FF,
		0xBABABA, 0x8A8A8A, 0xAA0000, 0x00AA00, 0x0000AA, 0x00AAAA, 0xAAAA00, 0xAA00AA
	};
	static const unsigned short pen16[16] = { 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };
	unsigned short i;

	for (i = 0; i < 16; i++)
	{
		CHECK(vdi(i) == std[i]);
		CHECK(colour_pen_of_vdi(16, i) == pen16[i]);
	}
	CHECK(colour_pen_of_vdi(2, 0) == 0 && colour_pen_of_vdi(2, 1) == 1);
	CHECK(colour_pen_of_vdi(4, 0) == 0 && colour_pen_of_vdi(4, 1) == 3);
	CHECK(colour_pen_of_vdi(4, 2) == 1 && colour_pen_of_vdi(4, 3) == 2);
	CHECK(colour_pen_of_vdi(256, 1) == 255 && colour_pen_of_vdi(256, 2) == 1);
	CHECK(colour_pen_of_vdi(256, 16) == 16 && colour_pen_of_vdi(256, 254) == 254);
	CHECK(colour_pen_of_vdi(256, 255) == 15);
	CHECK(vdi(255) == 0x000000 && vdi(16) == 0xFFFFFF);
}

static void test_pick(void)
{
	crgb pal16[16], pal4[4], c;
	cpick p;
	unsigned short i;

	for (i = 0; i < 16; i++)
		colour_image_pen(4, i, &pal16[i]);
	for (i = 0; i < 4; i++)
		colour_image_pen(2, i, &pal4[i]);

	/* 16 colours: every pen is its own, dither or not */
	for (i = 0; i < 16; i++)
	{
		colour_pick(pal16, 16, &pal16[i], 1, &p);
		CHECK(p.a == i && p.t == 0);
	}
	/* 4 colours: white, red, green and black are their own */
	for (i = 0; i < 4; i++)
	{
		colour_pick(pal4, 4, &pal4[i], 1, &p);
		CHECK(p.a == i && p.t == 0);
	}
	/* light grey: white with a quarter of black */
	set(&c, 0xBABABA);
	colour_pick(pal4, 4, &c, 1, &p);
	CHECK(p.a == 0 && p.b == 3 && p.t == 4);
	/* without dither, white */
	colour_pick(pal4, 4, &c, 0, &p);
	CHECK(p.a == 0 && p.t == 0);
	/* dark grey: nearly half */
	set(&c, 0x8A8A8A);
	colour_pick(pal4, 4, &c, 1, &p);
	CHECK(p.a == 0 && p.b == 3 && p.t == 7);
	/* dark red: red and black */
	set(&c, 0xAA0000);
	colour_pick(pal4, 4, &c, 1, &p);
	CHECK(((p.a == 1 && p.b == 3) || (p.a == 3 && p.b == 1)) && p.t > 0 && p.t <= 8);
	/* yellow: a dither of two bright pens, no black */
	set(&c, 0xFFFF00);
	colour_pick(pal4, 4, &c, 1, &p);
	CHECK(p.t > 0 && p.a != 3 && p.b != 3 && p.a != p.b);
}

/* text: never white unless white, greys grey, white behind it none */
static void test_text(void)
{
	crgb pal16[16], pal4[4], pal2[2], c;
	unsigned short i;

	for (i = 0; i < 16; i++)
		colour_image_pen(4, i, &pal16[i]);
	for (i = 0; i < 4; i++)
		colour_image_pen(2, i, &pal4[i]);
	for (i = 0; i < 2; i++)
		colour_image_pen(1, i, &pal2[i]);

	/* 16 colours: every VDI colour on its own pen */
	for (i = 0; i < 16; i++)
	{
		colour_vdi_std(i, &c);
		CHECK(colour_text(pal16, 16, &c, 0) == colour_pen_of_vdi(16, i));
		CHECK(colour_text(pal16, 16, &c, 1) == (i == 0 ? -1 : colour_pen_of_vdi(16, i)));
	}
	/* 4 colours: red and green as they are, the rest black; yellow
	   behind is green, white behind none */
	set(&c, 0xFF0000);
	CHECK(colour_text(pal4, 4, &c, 0) == 1);
	set(&c, 0x0000FF);
	CHECK(colour_text(pal4, 4, &c, 0) == 3);
	set(&c, 0xBABABA);
	CHECK(colour_text(pal4, 4, &c, 0) == 3);
	set(&c, 0xFFFFFF);
	CHECK(colour_text(pal4, 4, &c, 0) == 0);
	CHECK(colour_text(pal4, 4, &c, 1) == -1);
	set(&c, 0xFFFF00);
	CHECK(colour_text(pal4, 4, &c, 1) == 2);
	/* mono: all text but white black; light behind none, dark black */
	for (i = 1; i < 16; i++)
	{
		colour_vdi_std(i, &c);
		CHECK(colour_text(pal2, 2, &c, 0) == 1);
	}
	set(&c, 0xFFFF00);
	CHECK(colour_text(pal2, 2, &c, 1) == -1);
	set(&c, 0x000000);
	CHECK(colour_text(pal2, 2, &c, 1) == 1);
}

/* t of 16 cells are below t, for every t */
static void test_bayer(void)
{
	unsigned short t, x, y, n, seen[16];

	memset(seen, 0, sizeof(seen));
	for (y = 0; y < 4; y++)
		for (x = 0; x < 4; x++)
			seen[colour_bayer[y][x]]++;
	for (t = 0; t < 16; t++)
		CHECK(seen[t] == 1);
	for (t = 0; t <= 16; t++)
	{
		cpick p;

		p.a = 0;
		p.b = 1;
		p.t = (unsigned char) t;
		n = 0;
		for (y = 0; y < 4; y++)
			for (x = 0; x < 4; x++)
				n = (unsigned short) (n + COLOUR_AT(&p, x + 8, y + 4));
		CHECK(n == t);
	}
}

static void test_dark(void)
{
	crgb c, d;

	set(&c, 0xFF0000);
	CHECK(colour_dark(&c));
	set(&c, 0x00FF00);
	CHECK(!colour_dark(&c));
	set(&c, 0xFF00FF);
	CHECK(colour_dark(&c));
	set(&c, 0xFFFF00);
	CHECK(!colour_dark(&c));
	set(&c, 0x8A8A8A);
	CHECK(!colour_dark(&c));
	set(&c, 0x000000);
	set(&d, 0xFFFFFF);
	colour_mix(&c, &d, &c);
	CHECK(rgb_of(&c) == 0x808080);
}

/* high and true colour: the pixel from vq_scrninfo's answer, or from
   the pixels read back */
static void test_bits(void)
{
	short info[272];
	cbits b;
	crgb c;
	unsigned long rgbwk[5];
	int i;

	/* the Falcon's: RRRRRGGG GGGBBBBB */
	memset(info, -1, sizeof(info));
	info[0] = 2;
	info[8] = 5;
	info[9] = 6;
	info[10] = 5;
	for (i = 0; i < 5; i++)
	{
		info[16 + i] = (short) (11 + i);
		info[48 + i] = (short) i;
	}
	for (i = 0; i < 6; i++)
		info[32 + i] = (short) (5 + i);
	CHECK(colour_bits_info(info, &b));
	set(&c, 0xFF0000);
	CHECK(colour_pack(&b, &c) == 0xF800);
	set(&c, 0x00FF00);
	CHECK(colour_pack(&b, &c) == 0x07E0);
	set(&c, 0x0000FF);
	CHECK(colour_pack(&b, &c) == 0x001F);
	set(&c, 0xBABABA);
	CHECK(colour_pack(&b, &c) == 0xBDD7);
	info[0] = 0;
	CHECK(!colour_bits_info(info, &b));         /* planes, not packed pixels */

	/* Intel byte order (a NOVA card): GGGBBBBB RRRRRGGG as read */
	info[0] = 2;
	for (i = 0; i < 5; i++)
	{
		info[16 + i] = (short) (3 + i);
		info[48 + i] = (short) (8 + i);
	}
	for (i = 0; i < 3; i++)
	{
		info[32 + i] = (short) (13 + i);
		info[35 + i] = (short) i;
	}
	CHECK(colour_bits_info(info, &b));
	set(&c, 0xFF0000);
	CHECK(colour_pack(&b, &c) == 0x00F8);
	set(&c, 0x00FF00);
	CHECK(colour_pack(&b, &c) == 0xE007);
	set(&c, 0x0000FF);
	CHECK(colour_pack(&b, &c) == 0x1F00);

	/* 32 bits, xRGB */
	info[8] = info[9] = info[10] = 8;
	for (i = 0; i < 8; i++)
	{
		info[16 + i] = (short) (16 + i);
		info[32 + i] = (short) (8 + i);
		info[48 + i] = (short) i;
	}
	CHECK(colour_bits_info(info, &b));
	set(&c, 0x123456);
	CHECK(colour_pack(&b, &c) == 0x123456);

	/* read back from EmuTOS on a Falcon: its green tops at 62 of 63 */
	rgbwk[0] = 0xF800;
	rgbwk[1] = 0x07C0;
	rgbwk[2] = 0x001F;
	rgbwk[3] = 0xFFDF;
	rgbwk[4] = 0;
	CHECK(colour_bits_read(rgbwk, &b));
	set(&c, 0x00FF00);
	CHECK(colour_pack(&b, &c) == 0x07C0);
	set(&c, 0x808080);
	CHECK(colour_pack(&b, &c) == 0x8410);
	rgbwk[3] = 0xFFFF;
	CHECK(!colour_bits_read(rgbwk, &b));        /* white has more */
	rgbwk[3] = 0xFFDF;
	rgbwk[0] = 0xF801;
	CHECK(!colour_bits_read(rgbwk, &b));        /* not one run of bits */
	rgbwk[0] = 0xF840;
	CHECK(!colour_bits_read(rgbwk, &b));        /* red's and green's bits shared */
}

int main(void)
{
	test_pens();
	test_vdi();
	test_pick();
	test_text();
	test_bayer();
	test_dark();
	test_bits();
	printf("colour: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
