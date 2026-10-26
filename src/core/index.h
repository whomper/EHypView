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
 * a rescan then re-reads only the files that changed.  The names stay
 * in the file and are read when looked up (EHV_BLOCK).
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

/* a name: what the index holds for it, as read from its blocks */
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

	unsigned long nsyms;      /* its names                   */
} ehv_guide;

/*
 * The names (pages, aliases, labels of every hypertext) are not kept in
 * memory: they are sorted by name, ignoring case, into blocks of
 * EHV_BLOCK bytes at the start of EHYPVIEW.IDX, and a lookup reads the
 * block or two a name is in.  In memory there are only each block's
 * first name, to know which block that is.  An index built without a
 * file (the tests, or a disk that can't be written) keeps the blocks in
 * memory instead.
 */
#define EHV_BLOCK 1024

typedef struct ehv_build ehv_build;

typedef struct {
	ehv_pool pool;            /* all strings                 */
	ehv_guide *guides;
	unsigned long nguides, cap_guides;
	unsigned long nsyms;      /* names, all hypertexts'      */

	/* lookup tables, built by ehv_index_finish() */
	unsigned long *by_key;    /* guides sorted by key        */
	unsigned long *by_path;   /* guides sorted by path       */

	/* the names' blocks: in `file` from its start, or in `mem` */
	char *file;
	unsigned char *mem;
	unsigned long nblocks;
	unsigned long gen;        /* the file's mark, at each block's start */
	const char **first;       /* each block's first name     */

	ehv_build *build;         /* while it is built (names.c) */

	/* ALL.REF modules whose hypertext was not found */
	const char **orphans;
	unsigned long norphans;

	/* ALL.REF as merged, to see whether it changed (NULL: none) */
	const char *allref_path;
	unsigned long allref_size, allref_mtime;
} ehv_index;

ehv_index *ehv_index_new(void);
void ehv_index_free(ehv_index *idx);

/*
 * Sort the lookup tables and the names; call after adding guides and
 * names.  The names go to the file ehv_index_build_to() named (its
 * NAME.ID$, renamed by ehv_index_save()), else into memory.
 */
int ehv_index_finish(ehv_index *idx);

/*
 * The same in steps, for a program that must answer events meanwhile
 * (the AERA guidelines, 2.2): each unit of budget is a few hundred
 * moves, or a piece of a file.  1 = done, 0 = more to do, -1 = failed
 * (out of memory, or a file that couldn't be read or written).  Free
 * with ehv_index_finish_abort().
 */
typedef struct ehv_finish ehv_finish;
ehv_finish *ehv_index_finish_begin(ehv_index *idx);
int ehv_index_finish_some(ehv_finish *f, unsigned int budget);
void ehv_index_finish_abort(ehv_finish *f);
const char *ehv_index_finish_doing(const ehv_finish *f);    /* in words, for a profile */

/*
 * Merge aliases and labels from ALL.REF (or any REF file) into guides
 * that have no .REF file of their own.  Modules naming a hypertext that
 * is not in the index are listed in idx->orphans.  Call before
 * ehv_index_finish().
 */
int ehv_index_merge_ref(ehv_index *idx, const ref_db *db);

/* the same, a few names at a time (m zeroed to start): 1 = done, 0 = more */
typedef struct {
	unsigned long module, guide, sym;
	int matched, in_module;
} ehv_refmerge;
int ehv_index_merge_ref_some(ehv_index *idx, const ref_db *db, ehv_refmerge *m, unsigned int budget);

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
 * Names found: copies, the caller's (ehv_hits_free).  Start with
 * ehv_hits_init(); each lookup adds to what is there.
 */
typedef struct {
	ehv_sym *s;
	unsigned long n, cap;
} ehv_hits;
void ehv_hits_init(ehv_hits *h);
void ehv_hits_free(ehv_hits *h);
int ehv_hits_add(ehv_hits *h, const ehv_sym *s);   /* -1: out of memory */

/*
 * Symbols named `name`: exact spelling if there are any, otherwise
 * ignoring case.  With guide >= 0 only that guide's symbols count.
 * Pages come before aliases before labels; then scan order.  Adds up to
 * max of them to *hits (NULL: none, only counted), returns the total.
 */
unsigned long ehv_index_find_symbol(const ehv_index *idx, const char *name, long guide,
                                    ehv_hits *hits, unsigned long max);

/*
 * Suggestions for a name that was not found: symbols (guide >= 0: of
 * that guide only) whose names are close to `name`, best first, added
 * to *hits.  Uses ST-Guide style "similar" matching: case, umlauts
 * written as ae/oe/ue, blanks, '_' and '-' are ignored, small typos are
 * tolerated.  Reads all the names.
 */
unsigned long ehv_index_suggest(const ehv_index *idx, const char *name, long guide,
                                ehv_hits *hits, unsigned long max);

/* the same for file names: guides whose key is close to `file` */
unsigned long ehv_index_suggest_guides(const ehv_index *idx, const char *file,
                                       unsigned long *hits, unsigned long max);

/* distance used by the suggest functions; > limit means "not similar" */
unsigned int ehv_similarity(const char *a, const char *b, unsigned int limit);
/* the limit the suggest functions use for a name */
unsigned int ehv_similar_limit(const char *name);

/*
 * All the names in their order, read in pieces of `piece` blocks (each
 * piece an open of the file, unless one call reads them all: then the
 * file stays open from ehv_names_open() to ehv_names_close(), which
 * the caller does within one event's work).
 *
 *     ehv_names nm;
 *     if (ehv_names_open(&nm, idx, 16, 1) == 0)
 *         while (ehv_names_next(&nm, &sym) == 1)
 *             ... sym's strings last until the next call ...
 *     ehv_names_close(&nm);
 */
typedef struct {
	const ehv_index *idx;
	unsigned long next, last;     /* blocks */
	unsigned char *buf;
	const unsigned char *data;    /* the piece read (buf, or the blocks in memory) */
	unsigned long nbuf, bi, pos;  /* its blocks, the one read, where in it */
	unsigned long piece;
	int h, keep_open, failed;
} ehv_names;
int ehv_names_open(ehv_names *nm, const ehv_index *idx, unsigned long piece, int keep_open);
int ehv_names_seek(ehv_names *nm, unsigned long block);
/* the block where names starting with `name` (ignoring case) begin:
   they follow each other from there (the names are sorted) */
unsigned long ehv_names_start(const ehv_index *idx, const char *name);
int ehv_names_next(ehv_names *nm, ehv_sym *s);     /* 1, 0 at the end, -1 failed */
void ehv_names_close(ehv_names *nm);

/*
 * Cache file.  Save writes pf_tmp_name(path) and renames it.  Load
 * returns NULL if the file is missing, damaged or from another version -
 * the caller then simply scans.
 *
 * Version 3: the names' blocks, then the head (text: the guides, the
 * blocks' first names, the sorted tables), then a line saying where the
 * head is.  Loading reads only the head.
 */
#define EHV_CACHE_VERSION 3
int ehv_index_save(ehv_index *idx, const char *path);
ehv_index *ehv_index_load(const char *path);

/*
 * Both in steps (a unit of budget: a piece of the file, or a few lines,
 * guides or table lines): 1 = done, 0 = more, -1 = failed.  The index
 * saved must stay as it is until the saver is done or aborted; when its
 * names were sorted into this file's NAME.ID$, the saver adds the head,
 * renames it, and the index reads them from the file's own name then.
 *
 * The head is read a piece per step, each in one open, and written from
 * a buffer when it is full and at the end (the AERA guidelines, 6.5):
 * ehv_index_pieces() sets the sizes, 32 KB each by default (16 KB for a
 * 68000).  The buffer is taken at ehv_index_save_begin(), no bigger
 * than the head will be, halved until it can be; through
 * ehv_index_buffers()' alloc when one is given (the program: TT RAM
 * first, common/atari/bigbuf.c), else malloc().
 */
void ehv_index_pieces(unsigned long read_piece, unsigned long write_buffer);
void ehv_index_buffers(void *(*alloc)(unsigned long want, unsigned long *got), void (*release)(void *p));
typedef struct ehv_saver ehv_saver;
ehv_saver *ehv_index_save_begin(ehv_index *idx, const char *path);
int ehv_index_save_some(ehv_saver *v, unsigned int budget);
void ehv_index_save_abort(ehv_saver *v);

typedef struct ehv_loader ehv_loader;
ehv_loader *ehv_index_load_begin(const char *path);    /* NULL: no file */
int ehv_index_load_some(ehv_loader *l, unsigned int budget);
ehv_index *ehv_index_load_take(ehv_loader *l);          /* after 1; frees l */
void ehv_index_load_abort(ehv_loader *l);

/* --- building (used by the scanner and the cache loader) ----------- */

/*
 * Where an index being built sorts its names: beside the cache file
 * `cache` (NAME.RUN while they are gathered, then NAME.ID$, which
 * ehv_index_save() makes the cache).  Without it, in memory.  Call
 * before adding names.  -1: out of memory
 */
int ehv_index_build_to(ehv_index *idx, const char *cache);

/* the names gathered in memory (about 24 KB, by default) */
void ehv_index_batch(unsigned long bytes);

/* add a guide; returns its index or -1 when out of memory */
long ehv_index_add_guide(ehv_index *idx, const ehv_guide *g);
int ehv_index_add_sym(ehv_index *idx, unsigned long guide, unsigned char kind,
                      unsigned short line, const char *name, const char *node, int from_ref);

/* the names gathered fill their memory: before adding more, do
   ehv_index_build_step() while this is 1 (it sorts them and writes them
   to NAME.RUN, a step at a time; 1 = more, 0 = room again, -1 = failed).
   Adding names regardless does all of it at once */
int ehv_index_build_busy(const ehv_index *idx);
int ehv_index_build_step(ehv_index *idx);
int ehv_index_build_next_io(const ehv_index *idx);  /* that step writes NAME.RUN */

/* room for this many guides, so that adding them copies nothing (-1:
   out of memory; the index is as it was) */
int ehv_index_reserve(ehv_index *idx, unsigned long nguides);

/* guide `g` of `from` was added as `ng`: its names come from `from`'s
   blocks when the names are sorted (ALL.REF's left out: merged afresh).
   `from` must stay until then.  -1: out of memory */
int ehv_index_take_names(ehv_index *idx, const ehv_index *from, unsigned long g, unsigned long ng);

#endif
