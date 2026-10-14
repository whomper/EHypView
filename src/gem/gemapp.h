/*
 * EHypView - what the viewer window (ehypview.c) and the library window
 * (libwin.c) share.
 *
 * Copyright (C) the EHypView authors.  Free software under the GNU
 * General Public License, version 2 or (at your option) any later
 * version; see COPYING.
 */
#ifndef EHV_GEMAPP_H
#define EHV_GEMAPP_H

#include "index.h"

#define KEY_SCAN(k)  (((unsigned short) (k)) >> 8)
#define KEY_ASCII(k) ((k) & 0xff)
#define K_SHIFT      (K_RSHIFT | K_LSHIFT)

/* ehypview.c */
extern short ap_id, is_acc, quit_now;
extern short vdi_h, cw, ch, planes, ncolors;
extern short desk_x, desk_y, desk_w, desk_h;
extern ehv_index *idx;

void log_event(const char *what, const char *a, const char *b);
short link_color(void);
void fill(short color, short x1, short y1, short x2, short y2);
void text_at(short x, short y, const char *s, long n);
int intersect(short *x, short *y, short *w, short *h, short x2, short y2, short w2, short h2);
int scanning(void);                       /* the index is being rebuilt */
unsigned long scan_found(void);           /* hypertexts found so far    */

/* show a page in the viewer window (opening it when needed) */
void viewer_show(const char *path, const char *node, unsigned short line);
void viewer_top(void);                    /* bring the viewer to the front */
short viewer_handle(void);                /* -1 when closed */
void app_window_closed(void);             /* quits the program when none is left */

/* libwin.c: the library window */
void libwin_open(void);
void libwin_close(void);
void libwin_forget(void);                 /* AC_CLOSE took the window */
short libwin_handle(void);
int libwin_message(short *msg);           /* 1 when the message was for it */
void libwin_key(short kstate, short kreturn);
void libwin_click(short mx, short my);
int libwin_idle(void);                    /* 1 while the full-text search runs */
void libwin_index_changed(void);          /* a new index (or progress) */
void libwin_free(void);

#endif
