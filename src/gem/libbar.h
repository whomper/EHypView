/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the Subjects pane's scroll bar (libwin.c), the family's
 * pane bar of AES objects (common/ui/panebar.c, the guidelines 5.1).
 * libwin.c has drawing of its own named as gemui.h's, so the bar is
 * kept here and given in plain numbers.
 */
#ifndef EHV_LIBBAR_H
#define EHV_LIBBAR_H

/* the bar's width: the AES's slider column */
short libbar_width(void);

/* where it is and what it shows: the lines there are, on show, the first */
void libbar_set(short x, short y, short w, short h, long total, long shown, long top);

/* drawn within the rectangle (wind_update held, the mouse off); 0 when
   the bar is too low to draw */
int libbar_draw(short cx, short cy, short cw, short ch);

/* where it is */
void libbar_rect(short *x, short *y, short *w, short *h);

/* 1 when x, y is on it */
int libbar_hit(short x, short y);

/* a press on it, the button down: scroll(n) and goto_top(t) move the pane
   (and call libbar_set); returns when the button is up */
void libbar_click(short x, short y, int (*scroll)(long n), int (*goto_top)(long t));

/* the part of it to draw again after the first line moved from old_top */
void libbar_moved(long old_top, short *x, short *y, short *w, short *h);

#endif
