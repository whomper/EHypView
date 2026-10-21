/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - reader for ST-Guide reference files (*.REF, ALL.REF).
 *
 * A REF file maps symbols (page names, aliases, labels, @keywords) to the
 * hypertext that defines them.  ALL.REF is simply many single REF files
 * concatenated, one "module" per hypertext.
 *
 * EHypView builds its own index from the HYP files themselves, so REF
 * files are optional; they are still read so that existing ALL.REF
 * setups keep working.
 */
#ifndef EHV_REF_H
#define EHV_REF_H

/* entry kinds */
#define REF_FILE      0      /* hypertext file name (module start)   */
#define REF_NODE      1      /* page name                            */
#define REF_ALIAS     2      /* alias of the preceding page          */
#define REF_LABEL     3      /* label in the preceding page (+line)  */
#define REF_DATABASE  4      /* @database of the module              */
#define REF_OS        5      /* operating system, as a digit         */
#define REF_TITLE     6      /* title of the preceding page          */
#define REF_CHARSET   30     /* HypView: character set name          */

typedef struct {
	const char *file;        /* as stored, usually without path/ext  */
	const char *database;    /* NULL if absent                       */
	const char *charset;     /* NULL if absent                       */
	char os;                 /* '0'..'6' or 0 if absent               */
	unsigned long first;     /* first symbol in ref_db.syms          */
	unsigned long count;
} ref_module;

typedef struct {
	unsigned char kind;      /* REF_NODE, REF_ALIAS or REF_LABEL     */
	unsigned short line;     /* labels only                          */
	const char *name;
	const char *node;        /* page that contains it (= name for nodes);
	                            NULL for a stray alias/label */
	const char *title;       /* page title if known, else NULL       */
	unsigned long module;    /* index into ref_db.modules            */
} ref_symbol;

typedef struct {
	char *path;
	unsigned char *data;     /* whole file; strings point into it    */
	unsigned long size;
	ref_module *modules;
	unsigned long nmodules;
	ref_symbol *syms;
	unsigned long nsyms;
} ref_db;

#define REF_OK          0
#define REF_ERR_OPEN   -1
#define REF_ERR_FORMAT -2
#define REF_ERR_MEMORY -3

ref_db *ref_open(const char *path, int *err);
void ref_close(ref_db *db);

/*
 * Find symbols named `name`.  Exact matches are returned if there are
 * any, otherwise matches ignoring case.  Up to `max` symbol indices are
 * stored in `hits`; the return value is the total number of matches.
 */
unsigned long ref_find(const ref_db *db, const char *name, unsigned long *hits, unsigned long max);

/*
 * The same file's symbols one at a time, for a program that must answer
 * events meanwhile (the AERA guidelines, 2.2): ref_walk_open() reads the
 * file (one open) and checks its structure as ref_open() does (0, or
 * REF_ERR_*); ref_walk_next() gives the symbols in the order ref_open()
 * lists them, each a few instructions (1, 0 at the end).  The strings
 * stay valid until ref_walk_close().
 */
typedef struct {
	unsigned char *data;
	unsigned long size, pos, end;  /* end: of the module being read, 0: none */
	unsigned long module;
	const char *node;
} ref_walk;
int ref_walk_open(ref_walk *w, const char *path);
int ref_walk_next(ref_walk *w, ref_symbol *s);
void ref_walk_close(ref_walk *w);

/* the full file name for a module: adds ".HYP" if it has no extension;
   writes at most `size` bytes including the NUL */
void ref_module_filename(const ref_module *m, char *buf, unsigned long size);

#endif
