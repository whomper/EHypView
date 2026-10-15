/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the drawing the AERA programs' shared GEM building blocks
 * take from the app (common/ui/gemui.h), on EHypView's workstation and
 * in its fonts (fontsel.c: font_sys_push, font_sys_pop).
 */
#include <string.h>
#include "tos.h"
#include "gemui.h"

short scr_w, scr_h;

void fill(GRECT *r, short color)
{
	short pxy[4];

	if (r->w <= 0 || r->h <= 0)
		return;
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = (short) (r->x + r->w - 1);
	pxy[3] = (short) (r->y + r->h - 1);
	vswr_mode(vdi_h, MD_REPLACE);
	vsf_interior(vdi_h, FIS_SOLID);
	vsf_color(vdi_h, color);
	vsf_perimeter(vdi_h, 0);
	vr_recfl(vdi_h, pxy);
}

void line_col(short x1, short y1, short x2, short y2, short color)
{
	short p[4];

	p[0] = x1;
	p[1] = y1;
	p[2] = x2;
	p[3] = y2;
	vswr_mode(vdi_h, MD_REPLACE);
	vsl_color(vdi_h, color);
	v_pline(vdi_h, 2, p);
}

void hline(short x1, short x2, short y)
{
	line_col(x1, y, x2, y, 1);
}

void frame(GRECT *r, short color)
{
	short p[10];

	p[0] = p[6] = p[8] = r->x;
	p[1] = p[3] = p[9] = r->y;
	p[2] = p[4] = (short) (r->x + r->w - 1);
	p[5] = p[7] = (short) (r->y + r->h - 1);
	vswr_mode(vdi_h, MD_REPLACE);
	vsl_color(vdi_h, color);
	v_pline(vdi_h, 5, p);
}

/* text in the current font: bold, white on black, or light for what
   can't be used; at most cols characters */
void text_at(short x, short y, const char *s, long n, short cols, short flags)
{
	if (n > cols)
		n = cols;
	vswr_mode(vdi_h, MD_TRANS);
	vst_color(vdi_h, (flags & TX_INVERSE) ? 0 : 1);
	vst_effects(vdi_h, (flags & TX_BOLD) ? 1 : (flags & TX_DIM) ? 2 : 0);
	while (n > 0)
	{
		short k = n > 120 ? 120 : (short) n;

		v_gtext_n(vdi_h, x, y, s, k);
		x = (short) (x + k * cw);
		s += k;
		n -= k;
	}
	vst_effects(vdi_h, 0);
}

void clip_on(GRECT *r)
{
	short pxy[4];

	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = (short) (r->x + r->w - 1);
	pxy[3] = (short) (r->y + r->h - 1);
	vs_clip(vdi_h, 1, pxy);
}

void clip_off(void)
{
	short pxy[4];

	pxy[0] = 0;
	pxy[1] = 0;
	pxy[2] = (short) (scr_w - 1);
	pxy[3] = (short) (scr_h - 1);
	vs_clip(vdi_h, 0, pxy);
}

/* ST RAM for a bitmap to blit: the screen can't be fed from TT RAM, where
   the program is loaded when there is some */
void *st_ram(long n)
{
	static unsigned short fallback[256];
	static void *buf;

	if (buf == NULL)
	{
		void *p = Mxalloc(sizeof(fallback), 0);

		buf = (long) p > 0 ? p : fallback;
	}
	return n <= (long) sizeof(fallback) ? buf : NULL;
}
