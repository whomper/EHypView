/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - what a viewer window shows and how it moves: the open
 * hypertext and page, the scroll position, the selected link, the
 * history and an open pop-up.  No GEM here, so it is tested on the host;
 * src/gem draws it.
 */
#ifndef EHV_VIEW_H
#define EHV_VIEW_H

#include "hypfile.h"
#include "page.h"
#include "index.h"

/* results */
#define VIEW_OK      0
#define VIEW_CLOSE   1           /* a "quit" link: close the window */
#define VIEW_POPUP   2           /* a pop-up opened: see view->popup */
#define VIEW_ERROR  -1           /* nothing changed; see view->status */

/* view_relative() */
#define VIEW_NEXT    0           /* following page               */
#define VIEW_PREV    1           /* preceding page               */
#define VIEW_TOC     2           /* table of contents ("up")     */
#define VIEW_INDEX   3           /* the page called "Index"      */
#define VIEW_HELP    4           /* @help page                   */
#define VIEW_HOME    5           /* default page                 */
#define VIEW_BACK    6           /* view_can() only: the history */

typedef struct {
	char *path;
	unsigned short node;
	long top;
} view_hist;

typedef struct {
	const ehv_index *idx;        /* may be NULL; not owned       */
	hyp_doc *doc;
	hyp_page *page;              /* NULL when nothing is open    */
	long top, left;              /* first visible line / column  */
	long sel;                    /* selected link (index into page->runs), -1 */
	view_hist *hist;
	unsigned long nhist, cap_hist;
	char status[200];            /* message for the info line    */
	/*
	 * A link to a @pnode opens it as a pop-up, as ST-Guide does: a small
	 * box over the page, which stays as it is (nothing goes onto the
	 * history).  Any page change closes it.
	 */
	hyp_page *popup;             /* NULL when none is open       */
	unsigned short popup_node;   /* its index entry              */
} ehv_view;

void view_init(ehv_view *v, const ehv_index *idx);
void view_free(ehv_view *v);

/*
 * Open `path` at page `node` (NULL: the default page), scrolled to
 * `line`.  The current page goes onto the history.
 */
int view_open(ehv_view *v, const char *path, const char *node, unsigned short line);

/* a help request (VA_START, AC_HELP, command line); see resolve.h */
int view_request(ehv_view *v, const char *args);

/* show index entry `entry` of the open hypertext */
int view_goto(ehv_view *v, unsigned short entry, unsigned short line);

/* follow a link run of the current page */
int view_follow(ehv_view *v, const hyp_run *run);

/* follow the selected link */
int view_follow_selected(ehv_view *v);

/* close the pop-up (when one is open) */
void view_popup_close(ehv_view *v);

/* the pop-up's link at (line, column), or -1 */
long view_popup_link_at(const ehv_view *v, long line, long col);

/* follow link run `r` of the pop-up: the pop-up closes first */
int view_popup_follow(ehv_view *v, long r);

/* the pop-up's width in columns: its text without trailing blanks, and
   its lines and boxes (images are measured by the GEM side) */
long view_popup_cols(const ehv_view *v);

/* back to the previous page; VIEW_ERROR when there is none */
int view_back(ehv_view *v);

/* VIEW_NEXT, VIEW_PREV, ... */
int view_relative(ehv_view *v, int which);

/* nonzero when view_relative(which), or view_back for VIEW_BACK, would work */
int view_can(const ehv_view *v, int which);

/*
 * Select the next (dir > 0) or previous link.  The search starts at the
 * selection, or at the first/last visible line [first, last] when the
 * selection is not visible.  Returns the line of the new selection, or
 * -1 when there is no further link.
 */
long view_select_link(ehv_view *v, int dir, long first, long last);

/* the link at (line, column), or -1 */
long view_link_at(const ehv_view *v, long line, long col);

/* line of run `r` */
long view_run_line(const ehv_view *v, long r);

/* the page's text, a line each ending '\n', its links as their text
   (^C, the clipboard); at most size - 1 characters and a NUL.  Returns
   how many were written */
unsigned long view_page_text(const ehv_view *v, char *buf, unsigned long size);

/* "database - page title" for the window name */
void view_title(const ehv_view *v, char *buf, unsigned long size);

/* nonzero when a page is shown */
int view_has_page(const ehv_view *v);

#endif
