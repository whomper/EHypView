/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - turn links and help requests into "open this file at this
 * page".
 *
 * Three kinds of input:
 *
 *   ehv_resolve_xref     an external link stored in a hypertext:
 *                        "FILE.HYP/node" (FILE may have a folder)
 *   ehv_resolve_name     a bare page name, alias, label or keyword:
 *                        the open hypertext first, then every indexed one
 *   ehv_resolve_request  arguments from VA_START, AC_HELP or the command
 *                        line, in the forms ST-Guide understands:
 *                          'C:\AES.HYP appl_init'   file and page
 *                          '*:\AES.HYP'             file, from any folder
 *                          'C:\AES.REF appl_init'   page via a REF file
 *                          'appl_init'              page from anywhere
 *
 * Files are found in this order: the path as written (when it has one),
 * the folder of the open hypertext, then the index.  Names are matched
 * ignoring case, so 8+3 upper-case links find lower-case or long names.
 */
#ifndef EHV_RESOLVE_H
#define EHV_RESOLVE_H

#include "hypfile.h"
#include "index.h"

typedef struct {
	const ehv_index *idx;     /* may be NULL                     */
	const char *current;      /* path of the open hypertext      */
	const hyp_doc *doc;       /* the open hypertext, may be NULL */
} ehv_context;

/* status */
#define EHV_FOUND        0    /* open path at node             */
#define EHV_NO_NODE      1    /* file found, page not: path is set,
                                 node is NULL (default page)   */
#define EHV_NO_FILE      2    /* hypertext not found           */
#define EHV_NOT_FOUND    3    /* page name not found anywhere  */
#define EHV_ERR_MEMORY  -1

#define EHV_MAX_HITS     8

typedef struct {
	int status;
	char *path;               /* hypertext to open (malloc'ed)  */
	char *node;               /* page; NULL means default page  */
	unsigned short line;      /* line to show (labels), 0 = top */
	long guide;               /* index entry of path, or -1     */

	/* EHV_FOUND by name: the other hypertexts with that name (guides) */
	unsigned long alt[EHV_MAX_HITS];
	unsigned int nalt;

	/* not found: close matches, guides for EHV_NO_FILE (suggest), else
	   names (similar.s) */
	unsigned long suggest[EHV_MAX_HITS];
	ehv_hits similar;
	unsigned int nsuggest;
} ehv_target;

int ehv_resolve_xref(const ehv_context *ctx, const char *xref, ehv_target *t);
int ehv_resolve_name(const ehv_context *ctx, const char *name, ehv_target *t);
int ehv_resolve_request(const ehv_context *ctx, const char *args, ehv_target *t);

void ehv_target_free(ehv_target *t);

/*
 * Split an AV-protocol argument string: arguments are separated by
 * blanks; one containing blanks is quoted with ' (a quote inside is
 * doubled).  Free with inf_free_list().
 */
unsigned int ehv_split_args(const char *args, char ***out);

#endif
