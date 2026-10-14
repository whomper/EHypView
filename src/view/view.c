/*
 * EHypView - what a viewer window shows and how it moves.
 *
 * Copyright (C) the EHypView authors.  Free software under the GNU
 * General Public License, version 2 or (at your option) any later
 * version; see COPYING.
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

void view_free(ehv_view *v)
{
	unsigned long i;

	hyp_page_free(v->page);
	hyp_close(v->doc);
	for (i = 0; i < v->nhist; i++)
		free(v->hist[i].path);
	free(v->hist);
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

static int push_history(ehv_view *v)
{
	view_hist *h;

	if (v->page == NULL)
		return 0;
	if (v->nhist == v->cap_hist)
	{
		unsigned long ncap = v->cap_hist ? v->cap_hist * 2 : 16;
		view_hist *p = realloc(v->hist, ncap * sizeof(view_hist));

		if (p == NULL)
			return -1;
		v->hist = p;
		v->cap_hist = ncap;
	}
	h = &v->hist[v->nhist];
	h->path = dupstr(v->doc->path);
	if (h->path == NULL)
		return -1;
	h->node = v->page->num;
	h->top = v->top;
	v->nhist++;
	return 0;
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
	hyp_page_free(v->page);
	v->page = pg;
	v->left = 0;
	v->sel = -1;
	if (top >= pg->nlines)
		top = pg->nlines > 0 ? pg->nlines - 1 : 0;
	v->top = top < 0 ? 0 : top;
	return VIEW_OK;
}

/* open a document (or reuse the open one) and show `node` */
static int show(ehv_view *v, const char *path, const char *node, long top, int push)
{
	hyp_doc *doc = v->doc;
	unsigned short entry;
	int err, rc;

	if (doc == NULL || strcmp(doc->path, path) != 0)
	{
		doc = hyp_open(path, &err);
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
		hyp_page_free(v->page);
		v->page = NULL;
		hyp_close(v->doc);
		v->doc = doc;
	}
	rc = show_entry(v, entry, top);
	if (rc != VIEW_OK && push)
	{
		/* nothing is shown instead: forget the history entry again */
		v->nhist--;
		free(v->hist[v->nhist].path);
	}
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
		v->nhist--;
		free(v->hist[v->nhist].path);
		return VIEW_ERROR;
	}
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
			: v->idx->syms[t->suggest[i]].name;

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

	context(v, &ctx);
	if (ehv_resolve_request(&ctx, args, &t) < 0)
	{
		set_status(v, "Out of memory", NULL, NULL);
		return VIEW_ERROR;
	}
	return show_target(v, args, &t);
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
	case HYP_NODE_POPUP:
		return view_goto(v, run->target, run->target_line);
	case HYP_NODE_EXTERNAL:
		context(v, &ctx);
		if (ehv_resolve_xref(&ctx, e->name, &t) < 0)
			return VIEW_ERROR;
		if (t.status != EHV_NO_FILE && t.status != EHV_NOT_FOUND && run->target_line)
			t.line = run->target_line;
		return show_target(v, e->name, &t);
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

int view_back(ehv_view *v)
{
	view_hist h;
	int rc;

	if (v->nhist == 0)
	{
		set_status(v, "This is the first page", NULL, NULL);
		return VIEW_ERROR;
	}
	h = v->hist[v->nhist - 1];
	v->nhist--;
	if (v->doc != NULL && strcmp(v->doc->path, h.path) == 0)
		rc = show_entry(v, h.node, h.top);
	else
	{
		/* another hypertext: reopen it by its entry name */
		hyp_doc *doc;
		int err;

		doc = hyp_open(h.path, &err);
		if (doc == NULL)
		{
			set_status(v, fs_basename(h.path), ": ", hyp_strerror(err));
			free(h.path);
			return VIEW_ERROR;
		}
		hyp_page_free(v->page);
		v->page = NULL;
		hyp_close(v->doc);
		v->doc = doc;
		rc = show_entry(v, h.node, h.top);
	}
	free(h.path);
	if (rc == VIEW_OK)
		v->status[0] = '\0';
	return rc;
}

int view_relative(ehv_view *v, int which)
{
	unsigned short entry = HYP_NO_NODE;
	const char *msg = NULL;

	if (v->page == NULL)
		return VIEW_ERROR;
	switch (which)
	{
	case VIEW_NEXT:
		entry = v->doc->entries[v->page->num].next;
		msg = "This is the last page";
		break;
	case VIEW_PREV:
		entry = v->doc->entries[v->page->num].prev;
		msg = "This is the first page";
		break;
	case VIEW_TOC:
		entry = v->doc->entries[v->page->num].toc;
		msg = "No table of contents";
		break;
	case VIEW_INDEX:
		entry = hyp_find_node(v->doc, "Index");
		msg = "This hypertext has no index";
		break;
	case VIEW_HELP:
		entry = hyp_find_node(v->doc, v->doc->help_node);
		msg = "This hypertext has no help page";
		break;
	case VIEW_HOME:
		entry = hyp_default_node(v->doc);
		break;
	}
	if (entry == v->page->num && which != VIEW_HOME)
		entry = HYP_NO_NODE;
	if (entry == HYP_NO_NODE || entry >= v->doc->num_entries || !HYP_IS_TEXT(v->doc->entries[entry].type))
	{
		set_status(v, msg, NULL, NULL);
		return VIEW_ERROR;
	}
	return view_goto(v, entry, 0);
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

long view_link_at(const ehv_view *v, long line, long col)
{
	const hyp_line *ln;
	long c = 0;
	unsigned short i;

	if (v->page == NULL || line < 0 || line >= v->page->nlines || col < 0)
		return -1;
	ln = &v->page->lines[line];
	for (i = 0; i < ln->nruns; i++)
	{
		const hyp_run *run = &v->page->runs[ln->first_run + i];

		if (col >= c && col < c + run->len)
			return run->kind == HYP_RUN_LINK ? (long) (ln->first_run + i) : -1;
		c += run->len;
	}
	return -1;
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
