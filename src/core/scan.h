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
 * file) are copied from it instead of being opened again.
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

/* the whole scan in one call */
ehv_index *ehv_scan_all(const char *const *roots, unsigned int nroots,
                        const ehv_index *previous, const ref_db *allref);

/*
 * The folders to scan for an existing ST-Guide setup: PATHS, HYPFOLD and
 * the folder of REF from ST-GUIDE.INF (inf may be NULL), then the usual
 * places *:\GUIDES and *:\GEMSYS\GUIDES.  Duplicates are removed.
 * Free with inf_free_list().
 */
unsigned int ehv_default_roots(const inf_file *inf, char ***roots);

#endif
