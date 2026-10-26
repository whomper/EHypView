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
 * as the search runs.  Bookmarks come first, all of them with an empty
 * search, the ones whose title or page matches while typing.  No GEM
 * here; src/gem/libwin.c draws it.
 *
 * Two layouts (LIBRARY= in EHYPVIEW.INF, Settings):
 *
 *   Outline    the pages read last (LIB_RECENT, drawn as a strip),
 *              then the groups, each folded or unfolded from its
 *              heading, with the number of hypertexts in it;
 *   Two panes  the Subjects on the left (Bookmarks, Recently read,
 *              All hypertexts, each subject), the one chosen on the
 *              right: the rows are only that one's.
 *
 * Each subject has a tint (1, 2, ... in the order shown; 0: none), the
 * colour the window gives its square and its hypertexts' books.
 */
#ifndef EHV_LIBRARY_H
#define EHV_LIBRARY_H

#include "index.h"
#include "search.h"
#include "bookmark.h"

/* row kinds */
#define LIB_GROUP   0     /* heading: subject, "Pages", ...    */
#define LIB_GUIDE   1     /* a hypertext                        */
#define LIB_PAGE    2     /* a page name, alias or keyword      */
#define LIB_ACTION  3     /* "Search the full text for ..."     */
#define LIB_TEXT    4     /* a full-text hit                    */
#define LIB_NOTE    5     /* a message                          */
#define LIB_BOOKMARK 6    /* a bookmark                         */
#define LIB_RECENT  7     /* a page read lately                 */
#define LIB_WAIT    8     /* "Searching the names...": more may come */

/* layouts */
#define LIB_OUTLINE 0
#define LIB_PANES   1

/* a heading's flags */
#define LIB_FOLDS   1     /* can be folded (Outline, no search) */
#define LIB_FOLDED  2     /* folded: its rows left out          */
#define LIB_SUB     4     /* a part of a subject ("Personal/Calendar":
                             Calendar, under Personal)          */

#define LIB_QUERY_MAX 48

typedef struct {
	unsigned char kind;
	unsigned char flags;  /* GROUP: LIB_FOLDS, LIB_FOLDED          */
	unsigned char tint;   /* GROUP, GUIDE: the subject's tint      */
	unsigned long ref;    /* GUIDE: guide; PAGE: in found; TEXT: hit; BOOKMARK,
	                         RECENT: its index; GROUP: the rows under it */
	const char *text;     /* GROUP: heading (static or index string) */
} lib_row;

/* a line of the Subjects pane */
#define PANE_BOOKMARKS 0
#define PANE_RECENT    1
#define PANE_ALL       2
#define PANE_SUBJECT   3
#define PANE_OTHER     4  /* hypertexts without a subject */

/*
 * A subject with a "/" ("Personal/Calendar", as ST-Guide's hypertexts
 * give them) is a part of the one before it: the pane lists Personal,
 * with each part under it (level 1), and the Outline the same, a heading
 * for each part.  A subject is matched in any case.  "Documentation/"
 * and "Dokumentation/" are left out first (lib_subject): "Documentation/
 * Editors" is Editors.
 */
#define LIB_PANE_TEXT 40

typedef struct {
	unsigned char kind, tint;
	unsigned char level;  /* SUBJECT: 1, a part of the subject before */
	unsigned long count;
	const char *subj;     /* SUBJECT: one of its hypertexts' @subject */
	char text[LIB_PANE_TEXT];        /* what it is called */
} lib_pane;

#define LIB_FOLD_MAX 32

typedef struct {
	const ehv_index *idx;
	const ehv_bookmarks *bm;    /* may be NULL; not owned       */
	char query[LIB_QUERY_MAX];
	lib_row *rows;
	unsigned long nrows, cap;
	long sel;             /* selected row, -1 = none            */
	long top;             /* first visible row                  */
	ehv_fts *fts;         /* full-text search, NULL = none       */
	ehv_hits found;       /* the names the search found (PAGE rows) */
	char found_q[LIB_QUERY_MAX];     /* for this search, */
	const ehv_index *found_of;       /* in this index (NULL: none) */
	/* the names searched through: those starting with the query found
	   at once, the rest in slices (lib_search_step) */
	ehv_names scan;
	int scanning;         /* 1: lib_search_step() has names to go */
	unsigned long plain;  /* names the same as, starting with or containing it */
	ehv_hits rank[4];     /* the same, starting with, containing, similar */
	unsigned long npages, nguides;   /* matches in the last rebuild */
	char note[80];        /* text of the NOTE row               */

	int layout;           /* LIB_OUTLINE, LIB_PANES             */
	const ehv_bookmarks *recent;     /* pages read last; may be NULL */
	unsigned long *order; /* readable hypertexts, as listed     */
	unsigned long norder;
	unsigned char *tint;  /* each hypertext's subject's tint    */
	struct lib_gkey *keys;           /* each one's subject, worked out once */
	const ehv_index *order_of;       /* the index order was made for */
	lib_pane *panes;      /* the Subjects pane                  */
	unsigned long npanes, cap_panes;
	long pane;            /* the one chosen                     */
	unsigned char pane_kind;         /* it, kept over a new index      */
	char pane_name[64];
	unsigned long folded[LIB_FOLD_MAX];   /* the headings folded, hashed */
	unsigned int nfolded;
} ehv_library;

void lib_init(ehv_library *lib, const ehv_index *idx);
void lib_free(ehv_library *lib);

/* a new index (after a scan): rebuilds, stops a full-text search */
int lib_set_index(ehv_library *lib, const ehv_index *idx);

/* new bookmarks (added or removed): rebuilds */
int lib_set_bookmarks(ehv_library *lib, const ehv_bookmarks *bm);

/* new pages read lately (most recent first): rebuilds */
int lib_set_recent(ehv_library *lib, const ehv_bookmarks *recent);

/* LIB_OUTLINE or LIB_PANES: rebuilds */
int lib_set_layout(ehv_library *lib, int layout);

/* Outline: fold (0), unfold (1) or turn (-1) the chosen heading, or the
   group of the chosen row (folded, the heading is chosen); 1 when it
   changed */
int lib_fold(ehv_library *lib, int how);

/* Two panes: choose line i of the Subjects pane, or move by delta;
   the rows follow.  The one chosen */
long lib_pane_select(ehv_library *lib, long i);
long lib_pane_move(ehv_library *lib, long delta);

/* the hypertext a row is about (GUIDE, or BOOKMARK and RECENT when the
   index has it), or NULL */
const ehv_guide *lib_row_guide(const ehv_library *lib, unsigned long row);

/* the bookmark the selected row is, or -1 */
int lib_selected_bookmark(const ehv_library *lib);

/* change the search text; rebuilds the list */
int lib_set_query(ehv_library *lib, const char *query);
/* the search through the names on for at most `budget` names (the
   rows rebuilt when it found more): 1 done (or none going), 2 more and
   the rows changed, 0 more, -1 failed */
int lib_search_step(ehv_library *lib, unsigned int budget);

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

/* a hypertext's second line: author and version, else its folder */
void lib_row_about(const ehv_library *lib, unsigned long row, char *buf, unsigned long size);

/* a version as shown: "$VER:" and the file name left out */
const char *lib_version(const char *v);

/* a subject as shown: "Personal \xfa Calendar" for "Personal/Calendar" */
void lib_subject_text(const char *subj, char *buf, unsigned long size);
/* the subject a hypertext is filed under: past "Documentation/" or
   "Dokumentation/" when a part follows; NULL for none */
const char *lib_subject(const char *subj);

#endif
