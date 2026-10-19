/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - index of all hypertexts.
 *
 * ST-Guide finds pages only through the folders listed in PATHS and the
 * hand-maintained ALL.REF.  EHypView instead scans folders including
 * all subfolders (scan.h) and builds this index from the HYP files
 * themselves:
 *
 *   - one record per hypertext: path, @database, @subject, author, ...
 *   - every page name (from the file's own index table),
 *   - aliases, labels and @keywords from a .REF file next to the HYP,
 *     or from ALL.REF when there is none.
 *
 * The index is saved to a cache file so that EHypView starts instantly;
 * a rescan then re-reads only the files that changed.
 */
#ifndef EHV_INDEX_H
#define EHV_INDEX_H

#include "pool.h"
#include "ref.h"

/* symbol kinds */
#define EHV_SYM_NODE   0      /* @node                      */
#define EHV_SYM_POPUP  1      /* @pnode                     */
#define EHV_SYM_ALIAS  2      /* @alias of `node`           */
#define EHV_SYM_LABEL  3      /* @label/@keywords in `node` */

typedef struct {
	unsigned long guide;      /* index into ehv_index.guides */
	unsigned char kind;       /* EHV_SYM_*                   */
	unsigned char from_ref;   /* 0 HYP, 1 own .REF, 2 ALL.REF */
	unsigned short line;      /* labels: line in the page    */
	const char *name;
	const char *node;         /* the page it leads to        */
} ehv_sym;

typedef struct {
	const char *path;         /* full path as found on disk  */
	const char *key;          /* file name without ".hyp"    */
	unsigned long size, mtime;
	const char *ref_path;     /* .REF file next to it, or NULL */
	unsigned long ref_size, ref_mtime;
	unsigned short root;      /* scan root that found it (0 = first) */
	int error;                /* HYP_OK, or why it could not be read */

	const char *database, *subject, *author, *version;
	const char *hostname, *default_node, *help_node;
	const char *charset, *language;
	unsigned char os;
	unsigned short width, stg_flags;

	unsigned long first_sym, nsyms;   /* range in ehv_index.syms */
} ehv_guide;

typedef struct {
	ehv_pool pool;            /* all strings                 */
	ehv_guide *guides;
	unsigned long nguides, cap_guides;
	ehv_sym *syms;
	unsigned long nsyms, cap_syms;

	/* lookup tables, built by ehv_index_finish() */
	unsigned long *by_key;    /* guides sorted by key        */
	unsigned long *by_path;   /* guides sorted by path       */
	unsigned long *by_name;   /* syms sorted by name         */

	/* ALL.REF modules whose hypertext was not found */
	const char **orphans;
	unsigned long norphans;

	/* ALL.REF as merged, to see whether it changed (NULL: none) */
	const char *allref_path;
	unsigned long allref_size, allref_mtime;
	int old_file;             /* loaded from a version 1 cache file */
} ehv_index;

ehv_index *ehv_index_new(void);
void ehv_index_free(ehv_index *idx);

/* sort the lookup tables; call after adding guides or symbols */
int ehv_index_finish(ehv_index *idx);

/*
 * The same in steps, for a program that must answer events meanwhile
 * (the AERA guidelines, 2.2): each unit of budget is a few hundred
 * moves.  1 = done, 0 = more to do, -1 = out of memory (the tables are
 * then incomplete: finish again).  Free with ehv_index_finish_abort().
 */
typedef struct ehv_finish ehv_finish;
ehv_finish *ehv_index_finish_begin(ehv_index *idx);
int ehv_index_finish_some(ehv_finish *f, unsigned int budget);
void ehv_index_finish_abort(ehv_finish *f);

/*
 * Merge aliases and labels from ALL.REF (or any REF file) into guides
 * that have no .REF file of their own.  Modules naming a hypertext that
 * is not in the index are listed in idx->orphans.  Call before
 * ehv_index_finish().
 */
int ehv_index_merge_ref(ehv_index *idx, const ref_db *db);

/* the same, `budget` modules at a time from *next: 1 = done, 0 = more */
int ehv_index_merge_ref_some(ehv_index *idx, const ref_db *db, unsigned long *next, unsigned int budget);

/* guide with exactly this path, or -1 */
long ehv_index_find_path(const ehv_index *idx, const char *path);

/*
 * Guides whose file name matches `file` ignoring case.  `file` may
 * include a directory (ignored) and may omit ".hyp".  Results are in
 * scan order (earlier roots first).  Stores up to max indices, returns
 * the total.
 */
unsigned long ehv_index_find_guides(const ehv_index *idx, const char *file,
                                    unsigned long *hits, unsigned long max);

/*
 * Symbols named `name`: exact spelling if there are any, otherwise
 * ignoring case.  With guide >= 0 only that guide's symbols count.
 * Pages come before aliases before labels; then scan order.
 */
unsigned long ehv_index_find_symbol(const ehv_index *idx, const char *name, long guide,
                                    unsigned long *hits, unsigned long max);

/*
 * Suggestions for a name that was not found: symbols (guide >= 0: of
 * that guide only) whose names are close to `name`, best first.  Uses
 * ST-Guide style "similar" matching: case, umlauts written as ae/oe/ue,
 * blanks, '_' and '-' are ignored, small typos are tolerated.
 */
unsigned long ehv_index_suggest(const ehv_index *idx, const char *name, long guide,
                                unsigned long *hits, unsigned long max);

/* the same for file names: guides whose key is close to `file` */
unsigned long ehv_index_suggest_guides(const ehv_index *idx, const char *file,
                                       unsigned long *hits, unsigned long max);

/* distance used by the suggest functions; > limit means "not similar" */
unsigned int ehv_similarity(const char *a, const char *b, unsigned int limit);

/*
 * Cache file.  Save writes pf_tmp_name(path) and renames it.  Load
 * returns NULL if the file is missing, damaged or from another version -
 * the caller then simply scans.  Version 2 keeps the sorted tables, so a
 * load sorts nothing; a version 1 file is loaded and sorted (old_file).
 */
#define EHV_CACHE_VERSION 2
int ehv_index_save(const ehv_index *idx, const char *path);
ehv_index *ehv_index_load(const char *path);

/*
 * Both in steps (a unit of budget: a piece of the file, or a few lines,
 * guides or table lines): 1 = done, 0 = more, -1 = failed.  The index
 * saved must stay as it is until the saver is done or aborted.
 *
 * The file is read a piece per step, each in one open, and written from
 * a buffer when it is full and at the end (the AERA guidelines, 6.5):
 * ehv_index_pieces() sets the sizes, 32 KB each by default (16 KB for a
 * 68000; the buffer as big as the whole file where memory allows, for
 * one write).  The buffer is taken at ehv_index_save_begin(), no
 * bigger than the file will be, halved until it can be; through
 * ehv_index_buffers()' alloc when one is given (the program: TT RAM
 * first, common/atari/bigbuf.c), else malloc().
 */
void ehv_index_pieces(unsigned long read_piece, unsigned long write_buffer);
void ehv_index_buffers(void *(*alloc)(unsigned long want, unsigned long *got), void (*release)(void *p));
typedef struct ehv_saver ehv_saver;
ehv_saver *ehv_index_save_begin(const ehv_index *idx, const char *path);
int ehv_index_save_some(ehv_saver *v, unsigned int budget);
void ehv_index_save_abort(ehv_saver *v);

typedef struct ehv_loader ehv_loader;
ehv_loader *ehv_index_load_begin(const char *path);    /* NULL: no file */
int ehv_index_load_some(ehv_loader *l, unsigned int budget);
ehv_index *ehv_index_load_take(ehv_loader *l);          /* after 1; frees l */
void ehv_index_load_abort(ehv_loader *l);

/* --- building (used by the scanner and the cache loader) ----------- */

/* add a guide; returns its index or -1 when out of memory */
long ehv_index_add_guide(ehv_index *idx, const ehv_guide *g);
int ehv_index_add_sym(ehv_index *idx, unsigned long guide, unsigned char kind,
                      unsigned short line, const char *name, const char *node, int from_ref);

/* room for this many guides and symbols, so that adding them copies
   nothing (-1: out of memory; the index is as it was) */
int ehv_index_reserve(ehv_index *idx, unsigned long nguides, unsigned long nsyms);

/* copy a guide with its symbols from another index */
long ehv_index_copy_guide(ehv_index *idx, const ehv_index *from, unsigned long g);

#endif
