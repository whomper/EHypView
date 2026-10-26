/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - reader for ST-Guide hypertext files (*.HYP).
 *
 * This module knows nothing about GEM.  It is plain C and builds both on
 * the Atari (m68k-atari-mint-gcc) and on a host system for testing.
 *
 * All strings are returned in the file's own character set (normally the
 * Atari ST character set), exactly as stored.  See charset.h for
 * conversion when running on a non-Atari host.
 */
#ifndef EHV_HYPFILE_H
#define EHV_HYPFILE_H

/* index entry types */
#define HYP_NODE_INTERNAL   0   /* @node                       */
#define HYP_NODE_POPUP      1   /* @pnode                      */
#define HYP_NODE_EXTERNAL   2   /* link to FILE.HYP/node       */
#define HYP_NODE_IMAGE      3   /* @image data                 */
#define HYP_NODE_SYSTEM     4   /* @{... system ARG}           */
#define HYP_NODE_REXX_SCRIPT 5
#define HYP_NODE_REXX_CMD   6
#define HYP_NODE_QUIT       7
#define HYP_NODE_CLOSE      8   /* HypView extension           */
#define HYP_NODE_EOF        0xFF

#define HYP_IS_TEXT(type) ((type) == HYP_NODE_INTERNAL || (type) == HYP_NODE_POPUP)

/* operating system the file was compiled on */
#define HYP_OS_UNKNOWN  0
#define HYP_OS_AMIGA    1
#define HYP_OS_ATARI    2
#define HYP_OS_MAC      3
#define HYP_OS_WIN32    4
#define HYP_OS_UNIX     5
#define HYP_OS_MACOS    6

/* ST-Guide flags (extended header 10) */
#define HYP_STG_ENCRYPTED 0x0002

/* sentinel for "no such entry" in next/prev/toc fields */
#define HYP_NO_NODE 0xFFFFu

typedef struct {
	unsigned char type;
	unsigned long offset;        /* file offset of the object data   */
	unsigned long packed;        /* bytes stored in the file         */
	unsigned long size;          /* bytes after decompression        */
	unsigned short next;         /* following page (text nodes)      */
	unsigned short prev;         /* preceding page                   */
	unsigned short toc;          /* table of contents page           */
	const char *name;            /* node name / external reference   */
} hyp_entry;

/*
 * The file is not kept open between calls: a desk accessory on plain TOS
 * must not hold file handles, because they belong to whichever program
 * happened to be running when it opened them.  Within one call it may
 * be (hyp_open_held(), hyp_let_go()): a page shown with its hypertext
 * in one open (the AERA guidelines, 6.5).
 */
typedef struct {
	char *path;
	unsigned long filesize;
	int h;                       /* open, held; else -1             */

	unsigned char compiler;      /* compiler version byte           */
	unsigned char os;            /* HYP_OS_*                        */
	unsigned short num_entries;  /* without the trailing EOF entry  */
	hyp_entry *entries;
	char *names;                 /* storage for entry names         */

	/* extended headers, NULL if absent */
	char *database;              /* @database                       */
	char *default_node;          /* @default                        */
	char *hostname;              /* @hostname (may hold several)    */
	char *options;               /* @options                        */
	char *author;                /* @author                         */
	char *version;               /* @$VER:                          */
	char *help_node;             /* @help                           */
	char *subject;               /* @subject                        */
	char *charset;               /* HypView @charset, e.g. "atarist" */
	char *language;              /* HypView @lang                   */
	unsigned short stg_flags;    /* HYP_STG_*                       */
	unsigned short width;        /* @width, 0 if not given          */
} hyp_doc;

/* error codes */
#define HYP_OK          0
#define HYP_ERR_OPEN   -1        /* cannot open / read the file     */
#define HYP_ERR_FORMAT -2        /* not a hypertext or corrupt      */
#define HYP_ERR_MEMORY -3
#define HYP_ERR_RANGE  -4        /* bad entry number                */
#define HYP_ERR_DECODE -5        /* decompression failed            */

/* open a hypertext; *err (may be NULL) receives an error code */
hyp_doc *hyp_open(const char *path, int *err);
void hyp_close(hyp_doc *doc);

/* the same, the file left open for hyp_load() until hyp_let_go() (or
   hyp_close()), which must come before the caller's next event call */
hyp_doc *hyp_open_held(const char *path, int *err);
void hyp_let_go(hyp_doc *doc);
/* held again (a slice's pages read with one open): 0, or -1 when it
   can't be opened; hyp_let_go() before the next event call */
int hyp_hold(hyp_doc *doc);

/*
 * The last hypertext closed, kept for the rest of an event's work: a
 * request looked up in a file and then shown opens it once.  Off until
 * hyp_recent(1); the program calls hyp_forget() before each event call.
 */
void hyp_recent(int on);
void hyp_forget(void);

/* closed, but kept held for the next hyp_open*() of it in the same call
   (a request's file looked in, then shown); hyp_recent_let_go() closes
   the file, before the caller's next event call */
void hyp_keep(hyp_doc *doc);
void hyp_recent_let_go(void);

const char *hyp_strerror(int err);

/* entry number of a node by name (exact match first, then ignoring
   case), or HYP_NO_NODE */
unsigned short hyp_find_node(const hyp_doc *doc, const char *name);

/* the page shown when a file is opened without a node name */
unsigned short hyp_default_node(const hyp_doc *doc);

/*
 * Load and decode the object for entry `num`.  On success *data points to
 * a malloc'ed buffer of *size bytes (plus a terminating NUL that is not
 * counted) which the caller frees.
 */
int hyp_load(hyp_doc *doc, unsigned short num, unsigned char **data, unsigned long *size);

/*
 * The same a piece at a time (a picture unpacked between event calls,
 * the guidelines' Speed): hyp_unpack_begin() reads the object's packed
 * bytes (the file closed again) and returns NULL with *rc set when it
 * can't; hyp_unpack_head() gives the object's first n bytes at once (a
 * picture's size) without the room for the whole; hyp_unpack_more()
 * unpacks about `budget` more bytes (the room for the whole taken at the
 * first): 1 all of them, 0 more to come, else a HYP_ERR_*;
 * hyp_unpack_out() gives what is unpacked so far (*ready bytes of it);
 * hyp_unpack_take() hands the whole over when it is all there (as
 * hyp_load's *data) and frees the rest.
 */
typedef struct hyp_unpack hyp_unpack;
hyp_unpack *hyp_unpack_begin(hyp_doc *doc, unsigned short num, int *rc);
int hyp_unpack_head(const hyp_unpack *u, unsigned char *buf, unsigned long n);
int hyp_unpack_more(hyp_unpack *u, unsigned long budget);
const unsigned char *hyp_unpack_out(const hyp_unpack *u, unsigned long *ready);
unsigned char *hyp_unpack_take(hyp_unpack *u, unsigned long *size);
void hyp_unpack_free(hyp_unpack *u);

#endif
