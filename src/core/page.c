/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - parser for decoded text pages.
 *
 * Page data is a byte stream.  Plain bytes are text, NUL ends a line and
 * ESC (27) introduces a command:
 *
 *   ESC ESC                      literal ESC character
 *   ESC 0x23 title NUL           window title
 *   ESC 0x24..0x27 [line] node len [text]   link / alink
 *   ESC 0x28..0x2F len data      data block (0x2F: image dither mask)
 *   ESC 0x30 len node text NUL   @xref ("See also")
 *   ESC 0x31 line tree obj node  @tree object table
 *   ESC 0x32 node x y w h        image
 *   ESC 0x33..0x35 x y w h data  line, box, rounded box
 *   ESC 0x64..0xA3               text attributes (value - 0x64)
 *   ESC 0xA5 / 0xA6 colour       foreground / background colour
 *
 * 16 bit numbers inside commands avoid NUL bytes by using base 255 with
 * every digit stored plus one.
 */
#include <stdlib.h>
#include <string.h>
#include "page.h"

#define ESC 0x1B

#define C_ESC         0x1B
#define C_TITLE       0x23
#define C_LINK        0x24
#define C_LINK_LINE   0x25
#define C_ALINK       0x26
#define C_ALINK_LINE  0x27
#define C_DATA0       0x28
#define C_DITHERMASK  0x2F
#define C_XREF        0x30
#define C_OBJTABLE    0x31
#define C_PIC         0x32
#define C_LINE        0x33
#define C_BOX         0x34
#define C_RBOX        0x35
#define C_ATTR_FIRST  0x64
#define C_ATTR_LAST   0xA3
#define C_TYPEWRITER  0xA4
#define C_FG          0xA5
#define C_BG          0xA6

#define STRLEN_OFFSET 32

static unsigned short dec255(const unsigned char *p)
{
	return (unsigned short) ((p[1] - 1U) * 255U + p[0] - 1U);
}

/* grow *arr to hold at least n+1 elements of size sz */
static int grow(void **arr, unsigned long *cap, unsigned long n, size_t sz)
{
	if (n < *cap)
		return 0;
	{
		unsigned long ncap = *cap ? *cap * 2 : 16;
		void *p = realloc(*arr, ncap * sz);

		if (p == NULL)
			return -1;
		*arr = p;
		*cap = ncap;
	}
	return 0;
}

typedef struct {
	hyp_page *page;
	const hyp_doc *doc;
	unsigned long cap_runs, cap_lines, cap_gfx, cap_xrefs, cap_objs;
	unsigned long line_first;        /* first run of the current line  */
	unsigned short line_chars;       /* characters in the current line */
	unsigned char attr, fg, bg;
	unsigned short dithermask;
	int has_dither;
} parser;

static int add_run(parser *ps, unsigned char kind, const char *text, unsigned long len,
                   unsigned short target, unsigned short target_line, unsigned char link_kind)
{
	hyp_page *pg = ps->page;
	hyp_run *r;

	if (len == 0 && kind == HYP_RUN_TEXT)
		return 0;
	if (grow((void **) &pg->runs, &ps->cap_runs, pg->nruns, sizeof(hyp_run)) < 0)
		return -1;
	r = &pg->runs[pg->nruns++];
	r->kind = kind;
	r->attr = ps->attr;
	r->fg = ps->fg;
	r->bg = ps->bg;
	r->link_kind = link_kind;
	r->target = target;
	r->target_line = target_line;
	r->text = text;
	r->len = (unsigned short) (len > 0xFFFFUL ? 0xFFFFUL : len);
	ps->line_chars = (unsigned short) (ps->line_chars + r->len);
	return 0;
}

static int end_line(parser *ps)
{
	hyp_page *pg = ps->page;
	hyp_line *l;
	unsigned long cap = ps->cap_lines;

	if (pg->nlines == 0xFFFFu)
		return -1;
	if (grow((void **) &pg->lines, &cap, pg->nlines, sizeof(hyp_line)) < 0)
		return -1;
	ps->cap_lines = cap;
	l = &pg->lines[pg->nlines++];
	l->first_run = ps->line_first;
	l->nruns = (unsigned short) (pg->nruns - ps->line_first);
	if (ps->line_chars > pg->width)
		pg->width = ps->line_chars;
	ps->line_first = pg->nruns;
	ps->line_chars = 0;
	return 0;
}

const char *hyp_link_default_text(const hyp_doc *doc, unsigned short target)
{
	if (target < doc->num_entries)
		return doc->entries[target].name;
	return "???";
}

/*
 * Handle one ESC command at p (pointing at the ESC byte).  Returns the
 * number of bytes consumed, or 0 on a fatal error (out of memory).
 * Truncated commands at the end of the data consume everything left.
 */
static unsigned long do_command(parser *ps, const unsigned char *p, unsigned long left)
{
	hyp_page *pg = ps->page;
	unsigned char cmd;

	if (left < 2)
		return left;
	cmd = p[1];

	switch (cmd)
	{
	case C_ESC:
		if (add_run(ps, HYP_RUN_TEXT, (const char *) p + 1, 1, 0, 0, 0) < 0)
			return 0;
		return 2;

	case C_TITLE:
		{
			unsigned long n = 2;

			while (n < left && p[n] != '\0')
				n++;
			if (n >= left)
				return left;
			if (pg->title == NULL)
				pg->title = (const char *) p + 2;
			return n + 1;
		}

	case C_LINK:
	case C_LINK_LINE:
	case C_ALINK:
	case C_ALINK_LINE:
		{
			unsigned long n = 2;
			unsigned short line = 0, target;
			unsigned int lenbyte;
			const char *text;
			unsigned long tlen;

			if (cmd == C_LINK_LINE || cmd == C_ALINK_LINE)
			{
				if (left < n + 2)
					return left;
				line = dec255(p + n);
				n += 2;
			}
			if (left < n + 3)
				return left;
			target = dec255(p + n);
			lenbyte = p[n + 2];
			n += 3;
			if (lenbyte <= STRLEN_OFFSET)
			{
				text = hyp_link_default_text(ps->doc, target);
				tlen = strlen(text);
			} else
			{
				tlen = lenbyte - STRLEN_OFFSET;
				if (left < n + tlen)
					return left;
				text = (const char *) p + n;
				n += tlen;
			}
			if (add_run(ps, HYP_RUN_LINK, text, tlen, target, line,
					(cmd == C_ALINK || cmd == C_ALINK_LINE) ? HYP_LINK_ALINK : HYP_LINK_LINK) < 0)
				return 0;
			return n;
		}

	case C_XREF:
		{
			unsigned long len;
			hyp_xref *x;

			if (left < 5)
				return left;
			len = p[2] < 5 ? 5 : p[2];
			if (len > left)
				return left;
			if (grow((void **) &pg->xrefs, &ps->cap_xrefs, pg->nxrefs, sizeof(hyp_xref)) < 0)
				return 0;
			x = &pg->xrefs[pg->nxrefs++];
			x->target = dec255(p + 3);
			x->text = "";
			/* the text is NUL-terminated inside the block and stored with
			   surrounding blanks; p points into the page's own buffer */
			if (len > 5 && p[len - 1] == '\0')
			{
				char *s = (char *) p + 5;
				char *e = (char *) p + len - 1;

				while (*s == ' ')
					s++;
				while (e > s && e[-1] == ' ')
					*--e = '\0';
				x->text = s;
			}
			return len;
		}

	case C_OBJTABLE:
		{
			hyp_objref *o;

			if (left < 10)
				return left;
			if (grow((void **) &pg->objrefs, &ps->cap_objs, pg->nobjrefs, sizeof(hyp_objref)) < 0)
				return 0;
			o = &pg->objrefs[pg->nobjrefs++];
			o->line = dec255(p + 2);
			o->tree = dec255(p + 4);
			o->object = dec255(p + 6);
			o->target = dec255(p + 8);
			return 10;
		}

	case C_PIC:
	case C_LINE:
	case C_BOX:
	case C_RBOX:
		{
			hyp_gfx *g;
			unsigned long need = cmd == C_PIC ? 9 : 8;
			const unsigned char *q = p + 2;

			if (left < need)
				return left;
			if (grow((void **) &pg->gfx, &ps->cap_gfx, pg->ngfx, sizeof(hyp_gfx)) < 0)
				return 0;
			g = &pg->gfx[pg->ngfx++];
			memset(g, 0, sizeof(*g));
			g->type = cmd;
			if (cmd == C_PIC)
			{
				g->image = dec255(q);
				q += 2;
				g->has_dither = (unsigned char) ps->has_dither;
				g->dithermask = ps->dithermask;
				ps->has_dither = 0;
			}
			g->x = q[0];
			g->y = dec255(q + 1);
			switch (cmd)
			{
			case C_PIC:
				g->w = q[3];
				g->h = q[4];
				g->limage = q[3] == 1;
				break;
			case C_LINE:
				{
					unsigned int attr = q[5] ? q[5] - 1U : 0U;
					unsigned int style = attr >> 3;

					g->w = (short) (q[3] - 128);
					g->h = (short) (q[4] ? q[4] - 1 : 0);
					g->arrows = (unsigned char) (attr & 3);
					g->style = (unsigned char) ((style > 6 ? 6 : style) + 1);
				}
				break;
			default:
				g->w = q[3];
				g->h = q[4];
				g->pattern = (unsigned char) (q[5] ? q[5] - 1 : 0);
				break;
			}
			return need;
		}

	case C_FG:
	case C_BG:
		if (left < 3)
			return left;
		if (cmd == C_FG)
			ps->fg = p[2];
		else
			ps->bg = p[2];
		return 3;

	default:
		if (cmd >= C_DATA0 && cmd <= C_DITHERMASK)
		{
			unsigned long len = p[2] < 3 ? 3 : p[2];

			if (left < 3 || len > left)
				return left;
			if (cmd == C_DITHERMASK && len >= 5)
			{
				ps->dithermask = (unsigned short) ((p[3] << 8) | p[4]);
				ps->has_dither = 1;
			}
			return len;
		}
		if (cmd >= C_ATTR_FIRST && cmd <= C_ATTR_LAST)
		{
			ps->attr = (unsigned char) (cmd - C_ATTR_FIRST);
			return 2;
		}
		/* C_TYPEWRITER and unknown commands: ignore the command byte */
		return 2;
	}
}

hyp_page *hyp_page_load(hyp_doc *doc, unsigned short num, int *err)
{
	parser ps;
	hyp_page *pg;
	const unsigned char *p, *end, *text;
	int rc;

	if (err != NULL)
		*err = HYP_OK;
	if (num >= doc->num_entries || !HYP_IS_TEXT(doc->entries[num].type))
	{
		if (err != NULL)
			*err = HYP_ERR_RANGE;
		return NULL;
	}

	pg = calloc(1, sizeof(*pg));
	if (pg == NULL)
	{
		if (err != NULL)
			*err = HYP_ERR_MEMORY;
		return NULL;
	}
	pg->num = num;
	rc = hyp_load(doc, num, &pg->data, &pg->size);
	if (rc != HYP_OK)
	{
		free(pg);
		if (err != NULL)
			*err = rc;
		return NULL;
	}

	memset(&ps, 0, sizeof(ps));
	ps.page = pg;
	ps.doc = doc;
	ps.fg = ps.bg = HYP_COLOR_DEFAULT;

	p = pg->data;
	end = p + pg->size;
	text = p;
	while (p < end)
	{
		if (*p == ESC || *p == '\0')
		{
			if (add_run(&ps, HYP_RUN_TEXT, (const char *) text, (unsigned long) (p - text), 0, 0, 0) < 0)
				goto nomem;
			if (*p == '\0')
			{
				if (end_line(&ps) < 0)
					goto nomem;
				p++;
			} else
			{
				unsigned long n = do_command(&ps, p, (unsigned long) (end - p));

				if (n == 0)
					goto nomem;
				p += n;
			}
			text = p;
		} else
		{
			p++;
		}
	}
	if (add_run(&ps, HYP_RUN_TEXT, (const char *) text, (unsigned long) (p - text), 0, 0, 0) < 0)
		goto nomem;
	if (pg->nruns > ps.line_first && end_line(&ps) < 0)
		goto nomem;
	return pg;

nomem:
	hyp_page_free(pg);
	if (err != NULL)
		*err = HYP_ERR_MEMORY;
	return NULL;
}

void hyp_page_free(hyp_page *page)
{
	if (page == NULL)
		return;
	free(page->data);
	free(page->lines);
	free(page->runs);
	free(page->gfx);
	free(page->xrefs);
	free(page->objrefs);
	free(page);
}

const char *hyp_page_title(const hyp_doc *doc, const hyp_page *page)
{
	if (page->title != NULL && page->title[0] != '\0')
		return page->title;
	return doc->entries[page->num].name;
}
