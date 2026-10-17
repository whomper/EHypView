/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the colours of pictures and text, whatever the screen has.
 *
 * A HYP image carries no palette: its pixel values are the standard TOS
 * colours in hardware pen order (pen 0 white, 1 red, ... 15 black).  Text
 * colours (hcp's @{fg n} / @{bg n}) are VDI colour numbers (0 white,
 * 1 black, 2 red, ...).  Both are mapped to what the screen has: its pens
 * by nearest colour, dithered between two of them where it has too few
 * (4 colours).  The VDI side (reading the palette) is pagedraw.c's; this
 * part has none, so the tests run it on the host.
 */
#ifndef EHV_COLOUR_H
#define EHV_COLOUR_H

typedef struct {
	unsigned char r, g, b;
} crgb;

/* the colour of pixel value pen in an image of np planes */
void colour_image_pen(unsigned short np, unsigned short pen, crgb *c);

/* the standard colour of VDI colour number n */
void colour_vdi_std(unsigned short n, crgb *c);

/* the hardware pen of VDI colour n on a screen of ncol pens (2, 4, 16,
   256), as TOS, EmuTOS and NVDI map them */
unsigned short colour_pen_of_vdi(unsigned short ncol, unsigned short n);

/* how one colour is drawn: target a, or b where the 4 x 4 Bayer matrix
   is below t (t 0..16; 0: a alone) */
typedef struct {
	unsigned char a, b, t;
} cpick;

/* the nearest of the n colours of pal; dither: between two of them when
   that comes nearer (n at most 16) */
void colour_pick(const crgb *pal, unsigned short n, const crgb *c, int dither, cpick *out);

/* the target of a pick at pixel (x, y) */
#define COLOUR_AT(p, x, y) ((p)->t > colour_bayer[(y) & 3][(x) & 3] ? (p)->b : (p)->a)
extern const unsigned char colour_bayer[4][4];

/* the colour of pal that text of colour c is drawn in (bg: behind the
   text; -1: none, the page's white).  Text comes out white only when it
   is white, and a grey as a grey (or black) where the screen has one */
int colour_text(const crgb *pal, unsigned short n, const crgb *c, int bg);

/* the mean of two colours (two rows made one) */
void colour_mix(const crgb *a, const crgb *b, crgb *out);

/* high and true colour: the pixel bit each of a colour's 8 bits goes to
   (-1: none), red, green, blue */
typedef struct {
	signed char bit[3][8];
} cbits;

/* from vq_scrninfo's answer (info[0] 2: packed pixels; info[8..10] the
   bits of red, green, blue; info[16..63] the pixel bit of each, lowest
   first); 0 when the pixels aren't packed */
int colour_bits_info(const short *info, cbits *b);

/* from the pixels of red, green, blue, white and black (as read from the
   screen): a run of bits each, white all of them; 0 when they aren't */
int colour_bits_read(const unsigned long *rgbwk, cbits *b);

/* a pixel of colour c */
unsigned long colour_pack(const cbits *b, const crgb *c);

/* darker than half: black on a mono screen */
int colour_dark(const crgb *c);

#endif
