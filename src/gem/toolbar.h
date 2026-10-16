/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the viewer window's toolbar: its buttons, where they go and
 * what they do.  ST-Guide's button row (Back, the page before and after,
 * Index, Catalog, See also), in the AERA family's look (ECal's toolbar):
 *
 *   [<-][->][Home]  [<][Contents][>]  [Index][See also]  [Library][Bookmarks]  [=]
 *
 * Only the layout lives here, with no AES or VDI calls, so
 * tests/test_toolbar.c checks it on the host; ehypview.c draws it.
 */
#ifndef EHV_TOOLBAR_H
#define EHV_TOOLBAR_H

enum {
	TB_BACK,                       /* right button: the history */
	TB_FORWARD,                    /* right button: the history */
	TB_HOME,                       /* the main page */
	TB_PREV,
	TB_TOC,
	TB_NEXT,
	TB_INDEX,
	TB_SEEALSO,                    /* a menu of the page's @xref */
	TB_LIBRARY,                    /* ST-Guide's catalog */
	TB_BOOKMARKS,                  /* a menu: add, remove, the bookmarks */
	TB_MENU,                       /* the menu bar's commands, as a menu:
	                                  how the accessory reaches them */
	TB_COUNT
};

/* how a button looks */
#define TB_ARROW_LEFT  1               /* drawn, not text */
#define TB_ARROW_RIGHT 2
#define TB_HOUSE       3

typedef struct {
	short x, y, w, h;                  /* the bar */
	short bx[TB_COUNT], bw[TB_COUNT];  /* each button; bw 0: no room for it */
	short by, bh;                      /* the buttons' top and height */
} toolbar;

/* the bar's height for the system font's cell height ch */
short tb_height(short ch);

/*
 * Lay the buttons out on a bar at (x, y), w wide, for the system font's
 * cell cw x ch.  When they don't all fit, the least needed go first
 * (See also, Index, Bookmarks, ...); Back, Forward and the menu always
 * stay, and the menu has the rest.
 */
void tb_layout(toolbar *t, short x, short y, short w, short cw, short ch);

/* how wide a bar takes every button (a new window is made that wide) */
short tb_full_width(short cw);

/* the button at (mx, my), or -1 */
int tb_hit(const toolbar *t, short mx, short my);

const char *tb_label(int b);           /* "" for a drawn one */
int tb_glyph(int b);                   /* TB_ARROW_LEFT ..., 0 for text */
const char *tb_tip(int b);             /* the tooltip */
const char *tb_name(int b);            /* "back", ... for the log */

#endif
