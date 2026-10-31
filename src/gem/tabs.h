/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the tab strip under a viewer window's toolbar (tabs, Erez
 * 31 Oct 2026): one tab a page, shown while the window has two or more.
 * Each tab is as wide as its title (at most TABS_TITLE characters); when
 * they don't all fit, they share the strip's width and the titles are
 * cut, "..." after them.
 *
 * Only the layout lives here, with no AES or VDI calls, so
 * tests/test_tabs.c checks it on the host; ehypview.c draws it.
 */
#ifndef EHV_TABS_H
#define EHV_TABS_H

#define TABS_MAX   8                   /* tabs in a window */
#define TABS_TITLE 24                  /* a title's characters at the most */

typedef struct {
	short x, y, w, h;                  /* the strip */
	short n;                           /* its tabs */
	short tx[TABS_MAX], tw[TABS_MAX];  /* each tab: its left edge and width */
	short shown[TABS_MAX];             /* its title's characters shown */
	short dots[TABS_MAX];              /* cut: 1 with "..." after, 2 too narrow for it */
} tabstrip;

/* the strip's height for the system font's cell height ch */
short tabs_height(short ch);

/* n tabs (at most TABS_MAX) on a strip at (x, y), w wide, for the system
   font's cell cw x ch; len[i] is tab i's title length */
void tabs_layout(tabstrip *s, short x, short y, short w, short cw, short ch, int n, const short *len);

/* the tab at (mx, my), or -1 */
int tabs_hit(const tabstrip *s, short mx, short my);

/* 1 when tab i's title is cut (its tooltip gives it whole) */
int tabs_cut(const tabstrip *s, int i);

#endif
