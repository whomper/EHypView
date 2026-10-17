/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - a page laid out and drawn on the character grid of the text
 * font, as ST-Guide does: where each line and its graphics go, the
 * images converted for the screen, and the drawing.  The viewer window
 * keeps one layout for its page, a pop-up another for its own.
 */
#ifndef EHV_PAGEDRAW_H
#define EHV_PAGEDRAW_H

#include "gem.h"
#include "hypfile.h"
#include "page.h"

typedef struct {
	short w, h, planes;
	MFDB form;                    /* device format, or 1 plane mono */
	int mono;                     /* draw with vrt_cpyfm            */
} gimage;

typedef struct {
	const hyp_page *pg;           /* NULL: nothing laid out          */
	long *line_y;                 /* top of text line l, page pixels */
	long *gfx_y;                  /* top of the graphics of line l   */
	long *gfx_at;                 /* top of graphic i                */
	long height;                  /* below the last line             */
	gimage *images;               /* one per page->gfx entry         */
	unsigned short nimages;
} playout;

/* lay out `pg` of `doc` for the current text font (cw, ch) */
void layout_make(playout *L, hyp_doc *doc, const hyp_page *pg);
void layout_free(playout *L);

/*
 * Draw the page with its origin (page pixel 0, 0) at screen (ox, oy),
 * lines from `top` on, within the clip rectangle (cx, cy, cw_, ch_),
 * which the caller set.  `sel` is the selected link run (-1: none);
 * `cols` the width centred images are centred in.
 */
void layout_draw(const playout *L, short ox, long oy, long top,
                 short cx, short cy, short cw_, short ch_, long sel, long cols);

/* the line at page pixel row py, searching from `top`; -1 below the text */
long layout_line_at(const playout *L, long py, long top);

/* the page's size in pixels, its graphics included, for `cols` columns */
long layout_width(const playout *L, long cols);
long layout_height(const playout *L);

#endif
