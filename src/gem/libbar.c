/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the Subjects pane's scroll bar (libbar.h).
 */
#include "tos.h"
#include "panebar.h"
#include "libbar.h"

static PANEBAR bar;

short libbar_width(void)
{
	return pb_width();
}

void libbar_set(short x, short y, short w, short h, long total, long shown, long top)
{
	bar.r.x = x;
	bar.r.y = y;
	bar.r.w = w;
	bar.r.h = h;
	bar.total = total;
	bar.shown = shown;
	bar.top = top;
}

int libbar_draw(short cx, short cy, short cw_, short ch_)
{
	GRECT c;

	if (bar.r.h < 2 * pb_arrow_h() + 4)
		return 0;                     /* (pb_draw's own limit) */
	c.x = cx;
	c.y = cy;
	c.w = cw_;
	c.h = ch_;
	pb_draw(&bar, &c);
	return 1;
}

void libbar_rect(short *x, short *y, short *w, short *h)
{
	*x = bar.r.x;
	*y = bar.r.y;
	*w = bar.r.w;
	*h = bar.r.h;
}

int libbar_hit(short x, short y)
{
	return x >= bar.r.x && x < bar.r.x + bar.r.w && y >= bar.r.y && y < bar.r.y + bar.r.h;
}

void libbar_click(short x, short y, int (*scroll)(long n), int (*goto_top)(long t))
{
	pb_click(&bar, x, y, scroll, goto_top);
}

void libbar_moved(long old_top, short *x, short *y, short *w, short *h)
{
	GRECT r = pb_moved(&bar, old_top);

	*x = r.x;
	*y = r.y;
	*w = r.w;
	*h = r.h;
}
