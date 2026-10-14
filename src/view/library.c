/*
 * EHypView - the library: every hypertext found, and one search box.
 *
 * Copyright (C) the EHypView authors.  Free software under the GNU
 * General Public License, version 2 or (at your option) any later
 * version; see COPYING.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "hypfile.h"
#include "library.h"

#define MAX_PAGE_ROWS 100
#define ENOUGH_FOR_NO_SIMILAR 20

static const char *guide_title(const ehv_guide *g)
{
	return g->database != NULL && g->database[0] != '\0' ? g->database : g->key;
}

void lib_init(ehv_library *lib, const ehv_index *idx)
{
	memset(lib, 0, sizeof(*lib));
	lib->idx = idx;
	lib->sel = -1;
	lib_rebuild(lib);
}

void lib_free(ehv_library *lib)
{
	ehv_fts_free(lib->fts);
	free(lib->rows);
	memset(lib, 0, sizeof(*lib));
	lib->sel = -1;
}

static int add_row(ehv_library *lib, unsigned char kind, unsigned long ref, const char *text)
{
	lib_row *r;

	if (lib->nrows == lib->cap)
	{
		unsigned long ncap = lib->cap ? lib->cap * 2 : 64;
		lib_row *p = realloc(lib->rows, ncap * sizeof(lib_row));

		if (p == NULL)
			return -1;
		lib->rows = p;
		lib->cap = ncap;
	}
	r = &lib->rows[lib->nrows++];
	r->kind = kind;
	r->ref = ref;
	r->text = text;
	return 0;
}

static int selectable(unsigned char kind)
{
	return kind == LIB_GUIDE || kind == LIB_PAGE || kind == LIB_ACTION || kind == LIB_TEXT;
}

/* ---------------------------------------------------------------- */
/* the full list                                                     */

static const ehv_index *cmp_idx;

static int cmp_guides(const void *x, const void *y)
{
	const ehv_guide *g = &cmp_idx->guides[*(const unsigned long *) x];
	const ehv_guide *h = &cmp_idx->guides[*(const unsigned long *) y];
	int c;

	/* hypertexts without a subject come last */
	if ((g->subject == NULL) != (h->subject == NULL))
		return g->subject == NULL ? 1 : -1;
	if (g->subject != NULL && (c = atari_casecmp(g->subject, h->subject)) != 0)
		return c;
	if ((c = atari_casecmp(guide_title(g), guide_title(h))) != 0)
		return c;
	return strcmp(g->path, h->path);
}

static int build_all(ehv_library *lib)
{
	const ehv_index *idx = lib->idx;
	unsigned long *order, n = 0, i;
	const char *group = NULL;
	int first = 1;

	order = malloc((idx->nguides + 1) * sizeof(unsigned long));
	if (order == NULL)
		return -1;
	for (i = 0; i < idx->nguides; i++)
		if (idx->guides[i].error == HYP_OK)
			order[n++] = i;
	cmp_idx = idx;
	qsort(order, n, sizeof(unsigned long), cmp_guides);
	for (i = 0; i < n; i++)
	{
		const char *subj = idx->guides[order[i]].subject;

		if (first || (subj == NULL) != (group == NULL) ||
			(subj != NULL && atari_casecmp(subj, group) != 0))
		{
			if (add_row(lib, LIB_GROUP, 0, subj != NULL ? subj : "Other hypertexts") < 0)
			{
				free(order);
				return -1;
			}
			group = subj;
			first = 0;
		}
		if (add_row(lib, LIB_GUIDE, order[i], NULL) < 0)
		{
			free(order);
			return -1;
		}
	}
	lib->nguides = n;
	free(order);
	if (n == 0)
	{
		snprintf(lib->note, sizeof(lib->note), "No hypertexts found yet.");
		return add_row(lib, LIB_NOTE, 0, NULL);
	}
	return 0;
}

/* ---------------------------------------------------------------- */
/* searching                                                         */

static void fold(const char *s, char *out, size_t size)
{
	size_t i;

	for (i = 0; s[i] != '\0' && i < size - 1; i++)
		out[i] = (char) atari_tolower((unsigned char) s[i]);
	out[i] = '\0';
}

/* does `s` contain the folded query `q`? */
static int contains(const char *s, const char *q)
{
	size_t ql = strlen(q);

	for (; *s != '\0'; s++)
	{
		size_t i = 0;

		while (i < ql && s[i] != '\0' && (char) atari_tolower((unsigned char) s[i]) == q[i])
			i++;
		if (i == ql)
			return 1;
	}
	return 0;
}

typedef struct {
	unsigned long sym;
	unsigned int rank;
} match;

static int cmp_match(const void *x, const void *y)
{
	const match *a = x, *b = y;
	const ehv_sym *s = &cmp_idx->syms[a->sym], *t = &cmp_idx->syms[b->sym];
	int c;

	if (a->rank != b->rank)
		return a->rank < b->rank ? -1 : 1;
	if ((c = atari_casecmp(s->name, t->name)) != 0)
		return c;
	if (cmp_idx->guides[s->guide].root != cmp_idx->guides[t->guide].root)
		return cmp_idx->guides[s->guide].root < cmp_idx->guides[t->guide].root ? -1 : 1;
	return a->sym < b->sym ? -1 : a->sym > b->sym;
}

/* rank 0 same name, 1 starts with, 2 contains, 3 similar */
static int build_pages(ehv_library *lib, const char *folded)
{
	const ehv_index *idx = lib->idx;
	size_t ql = strlen(folded);
	match *m;
	unsigned long n = 0, i, cap = 64;
	int pass;

	m = malloc(cap * sizeof(match));
	if (m == NULL)
		return -1;
	for (pass = 0; pass < 2; pass++)
	{
		unsigned int limit = ql <= 4 ? 1 : ql <= 8 ? 2 : 3;

		/* similar names only when the plain matches are few */
		if (pass == 1 && (n >= ENOUGH_FOR_NO_SIMILAR || ql < 3))
			break;
		for (i = 0; i < idx->nsyms; i++)
		{
			const char *name = idx->syms[i].name;
			int rank;

			if (pass == 0)
			{
				if (atari_casecmp(name, lib->query) == 0)
					rank = 0;
				else if (atari_ncasecmp(name, lib->query, ql) == 0)
					rank = 1;
				else if (contains(name, folded))
					rank = 2;
				else
					continue;
			} else
			{
				if (contains(name, folded) || ehv_similarity(lib->query, name, limit) > limit)
					continue;
				rank = 3;
			}
			if (n == cap)
			{
				match *p = realloc(m, cap * 2 * sizeof(match));

				if (p == NULL)
				{
					free(m);
					return -1;
				}
				m = p;
				cap *= 2;
			}
			m[n].sym = i;
			m[n].rank = (unsigned int) rank;
			n++;
		}
	}
	cmp_idx = idx;
	qsort(m, n, sizeof(match), cmp_match);
	if (n > MAX_PAGE_ROWS)
		n = MAX_PAGE_ROWS;
	lib->npages = n;
	if (n > 0 && add_row(lib, LIB_GROUP, 0, "Pages") < 0)
	{
		free(m);
		return -1;
	}
	for (i = 0; i < n; i++)
	{
		if (add_row(lib, LIB_PAGE, m[i].sym, NULL) < 0)
		{
			free(m);
			return -1;
		}
	}
	free(m);
	return 0;
}

static int build_query(ehv_library *lib)
{
	const ehv_index *idx = lib->idx;
	char folded[LIB_QUERY_MAX];
	unsigned long i, ng = 0;
	int heading = 0;

	fold(lib->query, folded, sizeof(folded));
	if (build_pages(lib, folded) < 0)
		return -1;
	for (i = 0; i < idx->nguides; i++)
	{
		const ehv_guide *g = &idx->guides[i];

		if (g->error != HYP_OK)
			continue;
		if (!contains(guide_title(g), folded) && !contains(g->key, folded) &&
			!(g->subject != NULL && contains(g->subject, folded)))
			continue;
		if (!heading && add_row(lib, LIB_GROUP, 0, "Hypertexts") < 0)
			return -1;
		heading = 1;
		if (add_row(lib, LIB_GUIDE, i, NULL) < 0)
			return -1;
		ng++;
	}
	lib->nguides = ng;

	if (lib->fts == NULL)
		return add_row(lib, LIB_ACTION, 0, NULL);
	{
		unsigned long nh, h;

		ehv_fts_hits(lib->fts, &nh);
		if (add_row(lib, LIB_GROUP, 0, "Full text") < 0)
			return -1;
		for (h = 0; h < nh; h++)
			if (add_row(lib, LIB_TEXT, h, NULL) < 0)
				return -1;
		if (!ehv_fts_done(lib->fts))
			snprintf(lib->note, sizeof(lib->note), "Searching... %lu pages", ehv_fts_pages(lib->fts));
		else if (nh == 0)
			snprintf(lib->note, sizeof(lib->note), "Not found in %lu pages", ehv_fts_pages(lib->fts));
		else
			snprintf(lib->note, sizeof(lib->note), "Found on %lu of %lu pages%s", nh,
				ehv_fts_pages(lib->fts), nh >= EHV_FTS_MAX_HITS ? " (stopped)" : "");
		return add_row(lib, LIB_NOTE, 0, NULL);
	}
}

/* ---------------------------------------------------------------- */

static long first_selectable(const ehv_library *lib, long from, int kind)
{
	long i;

	for (i = from < 0 ? 0 : from; (unsigned long) i < lib->nrows; i++)
		if (selectable(lib->rows[i].kind) && (kind < 0 || lib->rows[i].kind == kind))
			return i;
	return -1;
}

static int rebuild(ehv_library *lib, int keep_selection, int want_text)
{
	unsigned char kind = 0;
	unsigned long ref = 0;
	int had = keep_selection && lib->sel >= 0 && (unsigned long) lib->sel < lib->nrows;
	int rc;

	if (had)
	{
		kind = lib->rows[lib->sel].kind;
		ref = lib->rows[lib->sel].ref;
	}
	lib->nrows = 0;
	lib->npages = lib->nguides = 0;
	lib->note[0] = '\0';
	lib->sel = -1;
	if (lib->idx == NULL)
	{
		snprintf(lib->note, sizeof(lib->note), "Looking for hypertexts...");
		return add_row(lib, LIB_NOTE, 0, NULL);
	}
	rc = lib->query[0] == '\0' ? build_all(lib) : build_query(lib);
	if (rc < 0)
		return -1;
	if (want_text)
		lib->sel = first_selectable(lib, 0, LIB_TEXT);
	if (lib->sel < 0 && had && !(kind == LIB_ACTION && want_text))
	{
		unsigned long i;

		for (i = 0; i < lib->nrows; i++)
			if (lib->rows[i].kind == kind && lib->rows[i].ref == ref)
				lib->sel = (long) i;
	}
	if (lib->sel < 0)
		lib->sel = first_selectable(lib, 0, -1);
	if (lib->top >= (long) lib->nrows)
		lib->top = 0;
	return 0;
}

int lib_rebuild(ehv_library *lib)
{
	return rebuild(lib, 1, 0);
}

int lib_set_index(ehv_library *lib, const ehv_index *idx)
{
	/* hits refer to the old index's entries */
	ehv_fts_free(lib->fts);
	lib->fts = NULL;
	lib->idx = idx;
	return rebuild(lib, 0, 0);
}

int lib_set_query(ehv_library *lib, const char *query)
{
	if (strcmp(lib->query, query) == 0)
		return 0;
	ehv_fts_free(lib->fts);
	lib->fts = NULL;
	strncpy(lib->query, query, sizeof(lib->query) - 1);
	lib->query[sizeof(lib->query) - 1] = '\0';
	lib->top = 0;
	return rebuild(lib, 0, 0);
}

long lib_move(ehv_library *lib, long delta)
{
	long i = lib->sel, step = delta < 0 ? -1 : 1, moved = 0, last = lib->sel;

	if (delta == 0)
		return lib->sel;
	for (i = lib->sel + step; i >= 0 && (unsigned long) i < lib->nrows; i += step)
	{
		if (selectable(lib->rows[i].kind))
		{
			last = i;
			if (++moved == (delta < 0 ? -delta : delta))
				break;
		}
	}
	lib->sel = last;
	return lib->sel;
}

long lib_select(ehv_library *lib, long row)
{
	if (row < 0 || (unsigned long) row >= lib->nrows || !selectable(lib->rows[row].kind))
		return lib->sel;
	lib->sel = row;
	return row;
}

int lib_activate(ehv_library *lib, const char **path, const char **node, unsigned short *line)
{
	const lib_row *r;

	*path = *node = NULL;
	*line = 0;
	if (lib->idx == NULL || lib->sel < 0 || (unsigned long) lib->sel >= lib->nrows)
		return LIB_NOTHING;
	r = &lib->rows[lib->sel];
	switch (r->kind)
	{
	case LIB_GUIDE:
		*path = lib->idx->guides[r->ref].path;
		return LIB_OPEN;
	case LIB_PAGE:
		{
			const ehv_sym *s = &lib->idx->syms[r->ref];

			*path = lib->idx->guides[s->guide].path;
			*node = s->node;
			*line = s->kind == EHV_SYM_LABEL ? s->line : 0;
			return LIB_OPEN;
		}
	case LIB_TEXT:
		{
			unsigned long n;
			const ehv_fts_hit *h = ehv_fts_hits(lib->fts, &n);

			if (r->ref >= n)
				return LIB_NOTHING;
			*path = lib->idx->guides[h[r->ref].guide].path;
			*node = h[r->ref].node;
			*line = h[r->ref].line;
			return LIB_OPEN;
		}
	case LIB_ACTION:
		return LIB_FULLTEXT;
	}
	return LIB_NOTHING;
}

int lib_fulltext_start(ehv_library *lib)
{
	ehv_fts_free(lib->fts);
	lib->fts = ehv_fts_begin(lib->idx, lib->query);
	if (lib->fts == NULL)
		return -1;
	return rebuild(lib, 0, 0);
}

int lib_fulltext_step(ehv_library *lib, unsigned int budget)
{
	unsigned long before, after;
	int r;

	if (lib->fts == NULL || ehv_fts_done(lib->fts))
		return 1;
	ehv_fts_hits(lib->fts, &before);
	r = ehv_fts_step(lib->fts, budget);
	if (r < 0)
		return -1;
	ehv_fts_hits(lib->fts, &after);
	if (after != before || r == 1)
	{
		/* jump to the first hit when it arrives, unless the user moved */
		int want_text = before == 0 && (lib->sel < 0 || lib->rows[lib->sel].kind != LIB_PAGE);

		if (rebuild(lib, 1, want_text) < 0)
			return -1;
	} else
	{
		snprintf(lib->note, sizeof(lib->note), "Searching... %lu pages", ehv_fts_pages(lib->fts));
	}
	return r;
}

void lib_row_text(const ehv_library *lib, unsigned long row, char *buf, unsigned long size)
{
	const lib_row *r;

	if (size == 0)
		return;
	buf[0] = '\0';
	if (row >= lib->nrows)
		return;
	r = &lib->rows[row];
	switch (r->kind)
	{
	case LIB_GROUP:
		snprintf(buf, size, "%s", r->text);
		break;
	case LIB_GUIDE:
		snprintf(buf, size, "  %s", guide_title(&lib->idx->guides[r->ref]));
		break;
	case LIB_PAGE:
		{
			const ehv_sym *s = &lib->idx->syms[r->ref];

			if (s->kind == EHV_SYM_ALIAS)
				snprintf(buf, size, "  %s -> %s", s->name, s->node);
			else if (s->kind == EHV_SYM_LABEL)
				snprintf(buf, size, "  %s (in %s)", s->name, s->node);
			else
				snprintf(buf, size, "  %s", s->name);
		}
		break;
	case LIB_ACTION:
		snprintf(buf, size, "  Search the full text for \"%s\"", lib->query);
		break;
	case LIB_TEXT:
		{
			unsigned long n;
			const ehv_fts_hit *h = ehv_fts_hits(lib->fts, &n);

			if (r->ref < n)
				snprintf(buf, size, "  %s: %s", h[r->ref].node, h[r->ref].context);
		}
		break;
	case LIB_NOTE:
		snprintf(buf, size, "  %s", lib->note);
		break;
	}
}

void lib_row_detail(const ehv_library *lib, unsigned long row, char *buf, unsigned long size)
{
	const lib_row *r;

	if (size == 0)
		return;
	buf[0] = '\0';
	if (row >= lib->nrows)
		return;
	r = &lib->rows[row];
	switch (r->kind)
	{
	case LIB_GUIDE:
		snprintf(buf, size, "%s", lib->idx->guides[r->ref].key);
		break;
	case LIB_PAGE:
		snprintf(buf, size, "%s", guide_title(&lib->idx->guides[lib->idx->syms[r->ref].guide]));
		break;
	case LIB_TEXT:
		{
			unsigned long n;
			const ehv_fts_hit *h = ehv_fts_hits(lib->fts, &n);

			if (r->ref < n)
				snprintf(buf, size, "%s", guide_title(&lib->idx->guides[h[r->ref].guide]));
		}
		break;
	}
}
