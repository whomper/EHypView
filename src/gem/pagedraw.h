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
	void *job;                    /* still being made (layout_work) */
} gimage;

typedef struct {
	const hyp_page *pg;           /* NULL: nothing laid out          */
	long *line_y;                 /* top of text line l, page pixels */
	long *gfx_y;                  /* top of the graphics of line l   */
	long *gfx_at;                 /* top of graphic i                */
	long height;                  /* below the last line             */
	gimage *images;               /* one per page->gfx entry         */
	unsigned short nimages;
	char *path;                   /* the hypertext's (pictures kept)  */
} playout;

/* lay out `pg` of `doc` for the current text font (cw, ch).  Its
   pictures are made afterwards, a piece at a time (layout_work; the
   guidelines' Speed), their places held: ready at once only when kept
   from before.  layout_free keeps the last pages' pictures for going
   back; layout_forget_kept lets them go. */
void layout_make(playout *L, hyp_doc *doc, const hyp_page *pg);
void layout_free(playout *L);
void layout_forget_kept(void);

/* pictures still to be made; the next piece of them (1 more to come,
   *done the picture just finished, to be drawn, or -1); all of them now */
int layout_busy(const playout *L);
int layout_work(playout *L, long *done);
void layout_finish(playout *L);

/* where picture i is on the screen with the page's origin at (ox, oy),
   centred ones in `cols` columns: 0 when it isn't one shown */
int layout_picture_at(const playout *L, unsigned short i, short ox, long oy, long cols,
                      long *x, long *y, short *w, short *h);

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
