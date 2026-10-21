/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - find every hypertext below a list of folders.
 *
 * The scan runs in small steps so that a GEM program (or accessory) can
 * keep handling events in between:
 *
 *     ehv_scan *s = ehv_scan_begin(roots, n, previous);
 *     while (ehv_scan_step(s, 8) == 0)
 *         ... handle events, show ehv_scan_status(s) ...
 *     idx = ehv_scan_finish(s, allref);
 *
 * One unit of work is reading one directory or indexing one file.  Files
 * that are unchanged since `previous` (same path, size, date, same .REF
 * file) are taken from it instead of being opened again: their names
 * from its blocks, when the names are sorted.
 */
#ifndef EHV_SCAN_H
#define EHV_SCAN_H

#include "index.h"
#include "inf.h"

typedef struct ehv_scan ehv_scan;

typedef struct {
	unsigned long dirs;       /* directories read             */
	unsigned long opened;     /* hypertexts read from disk    */
	unsigned long reused;     /* hypertexts taken from cache  */
	unsigned long failed;     /* unreadable or damaged files  */
	const char *current;      /* directory or file in progress */
} ehv_scan_status;

#define EHV_SCAN_MAX_DEPTH 16

/*
 * Start a scan.  Roots are searched in order; a folder reachable from
 * several roots is scanned once, under the first.  "*:\X" means X on
 * every hard disk (Atari only).  `previous` may be NULL and must stay
 * valid until ehv_scan_finish().
 */
ehv_scan *ehv_scan_begin(const char *const *roots, unsigned int nroots, const ehv_index *previous);

/*
 * The same, but checking first: while every hypertext found is the next
 * one of `previous`, unchanged, nothing is copied.  At the first
 * difference the scan starts again and builds the index.  When nothing
 * changed, ehv_scan_changed() says so and the scan's own index is empty:
 * keep `previous`.
 */
ehv_scan *ehv_scan_begin_check(const char *const *roots, unsigned int nroots, const ehv_index *previous);

/* do up to `budget` units; 1 = finished, 0 = more to do, -1 = out of memory */
int ehv_scan_step(ehv_scan *scan, unsigned int budget);

const ehv_scan_status *ehv_scan_status_get(const ehv_scan *scan);

/*
 * End the scan and return the new index (NULL when out of memory).
 * `allref` (may be NULL) supplies aliases for guides without their own
 * .REF file.  The scanner is freed either way.
 */
ehv_index *ehv_scan_finish(ehv_scan *scan, const ref_db *allref);

void ehv_scan_abort(ehv_scan *scan);

/*
 * After the last step: whether the index differs from `previous` (a
 * hypertext read again, new, gone or in another place in the list, or
 * `previous` came from an old cache file).  When it doesn't, `previous`
 * may simply be kept: ALL.REF aside, it is what the scan would build.
 */
int ehv_scan_changed(const ehv_scan *scan);

/* where the index built sorts its names: beside the cache file
   (ehv_index_build_to()); call after ehv_scan_begin*() */
int ehv_scan_build_to(ehv_scan *scan, const char *cache);
int ehv_index_build_busy_scan(const ehv_scan *scan);   /* its names being put away */
const char *ehv_scan_next_what(const ehv_scan *scan); /* the next step, in words (a profile) */

/* the next step opens a file or reads a folder: slower than the others,
   so a program may begin a slice of its own with it */
int ehv_scan_next_is_big(const ehv_scan *scan);

/* end the scan and return its index as found, not yet sorted (for
   ehv_index_merge_ref_some() and ehv_index_finish_some(); `previous`
   must stay until the names are sorted) */
ehv_index *ehv_scan_take(ehv_scan *scan);

/* the whole scan in one call */
ehv_index *ehv_scan_all(const char *const *roots, unsigned int nroots,
                        const ehv_index *previous, const ref_db *allref);

/*
 * The folders to scan: own, EHypView's PATHS from EHYPVIEW.INF (a list as
 * inf_split_list() takes, may be NULL), then for an existing ST-Guide
 * setup PATHS, HYPFOLD and the folder of REF from ST-GUIDE.INF (inf may
 * be NULL), then the usual places *:\GUIDES and *:\GEMSYS\GUIDES.
 * Duplicates are removed.  Free with inf_free_list().
 */
unsigned int ehv_default_roots(const inf_file *inf, const char *own, char ***roots);

#endif
