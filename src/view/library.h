/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the library: every hypertext found, and one search box.
 *
 * With an empty search the list shows every readable hypertext, grouped
 * by its @subject ("Programming/GEM" etc.).  While typing it shows the
 * page names, aliases and keywords that match (exact, then starting
 * with, then containing, then similar), the hypertexts whose title
 * matches, and an action to search the full text, whose hits follow
 * as the search runs.  No GEM here; src/gem/libwin.c draws it.
 */
#ifndef EHV_LIBRARY_H
#define EHV_LIBRARY_H

#include "index.h"
#include "search.h"

/* row kinds */
#define LIB_GROUP   0     /* heading: subject, "Pages", ...    */
#define LIB_GUIDE   1     /* a hypertext                        */
#define LIB_PAGE    2     /* a page name, alias or keyword      */
#define LIB_ACTION  3     /* "Search the full text for ..."     */
#define LIB_TEXT    4     /* a full-text hit                    */
#define LIB_NOTE    5     /* a message                          */

#define LIB_QUERY_MAX 48

typedef struct {
	unsigned char kind;
	unsigned long ref;    /* GUIDE: guide; PAGE: symbol; TEXT: hit */
	const char *text;     /* GROUP: heading (static or index string) */
} lib_row;

typedef struct {
	const ehv_index *idx;
	char query[LIB_QUERY_MAX];
	lib_row *rows;
	unsigned long nrows, cap;
	long sel;             /* selected row, -1 = none            */
	long top;             /* first visible row                  */
	ehv_fts *fts;         /* full-text search, NULL = none       */
	unsigned long npages, nguides;   /* matches in the last rebuild */
	char note[80];        /* text of the NOTE row               */
} ehv_library;

void lib_init(ehv_library *lib, const ehv_index *idx);
void lib_free(ehv_library *lib);

/* a new index (after a scan): rebuilds, stops a full-text search */
int lib_set_index(ehv_library *lib, const ehv_index *idx);

/* change the search text; rebuilds the list */
int lib_set_query(ehv_library *lib, const char *query);

/* rebuild the list for the current query */
int lib_rebuild(ehv_library *lib);

/* move the selection over selectable rows; returns the new selection */
long lib_move(ehv_library *lib, long delta);

/* select the selectable row at or after `row` (clicks) */
long lib_select(ehv_library *lib, long row);

/* what the selected row leads to */
#define LIB_OPEN      1   /* open *path at *node (NULL: main page), *line */
#define LIB_FULLTEXT  2   /* start the full-text search             */
#define LIB_NOTHING   0
int lib_activate(ehv_library *lib, const char **path, const char **node, unsigned short *line);

/* start the full-text search for the current query */
int lib_fulltext_start(ehv_library *lib);

/* continue it; 1 = finished (or none running), 0 = more, -1 = out of memory */
int lib_fulltext_step(ehv_library *lib, unsigned int budget);

/* text of a row for display */
void lib_row_text(const ehv_library *lib, unsigned long row, char *buf, unsigned long size);

/* text on the right of a row (file name, hypertext), "" if none */
void lib_row_detail(const ehv_library *lib, unsigned long row, char *buf, unsigned long size);

#endif
