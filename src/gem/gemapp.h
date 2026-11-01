/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - what the viewer window (ehypview.c) and the library window
 * (libwin.c) share.
 */
#ifndef EHV_GEMAPP_H
#define EHV_GEMAPP_H

#include "plat.h"
#include "index.h"
#include "bookmark.h"
#include "text.h"
#include "grect.h"

#define KEY_SCAN(k)  (((unsigned short) (k)) >> 8)
#define KEY_ASCII(k) ((k) & 0xff)
#define K_SHIFT      (K_RSHIFT | K_LSHIFT)

/* ehypview.c */
extern short ap_id, is_acc, quit_now;
extern short vdi_h, cw, ch, planes, ncolors;  /* cw, ch: the text font's cell */
extern short tall_pixels;                 /* twice as tall as wide (ST medium) */
extern short sys_cw, sys_ch;              /* the system font's cell (menus, dialogs) */
extern short ehv_ui_gl_wchar, ehv_ui_gl_hchar;  /* the same, for common/ui (ui.h) */
extern short desk_x, desk_y, desk_w, desk_h;
extern ehv_index *idx;

void log_event(const char *what, const char *a, const char *b);
short link_color(void);
short ui_colour(unsigned short n);        /* pagedraw.c: TOS's VDI colour n among 0-15 now */
void fill(short color, short x1, short y1, short x2, short y2);
/* common's text.c (text.h) on vdi_h: text_n, text_clip/text_noclip
   (every clip set where text is drawn goes through them), and bold and
   underline on plain text just drawn in MD_REPLACE on a white ground,
   where text_fx_fast(fx, mode) says so */
void text_at(short x, short y, const char *s, long n);
/* text_at for plain text (MD_REPLACE, pen 1, no effects set): common's
   text_plain, straight into TOS's own screen where it can, else text_n */
void text_plain_at(short x, short y, const char *s, long n);
void clip_on(const short *c);
void clip_off(void);
void text_fx_after(short x, short y, const char *s, long n, short fx, short colour);
int app_press_box(short win, short x, short y, short w, short h); /* a button pressed: 1 let go over it (5.8) */
int app_press(const GRECT *r, short x, short y, int drag); /* a row pressed: PRESS_IN/OUT/DRAG (click.h) */
void app_update_begin(void);              /* wind_update BEG/END_UPDATE, but not while the */
void app_update_end(void);                /* button is held on TOS (it would wait for the release) */
int intersect(short *x, short *y, short *w, short *h, short x2, short y2, short w2, short h2);
int scanning(void);                       /* the index is being rebuilt */
unsigned long scan_found(void);           /* hypertexts found so far    */
const char *index_busy(void);             /* what the index's job does: "Reading the library..." */

/* show a page in the viewer window (opening it when needed) */
void viewer_show(const char *path, const char *node, unsigned short line);
void viewer_top(void);                    /* bring the viewer to the front */
short viewer_handle(void);                /* -1 when closed */
void app_window_closed(void);             /* quits the program when none is left */
int app_key(short kstate, short kreturn); /* ^O, ^L, ^U, ^W, ^Q...: 1 when handled */
void app_font_changed(void);              /* lay out and redraw both windows */
void app_guide(const char *page);         /* EHYPVIEW.HYP at page (NULL: the front page) */

/* about.c: an iconified window (WM_ICONIFY) drawn as EHypView's icon,
   where the AES asks (w <= 0: all of it) */
void app_icon_redraw(short h, short x, short y, short w, short ht);

/* libwin.c: the library window */
void libwin_open(void);
void libwin_close(void);
void libwin_forget(void);                 /* AC_CLOSE took the window */
short libwin_handle(void);
int libwin_iconified(void);               /* shown as the icon */
int libwin_message(short *msg);           /* 1 when the message was for it */
void libwin_key(short kstate, short kreturn);
void libwin_click(short mx, short my, short button);
void libwin_clip(int what);               /* ^C ^X ^V: CLIP_COPY, ... (clip.h) */
int libwin_idle(void);                    /* 1 while the search or full-text search runs, | 2 when the slice ends (it drew, has drawing due, or had the slice) */
int libwin_busy(void);                    /* the same, without working */
void libwin_index_changed(void);          /* a new index (or progress) */
void libwin_free(void);
void libwin_font_changed(void);
void libwin_bookmarks(const ehv_bookmarks *bm);  /* added or removed: shown again */
void libwin_recent(const ehv_bookmarks *recent); /* the pages read last */
void libwin_layout(int layout);                  /* LIB_OUTLINE, LIB_PANES (LIBRARY=) */
void app_bookmark_remove(int i);         /* Delete in the library, the menu */
/* the library filed these hypertexts (idx->guides) under other subjects:
   kept for this run and EHYPVIEW.IDX written again (ehypview.c) */
void app_index_edited(const unsigned long *g, unsigned long n);
/* the Subjects pane's width in characters (PANECOLS=), and a new one */
int app_pane_cols(void);
void app_pane_cols_set(int cols);

/* ehypview.c: EHYPVIEW.INF, in the boot drive's root (the AERA
   guidelines, 6.1): key=value set, the rest of the file kept; note is the
   ';' line written above a new setting. 0 when written */
int settings_put(const char *key, const char *value, const char *note);

/* font.c: the text font (its dialog: common/ui/fontsel.c) */
/* the font from inf (its text and date, read once); EHYPVIEW.FNT in dir;
   the accessory: GDOS fonts now or not at all */
void font_init(const char *dir, const char *inf, const char *text, const pf_info *info, int is_acc);
void font_check(void);                    /* the accessory: inf changed meanwhile? the font again */
void font_exit(void);
void font_dialog(void);                   /* View > Font... */
int font_step(int dir);                   /* the next size up or down: 1 when changed */
int font_protocol(short id, short point); /* FONT_CHANGED: 1 when taken */
const char *font_name(void);
short font_point(void);
void font_sys_push(void);                 /* the system font, for a dialog */
void font_sys_pop(void);                  /* back to the text font */

/* about.c */
void about_box(void);

/* gemui.c: the drawing the shared GEM building blocks take from EHypView */
extern short ehv_ui_scr_w, ehv_ui_scr_h;
void *ehv_ui_st_ram(long n);           /* st_ram() of gemui.c (ui.h) */

#endif
