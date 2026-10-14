/*
 * EHypView - the text font setting (no VDI here, so it is tested on the
 * host; src/gem/fontsel.c measures and sets the fonts).
 *
 * The setting lives in EHYPVIEW.INF next to the program:
 *
 *   FONT=Courier
 *   POINT=10
 *
 * The font is kept by name, since GDOS font ids change when fonts are
 * added.  "System font" with point 8 or 16 is the system font's 8x8 or
 * 8x16 size; point 0 is the size the AES uses.
 *
 * Copyright (C) the EHypView authors.  Free software under the GNU
 * General Public License, version 2 or (at your option) any later
 * version; see COPYING.
 */
#ifndef EHV_FONTPREF_H
#define EHV_FONTPREF_H

#define FONT_SYSTEM_NAME "System font"
#define FONT_NAME_MAX    33

typedef struct {
	char name[FONT_NAME_MAX];
	short point;
} font_pref;

/* the system font at the AES's size */
void fontpref_default(font_pref *p);
int fontpref_is_system(const font_pref *p);

/* read FONT= and POINT= from the text of EHYPVIEW.INF (other lines are
   kept for other settings); missing or bad values leave the default */
void fontpref_parse(font_pref *p, const char *text);

/* "FONT=...\r\nPOINT=...\r\n"; the length, or 0 when it doesn't fit */
unsigned long fontpref_format(const font_pref *p, char *buf, unsigned long size);

/*
 * sizes[] is a font's sizes, smallest first.  The index of the size to
 * use for `want` (the largest not above it, else the smallest), or -1
 * when there are none.
 */
int fontpref_pick(const short *sizes, int n, short want);

/* the next size up (dir > 0) or down from `cur`, or -1 at the end */
int fontpref_step(const short *sizes, int n, short cur, int dir);

#endif
