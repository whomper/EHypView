/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - decoded representation of a text page (@node / @pnode).
 *
 * A page is a list of lines; each line is a list of runs.  A run is a
 * piece of text with one set of attributes, optionally a link.  Run text
 * points into the page's decoded data buffer and is NOT NUL-terminated;
 * use run->len.
 *
 * Graphics (@image, @line, @box, @rbox) are positioned on the character
 * grid: x in columns, y in lines, both relative to the page text.
 */
#ifndef EHV_PAGE_H
#define EHV_PAGE_H

#include "hypfile.h"

/* text attributes (bit vector, as stored in the file) */
#define HYP_ATTR_BOLD       0x01
#define HYP_ATTR_GHOSTED    0x02
#define HYP_ATTR_ITALIC     0x04
#define HYP_ATTR_UNDERLINE  0x08
#define HYP_ATTR_OUTLINED   0x10
#define HYP_ATTR_SHADOWED   0x20

#define HYP_COLOR_DEFAULT   0xFF

/* run kinds */
#define HYP_RUN_TEXT   0
#define HYP_RUN_LINK   1

/* link kinds */
#define HYP_LINK_LINK  0            /* @{... link NODE}  */
#define HYP_LINK_ALINK 1            /* @{... alink NODE} */

typedef struct {
	unsigned char kind;             /* HYP_RUN_*                        */
	unsigned char attr;             /* HYP_ATTR_* bits                  */
	unsigned char fg, bg;           /* colour index or HYP_COLOR_DEFAULT */
	unsigned char link_kind;        /* HYP_LINK_*, links only           */
	unsigned short target;          /* index entry, links only; may be
	                                   out of range in a damaged file  */
	unsigned short target_line;     /* 0-based line to scroll to        */
	const char *text;
	unsigned short len;
} hyp_run;

typedef struct {
	unsigned long first_run;        /* index into page->runs            */
	unsigned short nruns;
} hyp_line;

/* graphic objects */
#define HYP_GFX_IMAGE  0x32
#define HYP_GFX_LINE   0x33
#define HYP_GFX_BOX    0x34
#define HYP_GFX_RBOX   0x35

/* arrow flags for lines */
#define HYP_ARROW_START 0x01
#define HYP_ARROW_END   0x02

/*
 * Values are decoded: in the file, line widths carry a +128 offset (so
 * they can be negative) and heights/attributes a +1 offset to avoid NUL
 * bytes.
 */
typedef struct {
	unsigned char type;             /* HYP_GFX_*                        */
	unsigned char x;                /* column; 0 = centred (images)     */
	unsigned short y;               /* line                             */
	short w;                        /* characters; lines: dx, may be < 0 */
	short h;                        /* lines; lines: dy                 */
	unsigned short image;           /* images: index entry of the data  */
	unsigned char limage;           /* images: @limage (text flows by)  */
	unsigned char has_dither;       /* images: dithermask valid         */
	unsigned short dithermask;
	unsigned char arrows;           /* lines: HYP_ARROW_*               */
	unsigned char style;            /* lines: VDI line style 1..7       */
	unsigned char pattern;          /* boxes: fill pattern 0..          */
} hyp_gfx;

/* @xref entries, shown as a "See also" list */
typedef struct {
	unsigned short target;
	const char *text;               /* NUL-terminated, may be empty     */
} hyp_xref;

/* @tree object table entries */
typedef struct {
	unsigned short line, tree, object, target;
} hyp_objref;

typedef struct {
	unsigned short num;             /* index entry of this page         */
	unsigned char *data;            /* decoded data (owned)             */
	unsigned long size;

	const char *title;              /* @title, or NULL                  */

	hyp_line *lines;
	unsigned short nlines;
	hyp_run *runs;
	unsigned long nruns;

	hyp_gfx *gfx;
	unsigned short ngfx;
	hyp_xref *xrefs;
	unsigned short nxrefs;
	hyp_objref *objrefs;
	unsigned short nobjrefs;

	unsigned short width;           /* longest line in characters       */
} hyp_page;

/* load and parse a text page; *err receives a HYP_ERR_* code */
hyp_page *hyp_page_load(hyp_doc *doc, unsigned short num, int *err);
void hyp_page_free(hyp_page *page);

/* the title to show for a page: @title, else the node name */
const char *hyp_page_title(const hyp_doc *doc, const hyp_page *page);

/* text of a link run with an empty label: the target's name */
const char *hyp_link_default_text(const hyp_doc *doc, unsigned short target);

#endif
