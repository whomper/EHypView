/*
 * EHypView - full-text search through every indexed hypertext.
 *
 * ST-Guide needed the separate HypFind tool for this, which wrote its
 * results into a new hypertext.  Here the search runs in small steps
 * (one page per unit) so that a GEM program stays responsive, and the
 * hits are a plain list.
 *
 *     ehv_fts *s = ehv_fts_begin(idx, "word");
 *     while (ehv_fts_step(s, 8) == 0)
 *         ... handle events ...
 *
 * Matching ignores case (Atari character set).  Each page is reported
 * once, at its first matching line, with the number of matching lines.
 *
 * Copyright (C) the EHypView authors.  Free software under the GNU
 * General Public License, version 2 or (at your option) any later
 * version; see COPYING.
 */
#ifndef EHV_SEARCH_H
#define EHV_SEARCH_H

#include "index.h"

#define EHV_FTS_MAX_HITS 500

typedef struct {
	unsigned long guide;          /* index entry of the hypertext */
	char *node;                   /* page name                    */
	unsigned short line;          /* first matching line          */
	unsigned short count;         /* matching lines on the page   */
	char context[64];             /* the text around the match    */
} ehv_fts_hit;

typedef struct ehv_fts ehv_fts;

/* NULL when out of memory or the query is empty; idx must stay valid */
ehv_fts *ehv_fts_begin(const ehv_index *idx, const char *query);

/* search up to `budget` pages: 1 = finished, 0 = more to do, -1 = out of memory */
int ehv_fts_step(ehv_fts *s, unsigned int budget);

const ehv_fts_hit *ehv_fts_hits(const ehv_fts *s, unsigned long *count);
unsigned long ehv_fts_pages(const ehv_fts *s);     /* pages searched so far */
int ehv_fts_done(const ehv_fts *s);
const char *ehv_fts_query(const ehv_fts *s);

void ehv_fts_free(ehv_fts *s);

#endif
