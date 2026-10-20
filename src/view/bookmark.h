/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - bookmarks: pages kept to come back to.
 *
 * Each is a hypertext's path, a page name and a title to show.  They are
 * kept in EHYPVIEW.INF, one per line, BOOKMARK1 to BOOKMARK10:
 *
 *   BOOKMARK1=C:\GUIDES\TOS.HYP|Fsfirst|TOS - Fsfirst
 *
 * (path, page, title: GEMDOS names can't hold a '|').  An empty value is
 * a removed one.  No GEM here, so it is tested on the host.
 *
 * The pages read last (the library's Continue, Recently read) are kept
 * the same way, RECENT1 to RECENT5, newest first: one a hypertext.
 */
#ifndef EHV_BOOKMARK_H
#define EHV_BOOKMARK_H

#include "inf.h"

#define BM_MAX       10
#define BM_TITLE_MAX 48           /* a title is cut to this many characters */

typedef struct {
	char *path, *node, *title;
} ehv_bookmark;

typedef struct {
	ehv_bookmark b[BM_MAX];
	unsigned int n;
} ehv_bookmarks;

void bm_init(ehv_bookmarks *bm);
void bm_free(ehv_bookmarks *bm);

/* the bookmark of path (any case) and node, or -1 */
int bm_find(const ehv_bookmarks *bm, const char *path, const char *node);

/*
 * Add a bookmark at the end.  The index of the new one, or of the one
 * already there for the same page (its title is not changed); -1 when
 * all BM_MAX are taken, -2 when out of memory.
 */
int bm_add(ehv_bookmarks *bm, const char *path, const char *node, const char *title);

/* remove bookmark i; the ones after it move up.  0, or -1 for no such */
int bm_remove(ehv_bookmarks *bm, unsigned int i);

/* the bookmarks in an EHYPVIEW.INF (none when inf is NULL) */
void bm_read(ehv_bookmarks *bm, const inf_file *inf);

/* "BOOKMARKn" for slot i (0-based) */
void bm_key(unsigned int i, char *buf, unsigned long size);

/* the same for another list: prefix "RECENT", at most max of them */
void bm_read_as(ehv_bookmarks *bm, const inf_file *inf, const char *prefix, unsigned int max);
void bm_key_as(const char *prefix, unsigned int i, char *buf, unsigned long size);

/*
 * A page read: first in the list, in place of the one of the same
 * hypertext (any case), the last one dropped beyond max.  1 when the
 * list changed, 0 when it was first already, -1 when it can't be kept.
 */
int bm_touch(ehv_bookmarks *bm, const char *path, const char *node, const char *title, unsigned int max);

/* the value for slot i: "path|node|title", or "" past the last one.
   The length, or 0 when it doesn't fit */
unsigned long bm_value(const ehv_bookmarks *bm, unsigned int i, char *buf, unsigned long size);

#endif
