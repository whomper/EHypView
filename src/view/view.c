/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - what a viewer window shows and how it moves.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fsys.h"
#include "resolve.h"
#include "view.h"

static char *dupstr(const char *s)
{
	char *d = malloc(strlen(s) + 1);

	if (d != NULL)
		strcpy(d, s);
	return d;
}

void view_init(ehv_view *v, const ehv_index *idx)
{
	memset(v, 0, sizeof(*v));
	v->idx = idx;
	v->sel = -1;
}

static void hist_free(view_hist *h)
{
	free(h->path);
	free(h->title);
}

/* forget every page gone back from (any move but back and forward) */
static void forward_clear(ehv_view *v)
{
	while (v->nfwd > 0)
		hist_free(&v->fwd[--v->nfwd]);
}

void view_free(ehv_view *v)
{
	unsigned long i;

	hyp_page_free(v->popup);
	hyp_page_free(v->page);
	hyp_close(v->doc);
	for (i = 0; i < v->nhist; i++)
		hist_free(&v->hist[i]);
	free(v->hist);
	forward_clear(v);
	free(v->fwd);
	view_init(v, v->idx);
}

int view_has_page(const ehv_view *v)
{
	return v->page != NULL;
}

static void set_status(ehv_view *v, const char *a, const char *b, const char *c)
{
	snprintf(v->status, sizeof(v->status), "%s%s%s", a ? a : "", b ? b : "", c ? c : "");
}

/* room for one more entry on a list */
static int hist_room(view_hist **list, unsigned long n, unsigned long *cap)
{
	if (n == *cap)
	{
		unsigned long ncap = *cap ? *cap * 2 : 16;
		view_hist *p = realloc(*list, ncap * sizeof(view_hist));

		if (p == NULL)
			return -1;
		*list = p;
		*cap = ncap;
	}
	return 0;
}

/* the page shown, put on a list (the history, or the forward list);
   1 when there was one, 0 when nothing is shown, -1 out of memory */
static int push_current(ehv_view *v, view_hist **list, unsigned long *n, unsigned long *cap)
{
	view_hist *h;

	if (v->page == NULL)
		return 0;
	if (hist_room(list, *n, cap) < 0)
		return -1;
	h = &(*list)[*n];
	h->path = dupstr(v->doc->path);
	h->title = dupstr(hyp_page_title(v->doc, v->page));
	if (h->path == NULL || h->title == NULL)
	{
		hist_free(h);
		return -1;
	}
	h->node = v->page->num;
	h->top = v->top;
	(*n)++;
	return 1;
}

static int push_history(ehv_view *v)
{
	return push_current(v, &v->hist, &v->nhist, &v->cap_hist) < 0 ? -1 : 0;
}

/* forget the entry push_history() added, the move having failed */
static void pop_history(ehv_view *v)
{
	v->nhist--;
	hist_free(&v->hist[v->nhist]);
}

/* replace the page; the document stays */
static int show_entry(ehv_view *v, unsigned short entry, long top)
{
	int err;
	hyp_page *pg = hyp_page_load(v->doc, entry, &err);

	if (pg == NULL)
	{
		set_status(v, "Cannot show the page: ", hyp_strerror(err), NULL);
		return VIEW_ERROR;
	}
	view_popup_close(v);
	hyp_page_free(v->page);
	v->page = pg;
	v->left = 0;
	v->sel = -1;
	if (top >= pg->nlines)
		top = pg->nlines > 0 ? pg->nlines - 1 : 0;
	v->top = top < 0 ? 0 : top;
	return VIEW_OK;
}

/* open a document (or reuse the open one) and show `node`: a new one
   held open until its page is read, one open for both */
static int show_in(ehv_view *v, const char *path, const char *node, long top, int push)
{
	hyp_doc *doc = v->doc;
	unsigned short entry;
	int err, rc;

	if (doc == NULL || strcmp(doc->path, path) != 0)
	{
		doc = hyp_open_held(path, &err);
		if (doc == NULL)
		{
			set_status(v, fs_basename(path), ": ", hyp_strerror(err));
			return VIEW_ERROR;
		}
	}
	v->status[0] = '\0';
	entry = node != NULL ? hyp_find_node(doc, node) : HYP_NO_NODE;
	if (entry == HYP_NO_NODE)
	{
		if (node != NULL)
			set_status(v, "No page \"", node, "\" - showing the main page");
		entry = hyp_default_node(doc);
		top = 0;
	}
	if (entry == HYP_NO_NODE)
	{
		set_status(v, fs_basename(path), ": no pages", NULL);
		if (doc != v->doc)
			hyp_close(doc);
		return VIEW_ERROR;
	}
	if (push && push_history(v) < 0)
	{
		set_status(v, "Out of memory", NULL, NULL);
		if (doc != v->doc)
			hyp_close(doc);
		return VIEW_ERROR;
	}
	if (doc != v->doc)
	{
		view_popup_close(v);
		hyp_page_free(v->page);
		v->page = NULL;
		hyp_close(v->doc);
		v->doc = doc;
	}
	rc = show_entry(v, entry, top);
	if (rc != VIEW_OK && push)
		pop_history(v);           /* nothing is shown instead */
	else if (rc == VIEW_OK && push)
		forward_clear(v);
	return rc;
}

static int show(ehv_view *v, const char *path, const char *node, long top, int push)
{
	int rc = show_in(v, path, node, top, push);

	hyp_let_go(v->doc);
	return rc;
}

int view_open(ehv_view *v, const char *path, const char *node, unsigned short line)
{
	return show(v, path, node, line, 1);
}

int view_goto(ehv_view *v, unsigned short entry, unsigned short line)
{
	if (v->doc == NULL || entry >= v->doc->num_entries || !HYP_IS_TEXT(v->doc->entries[entry].type))
		return VIEW_ERROR;
	if (push_history(v) < 0)
		return VIEW_ERROR;
	if (show_entry(v, entry, line) != VIEW_OK)
	{
		pop_history(v);
		return VIEW_ERROR;
	}
	forward_clear(v);
	v->status[0] = '\0';
	return VIEW_OK;
}

/* explain a target that could not be found */
static void report(ehv_view *v, const char *what, const ehv_target *t)
{
	char sugg[120] = "";
	unsigned int i;

	for (i = 0; v->idx != NULL && i < t->nsuggest && i < 3; i++)
	{
		const char *s = t->status == EHV_NO_FILE ? v->idx->guides[t->suggest[i]].key
			: i < t->similar.n ? t->similar.s[i].name : "";

		if (strlen(sugg) + strlen(s) + 3 < sizeof(sugg))
		{
			strcat(sugg, i ? ", " : "");
			strcat(sugg, s);
		}
	}
	snprintf(v->status, sizeof(v->status), "%s not found: %s%s%s", t->status == EHV_NO_FILE ? "Hypertext" : "Page",
		what, sugg[0] ? " - did you mean " : "", sugg);
}

/* show a resolved target */
static int show_target(ehv_view *v, const char *what, ehv_target *t)
{
	int rc;

	if (t->status == EHV_NO_FILE || t->status == EHV_NOT_FOUND || t->path == NULL)
	{
		report(v, what, t);
		ehv_target_free(t);
		return VIEW_ERROR;
	}
	rc = show(v, t->path, t->node, t->line, 1);
	if (rc == VIEW_OK && t->status == EHV_NO_NODE)
		report(v, what, t);
	ehv_target_free(t);
	return rc;
}

static void context(const ehv_view *v, ehv_context *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->idx = v->idx;
	if (v->doc != NULL)
	{
		ctx->current = v->doc->path;
		ctx->doc = v->doc;
	}
}

int view_request(ehv_view *v, const char *args)
{
	ehv_context ctx;
	ehv_target t;

	int rc;

	context(v, &ctx);
	if (ehv_resolve_request(&ctx, args, &t) < 0)
	{
		hyp_recent_let_go();
		set_status(v, "Out of memory", NULL, NULL);
		return VIEW_ERROR;
	}
	rc = show_target(v, args, &t);
	hyp_recent_let_go();              /* the file the resolver looked in */
	return rc;
}

int view_follow(ehv_view *v, const hyp_run *run)
{
	const hyp_entry *e;
	ehv_context ctx;
	ehv_target t;

	if (v->doc == NULL || run == NULL || run->kind != HYP_RUN_LINK)
		return VIEW_ERROR;
	if (run->target >= v->doc->num_entries)
	{
		set_status(v, "This link is damaged", NULL, NULL);
		return VIEW_ERROR;
	}
	e = &v->doc->entries[run->target];
	switch (e->type)
	{
	case HYP_NODE_INTERNAL:
		return view_goto(v, run->target, run->target_line);
	case HYP_NODE_POPUP:
		{
			int err;
			hyp_page *pg = hyp_page_load(v->doc, run->target, &err);

			if (pg == NULL)
			{
				set_status(v, "Cannot show the pop-up: ", hyp_strerror(err), NULL);
				return VIEW_ERROR;
			}
			view_popup_close(v);
			v->popup = pg;
			v->popup_node = run->target;
			return VIEW_POPUP;
		}
	case HYP_NODE_EXTERNAL:
		{
			int rc;

			context(v, &ctx);
			if (ehv_resolve_xref(&ctx, e->name, &t) < 0)
			{
				hyp_recent_let_go();
				return VIEW_ERROR;
			}
			if (t.status != EHV_NO_FILE && t.status != EHV_NOT_FOUND && run->target_line)
				t.line = run->target_line;
			rc = show_target(v, e->name, &t);
			hyp_recent_let_go();      /* the file the resolver looked in */
			return rc;
		}
	case HYP_NODE_QUIT:
	case HYP_NODE_CLOSE:
		return VIEW_CLOSE;
	default:
		set_status(v, "This kind of link is not supported: ", e->name, NULL);
		return VIEW_ERROR;
	}
}

int view_follow_selected(ehv_view *v)
{
	if (v->page == NULL || v->sel < 0 || (unsigned long) v->sel >= v->page->nruns)
		return VIEW_ERROR;
	return view_follow(v, &v->page->runs[v->sel]);
}

/* show a page from a list; the lists stay as they are */
static int show_hist(ehv_view *v, const view_hist *h)
{
	hyp_doc *doc;
	int err;

	if (v->doc != NULL && strcmp(v->doc->path, h->path) == 0)
		return show_entry(v, h->node, h->top);
	/* another hypertext: open it again, and its page in the same open */
	doc = hyp_open_held(h->path, &err);
	if (doc == NULL)
	{
		set_status(v, fs_basename(h->path), ": ", hyp_strerror(err));
		return VIEW_ERROR;
	}
	hyp_page_free(v->page);
	v->page = NULL;
	hyp_close(v->doc);
	v->doc = doc;
	err = show_entry(v, h->node, h->top);
	hyp_let_go(doc);
	return err;
}

int view_jump(ehv_view *v, long steps)
{
	view_hist **from, **to, h;
	unsigned long *nfrom, *nto, *capto, k, i;
	int pushed, rc;

	if (steps == 0)
		return VIEW_OK;
	if (steps < 0)
	{
		from = &v->hist, nfrom = &v->nhist;
		to = &v->fwd, nto = &v->nfwd, capto = &v->cap_fwd;
		k = (unsigned long) -steps;
	} else
	{
		from = &v->fwd, nfrom = &v->nfwd;
		to = &v->hist, nto = &v->nhist, capto = &v->cap_hist;
		k = (unsigned long) steps;
	}
	if (k > *nfrom)
	{
		set_status(v, steps < 0 ? "This is the first page" : "There is no page to go forward to", NULL, NULL);
		return VIEW_ERROR;
	}
	/* room for the page shown and the ones passed over */
	for (i = 0; i < k; i++)
		if (hist_room(to, *nto + i, capto) < 0)
		{
			set_status(v, "Out of memory", NULL, NULL);
			return VIEW_ERROR;
		}
	pushed = push_current(v, to, nto, capto);
	if (pushed < 0)
	{
		set_status(v, "Out of memory", NULL, NULL);
		return VIEW_ERROR;
	}
	view_popup_close(v);
	for (i = 1; i < k; i++)
		(*to)[(*nto)++] = (*from)[--*nfrom];
	h = (*from)[--*nfrom];
	rc = show_hist(v, &h);
	if (rc != VIEW_OK)
	{
		/* as it was */
		(*from)[(*nfrom)++] = h;
		for (i = 1; i < k; i++)
			(*from)[(*nfrom)++] = (*to)[--*nto];
		if (pushed)
			hist_free(&(*to)[--*nto]);
		return rc;
	}
	hist_free(&h);
	v->status[0] = '\0';
	return VIEW_OK;
}

int view_back(ehv_view *v)
{
	return view_jump(v, -1);
}

int view_forward(ehv_view *v)
{
	return view_jump(v, 1);
}

const char *view_jump_title(const ehv_view *v, long steps)
{
	if (steps < 0 && (unsigned long) -steps <= v->nhist)
		return v->hist[v->nhist - (unsigned long) -steps].title;
	if (steps > 0 && (unsigned long) steps <= v->nfwd)
		return v->fwd[v->nfwd - (unsigned long) steps].title;
	return NULL;
}

unsigned int view_xref_count(const ehv_view *v)
{
	return v->page != NULL ? v->page->nxrefs : 0;
}

void view_xref_label(const ehv_view *v, unsigned int i, char *buf, unsigned long size)
{
	const hyp_xref *x;

	if (size == 0)
		return;
	buf[0] = '\0';
	if (i >= view_xref_count(v))
		return;
	x = &v->page->xrefs[i];
	if (x->text != NULL && x->text[0] != '\0')
		snprintf(buf, size, "%s", x->text);
	else if (x->target < v->doc->num_entries)
		snprintf(buf, size, "%s", v->doc->entries[x->target].name);
}

int view_xref_follow(ehv_view *v, unsigned int i)
{
	hyp_run run;

	if (i >= view_xref_count(v))
		return VIEW_ERROR;
	memset(&run, 0, sizeof(run));
	run.kind = HYP_RUN_LINK;
	run.target = v->page->xrefs[i].target;
	return view_follow(v, &run);
}

/* the entry `which` leads to, or HYP_NO_NODE (with *msg saying why) */
static unsigned short relative_entry(const ehv_view *v, int which, const char **msg)
{
	unsigned short entry = HYP_NO_NODE;

	*msg = NULL;
	if (v->page == NULL)
		return HYP_NO_NODE;
	switch (which)
	{
	case VIEW_NEXT:
		entry = v->doc->entries[v->page->num].next;
		*msg = "This is the last page";
		break;
	case VIEW_PREV:
		entry = v->doc->entries[v->page->num].prev;
		*msg = "This is the first page";
		break;
	case VIEW_TOC:
		entry = v->doc->entries[v->page->num].toc;
		*msg = "No table of contents";
		break;
	case VIEW_INDEX:
		entry = hyp_find_node(v->doc, "Index");
		*msg = "This hypertext has no index";
		break;
	case VIEW_HELP:
		entry = hyp_find_node(v->doc, v->doc->help_node);
		*msg = "This hypertext has no help page";
		break;
	case VIEW_HOME:
		entry = hyp_default_node(v->doc);
		break;
	}
	if (entry == v->page->num && which != VIEW_HOME)
		entry = HYP_NO_NODE;
	if (entry == HYP_NO_NODE || entry >= v->doc->num_entries || !HYP_IS_TEXT(v->doc->entries[entry].type))
		return HYP_NO_NODE;
	return entry;
}

int view_relative(ehv_view *v, int which)
{
	const char *msg;
	unsigned short entry;

	if (v->page == NULL)
		return VIEW_ERROR;
	entry = relative_entry(v, which, &msg);
	if (entry == HYP_NO_NODE)
	{
		set_status(v, msg, NULL, NULL);
		return VIEW_ERROR;
	}
	return view_goto(v, entry, 0);
}

int view_can(const ehv_view *v, int which)
{
	const char *msg;

	if (which == VIEW_BACK)
		return v->nhist > 0;
	if (which == VIEW_FORWARD)
		return v->nfwd > 0;
	if (which == VIEW_SEEALSO)
		return view_xref_count(v) > 0;
	return relative_entry(v, which, &msg) != HYP_NO_NODE;
}

long view_run_line(const ehv_view *v, long r)
{
	unsigned short l;

	if (v->page == NULL || r < 0)
		return -1;
	for (l = 0; l < v->page->nlines; l++)
	{
		const hyp_line *ln = &v->page->lines[l];

		if ((unsigned long) r >= ln->first_run && (unsigned long) r < ln->first_run + ln->nruns)
			return l;
	}
	return -1;
}

long view_select_link(ehv_view *v, int dir, long first, long last)
{
	const hyp_page *pg = v->page;
	long r, start, sel_line;

	if (pg == NULL || pg->nruns == 0)
		return -1;
	sel_line = view_run_line(v, v->sel);
	if (v->sel >= 0 && sel_line >= first && sel_line <= last)
		start = v->sel + dir;
	else if (dir > 0)
		start = first >= 0 && first < pg->nlines ? (long) pg->lines[first].first_run : 0;
	else
	{
		long l = last < pg->nlines ? last : pg->nlines - 1;

		start = l >= 0 ? (long) (pg->lines[l].first_run + pg->lines[l].nruns) - 1 : -1;
	}
	for (r = start; r >= 0 && (unsigned long) r < pg->nruns; r += dir)
	{
		if (pg->runs[r].kind == HYP_RUN_LINK)
		{
			v->sel = r;
			return view_run_line(v, r);
		}
	}
	return -1;
}

static long page_link_at(const hyp_page *pg, long line, long col)
{
	const hyp_line *ln;
	long c = 0;
	unsigned short i;

	if (pg == NULL || line < 0 || line >= pg->nlines || col < 0)
		return -1;
	ln = &pg->lines[line];
	for (i = 0; i < ln->nruns; i++)
	{
		const hyp_run *run = &pg->runs[ln->first_run + i];

		if (col >= c && col < c + run->len)
			return run->kind == HYP_RUN_LINK ? (long) (ln->first_run + i) : -1;
		c += run->len;
	}
	return -1;
}

long view_link_at(const ehv_view *v, long line, long col)
{
	return page_link_at(v->page, line, col);
}

void view_popup_close(ehv_view *v)
{
	hyp_page_free(v->popup);
	v->popup = NULL;
}

long view_popup_link_at(const ehv_view *v, long line, long col)
{
	return page_link_at(v->popup, line, col);
}

int view_popup_follow(ehv_view *v, long r)
{
	hyp_run run;

	if (v->popup == NULL || r < 0 || (unsigned long) r >= v->popup->nruns)
		return VIEW_ERROR;
	run = v->popup->runs[r];             /* the pop-up goes, its run is kept */
	run.text = NULL;
	view_popup_close(v);
	return view_follow(v, &run);
}

long view_popup_cols(const ehv_view *v)
{
	const hyp_page *pg = v->popup;
	long cols = 0;
	unsigned short l, i;

	if (pg == NULL)
		return 0;
	for (l = 0; l < pg->nlines; l++)
	{
		const hyp_line *ln = &pg->lines[l];
		long c = 0, end = 0;

		for (i = 0; i < ln->nruns; i++)
		{
			const hyp_run *run = &pg->runs[ln->first_run + i];
			long k;

			for (k = 0; k < run->len; k++)
				if (run->text[k] != ' ')
					end = c + k + 1;
			c += run->len;
		}
		if (end > cols)
			cols = end;
	}
	for (i = 0; i < pg->ngfx; i++)
	{
		const hyp_gfx *g = &pg->gfx[i];
		long right = (g->x > 0 ? g->x - 1 : 0) + (g->w > 0 ? g->w : -g->w);

		if (g->type != HYP_GFX_IMAGE && right > cols)
			cols = right;
	}
	return cols;
}

unsigned long view_page_text(const ehv_view *v, char *buf, unsigned long size)
{
	unsigned long n = 0, i, r;

	if (size == 0)
		return 0;
	for (i = 0; v->page != NULL && i < v->page->nlines; i++)
	{
		const hyp_line *l = &v->page->lines[i];

		for (r = l->first_run; r < l->first_run + l->nruns && r < v->page->nruns; r++)
		{
			const hyp_run *run = &v->page->runs[r];
			unsigned long k = run->len;

			if (run->text == NULL)
				continue;
			if (k > size - 1 - n)
				k = size - 1 - n;
			memcpy(buf + n, run->text, k);
			n += k;
		}
		if (n < size - 1)
			buf[n++] = '\n';
	}
	buf[n] = '\0';
	return n;
}

int view_park(ehv_view *v, view_parked *p)
{
	memset(p, 0, sizeof(*p));
	if (v->page != NULL)
	{
		p->here.path = dupstr(v->doc->path);
		p->here.title = dupstr(hyp_page_title(v->doc, v->page));
		if (p->here.path == NULL || p->here.title == NULL)
		{
			hist_free(&p->here);
			memset(p, 0, sizeof(*p));
			return -1;
		}
		p->here.node = v->page->num;
		p->here.top = v->top;
	}
	p->left = v->left;
	p->hist = v->hist;
	p->nhist = v->nhist;
	p->cap_hist = v->cap_hist;
	p->fwd = v->fwd;
	p->nfwd = v->nfwd;
	p->cap_fwd = v->cap_fwd;
	v->hist = v->fwd = NULL;
	v->nhist = v->cap_hist = v->nfwd = v->cap_fwd = 0;
	view_free(v);                 /* the page and hypertext; v empty */
	return 0;
}

int view_unpark(ehv_view *v, view_parked *p)
{
	int rc = VIEW_OK;

	view_free(v);
	v->hist = p->hist;
	v->nhist = p->nhist;
	v->cap_hist = p->cap_hist;
	v->fwd = p->fwd;
	v->nfwd = p->nfwd;
	v->cap_fwd = p->cap_fwd;
	p->hist = p->fwd = NULL;
	p->nhist = p->cap_hist = p->nfwd = p->cap_fwd = 0;
	if (p->here.path != NULL)
	{
		rc = show_hist(v, &p->here);
		if (rc == VIEW_OK)
			v->left = p->left;
	}
	view_parked_free(p);
	return rc;
}

void view_parked_free(view_parked *p)
{
	unsigned long i;

	hist_free(&p->here);
	for (i = 0; i < p->nhist; i++)
		hist_free(&p->hist[i]);
	free(p->hist);
	for (i = 0; i < p->nfwd; i++)
		hist_free(&p->fwd[i]);
	free(p->fwd);
	memset(p, 0, sizeof(*p));
}

void view_title(const ehv_view *v, char *buf, unsigned long size)
{
	const char *db, *pt;

	if (size == 0)
		return;
	if (v->page == NULL)
	{
		snprintf(buf, size, "EHypView");
		return;
	}
	db = v->doc->database != NULL ? v->doc->database : fs_basename(v->doc->path);
	pt = hyp_page_title(v->doc, v->page);
	snprintf(buf, size, " %s - %s ", db, pt);
}
