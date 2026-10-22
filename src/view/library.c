/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the library: every hypertext found, and one search box.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "hypfile.h"
#include "fsys.h"
#include "library.h"
#include "names.h"

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
	lib->pane_kind = PANE_ALL;
	lib_rebuild(lib);
}

void lib_free(ehv_library *lib)
{
	ehv_fts_free(lib->fts);
	ehv_hits_free(&lib->found);
	free(lib->rows);
	free(lib->order);
	free(lib->tint);
	free(lib->panes);
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
	r->flags = r->tint = 0;
	r->ref = ref;
	r->text = text;
	return 0;
}

static int selectable(const lib_row *r)
{
	return r->kind == LIB_GUIDE || r->kind == LIB_PAGE || r->kind == LIB_ACTION || r->kind == LIB_TEXT ||
		r->kind == LIB_BOOKMARK || r->kind == LIB_RECENT || (r->kind == LIB_GROUP && (r->flags & LIB_FOLDS));
}

/* the headings folded: their names, hashed in any case */
static unsigned long hash(const char *s)
{
	unsigned long h = 5381;

	for (; *s != '\0'; s++)
		h = (h << 5) + h + (unsigned char) atari_tolower((unsigned char) *s);
	return h;
}

static int is_folded(const ehv_library *lib, const char *text)
{
	unsigned long h = hash(text);
	unsigned int i;

	for (i = 0; i < lib->nfolded; i++)
		if (lib->folded[i] == h)
			return 1;
	return 0;
}

static void set_folded(ehv_library *lib, const char *text, int on)
{
	unsigned long h = hash(text);
	unsigned int i;

	for (i = 0; i < lib->nfolded; i++)
		if (lib->folded[i] == h)
		{
			if (!on)
				lib->folded[i] = lib->folded[--lib->nfolded];
			return;
		}
	if (on && lib->nfolded < LIB_FOLD_MAX)
		lib->folded[lib->nfolded++] = h;
}

/* s holds q, in any case (q as typed) */
static int has_text(const char *s, const char *q)
{
	size_t n = strlen(q);

	for (; *s != '\0'; s++)
		if (atari_ncasecmp(s, q, n) == 0)
			return 1;
	return n == 0;
}

/* the bookmarks: all of them, or those whose title or page has the
   query; under a heading that folds in the Outline */
static int build_bookmarks(ehv_library *lib, int folds)
{
	unsigned int i;
	long heading = -1;

	for (i = 0; lib->bm != NULL && i < lib->bm->n; i++)
	{
		const ehv_bookmark *b = &lib->bm->b[i];

		if (!has_text(b->title, lib->query) && !has_text(b->node, lib->query))
			continue;
		if (heading < 0)
		{
			if (add_row(lib, LIB_GROUP, 0, "Bookmarks") < 0)
				return -1;
			heading = (long) lib->nrows - 1;
			if (folds)
			{
				lib->rows[heading].flags = LIB_FOLDS;
				if (is_folded(lib, "Bookmarks"))
					lib->rows[heading].flags |= LIB_FOLDED;
			}
		}
		lib->rows[heading].ref++;
		if (!(lib->rows[heading].flags & LIB_FOLDED) && add_row(lib, LIB_BOOKMARK, i, NULL) < 0)
			return -1;
	}
	return 0;
}

/* the pages read last, newest first */
static int build_recent(ehv_library *lib)
{
	unsigned int i;

	for (i = 0; lib->recent != NULL && i < lib->recent->n; i++)
		if (add_row(lib, LIB_RECENT, i, NULL) < 0)
			return -1;
	return 0;
}

/* ---------------------------------------------------------------- */
/* the full list                                                     */

/* the subject a hypertext is filed under: "Documentation/Editors" and
   "Dokumentation/Editoren" under their part, as Editors, since
   Documentation alone says nothing of what a hypertext is about (Erez,
   22 Oct 2026); "Documentation" without a part stays as it is */
const char *lib_subject(const char *s)
{
	static const char *const skip[] = { "Documentation/", "Dokumentation/" };
	unsigned int k;

	for (k = 0; s != NULL && k < sizeof(skip) / sizeof(skip[0]); k++)
	{
		size_t n = strlen(skip[k]);

		if (atari_ncasecmp(s, skip[k], n) == 0 && s[n] != '\0' && s[n] != '/')
		{
			s += n;
			k = (unsigned int) -1;     /* "Documentation/Dokumentation/..." too */
		}
	}
	return s;
}

/* a subject's own part: up to its first "/" ("Personal" of
   "Personal/Calendar"); the whole of one that starts with "/" */
static size_t subj_top(const char *s)
{
	const char *slash = strchr(s, '/');

	return slash != NULL && slash > s && slash[1] != '\0' ? (size_t) (slash - s) : strlen(s);
}

/* the part after it ("Editors"), or NULL */
static const char *subj_part(const char *s)
{
	size_t n = subj_top(s);

	return s[n] == '/' ? s + n + 1 : NULL;
}

/* the same subject, in any case: the whole, or only the own part */
static int same_top(const char *a, const char *b)
{
	size_t n = subj_top(a);

	return n == subj_top(b) && atari_ncasecmp(a, b, n) == 0;
}

/* the subjects' order: by their own part, then the one without a part
   first, then by the part; in any case */
static int subj_cmp(const char *a, const char *b)
{
	size_t na = subj_top(a), nb = subj_top(b), i;
	const char *pa, *pb;

	for (i = 0; i < na && i < nb; i++)
		if (atari_tolower((unsigned char) a[i]) != atari_tolower((unsigned char) b[i]))
			return atari_tolower((unsigned char) a[i]) - atari_tolower((unsigned char) b[i]);
	if (na != nb)
		return na < nb ? -1 : 1;
	pa = subj_part(a);
	pb = subj_part(b);
	if ((pa == NULL) != (pb == NULL))
		return pa == NULL ? -1 : 1;
	return pa == NULL ? 0 : atari_casecmp(pa, pb);
}

static int same_subject(const char *a, const char *b)
{
	return subj_cmp(a, b) == 0;
}

void lib_subject_text(const char *subj, char *buf, unsigned long size)
{
	const char *part = subj_part(subj);

	if (part != NULL)
		snprintf(buf, size, "%.*s \xfa %s", (int) subj_top(subj), subj, part);
	else
		snprintf(buf, size, "%s", subj);
}

static const ehv_index *cmp_idx;

static int cmp_guides(const void *x, const void *y)
{
	const ehv_guide *g = &cmp_idx->guides[*(const unsigned long *) x];
	const ehv_guide *h = &cmp_idx->guides[*(const unsigned long *) y];
	const char *a = lib_subject(g->subject), *b = lib_subject(h->subject);
	int c;

	/* hypertexts without a subject come last */
	if ((a == NULL) != (b == NULL))
		return a == NULL ? 1 : -1;
	if (a != NULL && (c = subj_cmp(a, b)) != 0)
		return c;
	if ((c = atari_casecmp(guide_title(g), guide_title(h))) != 0)
		return c;
	return strcmp(g->path, h->path);
}

/* the readable hypertexts in the order listed, and their tints: made
   again for each new index */
static int make_order(ehv_library *lib)
{
	const ehv_index *idx = lib->idx;
	unsigned long i, n = 0;
	unsigned int t = 0;
	const char *group = NULL;

	if (idx != NULL && lib->order != NULL && lib->order_of == idx)
		return 0;
	free(lib->order);
	free(lib->tint);
	lib->order = NULL;
	lib->tint = NULL;
	lib->norder = 0;
	lib->order_of = NULL;
	if (idx == NULL)
		return 0;
	lib->order = malloc((idx->nguides + 1) * sizeof(unsigned long));
	lib->tint = malloc(idx->nguides + 1);
	if (lib->order == NULL || lib->tint == NULL)
		return -1;
	for (i = 0; i < idx->nguides; i++)
	{
		lib->tint[i] = 0;
		if (idx->guides[i].error == HYP_OK)
			lib->order[n++] = i;
	}
	cmp_idx = idx;
	qsort(lib->order, n, sizeof(unsigned long), cmp_guides);
	for (i = 0; i < n; i++)
	{
		const char *subj = lib_subject(idx->guides[lib->order[i]].subject);

		if (subj == NULL)
			continue;
		if (group == NULL || !same_top(subj, group))      /* a part: its subject's tint */
		{
			t = t < 255 ? t + 1 : 1;
			group = subj;
		}
		lib->tint[lib->order[i]] = (unsigned char) t;
	}
	lib->norder = n;
	lib->order_of = idx;
	return 0;
}

static int same_group(const char *a, const char *b)
{
	return (a == NULL) == (b == NULL) && (a == NULL || same_top(a, b));
}

/* a hypertext with this subject is in the pane */
static int in_pane(const lib_pane *p, const char *subj)
{
	if (p->kind == PANE_OTHER)
		return subj == NULL;
	return subj != NULL && (p->level ? same_subject(subj, p->subj) : same_top(subj, p->subj));
}

/* the next line of the Subjects pane at this level, after `k` (the
   panes and the groups come in the same order) */
static long next_pane(const ehv_library *lib, long k, unsigned char level)
{
	while (++k < (long) lib->npanes)
		if (lib->panes[k].kind == PANE_SUBJECT && lib->panes[k].level == level)
			return k;
	return -1;
}

/*
 * The hypertexts, from the order: all under their headings (which fold
 * when `folds`), or, for a pane of one subject, only its own and no
 * heading.  The parts of a subject have headings of their own under
 * its heading (or in its pane), which don't fold.
 */
static int build_groups(ehv_library *lib, int folds, const lib_pane *only)
{
	const ehv_index *idx = lib->idx;
	unsigned long i, n = 0;
	long heading = -1, sub = -1, k = -1;
	const char *group = NULL, *part_of = NULL;

	for (i = 0; i < lib->norder; i++)
	{
		unsigned long g = lib->order[i];
		const char *subj = lib_subject(idx->guides[g].subject);
		long under;

		if (only != NULL)
		{
			if (!in_pane(only, subj))
				continue;
		} else if (heading < 0 || !same_group(subj, group))
		{
			const char *text = "Other hypertexts";

			if (subj != NULL && (k = next_pane(lib, k, 0)) >= 0)
				text = lib->panes[k].text;
			if (add_row(lib, LIB_GROUP, 0, text) < 0)
				return -1;
			heading = (long) lib->nrows - 1;
			lib->rows[heading].tint = lib->tint[g];
			if (folds)
			{
				lib->rows[heading].flags = LIB_FOLDS;
				if (is_folded(lib, lib->rows[heading].text))
					lib->rows[heading].flags |= LIB_FOLDED;
			}
			group = subj;
			sub = -1;
			part_of = NULL;
		}
		n++;
		if (heading >= 0)
		{
			lib->rows[heading].ref++;
			if (lib->rows[heading].flags & LIB_FOLDED)
				continue;
		}
		/* a part of the subject: under a heading of its own */
		under = -1;
		if (subj != NULL && subj_part(subj) != NULL && (only == NULL || only->level == 0))
		{
			if (sub < 0 || part_of == NULL || !same_subject(subj, part_of))
			{
				if (add_row(lib, LIB_GROUP, 0, subj_part(subj)) < 0)
					return -1;
				sub = (long) lib->nrows - 1;
				lib->rows[sub].flags = LIB_SUB;
				lib->rows[sub].tint = lib->tint[g];
				part_of = subj;
			}
			under = sub;
		}
		if (under >= 0)
			lib->rows[under].ref++;
		if (add_row(lib, LIB_GUIDE, g, NULL) < 0)
			return -1;
		lib->rows[lib->nrows - 1].tint = lib->tint[g];
	}
	lib->nguides = n;
	if (lib->norder == 0)
	{
		snprintf(lib->note, sizeof(lib->note), "No hypertexts found yet.");
		return add_row(lib, LIB_NOTE, 0, NULL);
	}
	return 0;
}

/* ---------------------------------------------------------------- */
/* the Subjects pane                                                 */

static int add_pane(ehv_library *lib, unsigned char kind, unsigned char tint, unsigned long count,
		    const char *text, int len)
{
	lib_pane *p;

	if (lib->npanes == lib->cap_panes)
	{
		unsigned long ncap = lib->cap_panes ? lib->cap_panes * 2 : 16;

		p = realloc(lib->panes, ncap * sizeof(lib_pane));
		if (p == NULL)
			return -1;
		lib->panes = p;
		lib->cap_panes = ncap;
	}
	p = &lib->panes[lib->npanes++];
	p->kind = kind;
	p->tint = tint;
	p->level = 0;
	p->count = count;
	p->subj = NULL;
	snprintf(p->text, sizeof(p->text), "%.*s", len, text);
	return 0;
}

/* the name a pane is found again by, over a new index: the subject, or
   the subject and its part */
static void pane_key(const lib_pane *p, char *buf, size_t size)
{
	if (p->kind != PANE_SUBJECT)
		snprintf(buf, size, "%s", p->text);
	else if (p->level)
		snprintf(buf, size, "%.*s/%s", (int) subj_top(p->subj), p->subj, subj_part(p->subj));
	else
		snprintf(buf, size, "%.*s", (int) subj_top(p->subj), p->subj);
}

/* Bookmarks, Recently read, All hypertexts, each subject, the ones with
   none; the one chosen found again by its name */
static int build_panes(ehv_library *lib)
{
	const ehv_index *idx = lib->idx;
	unsigned long i, other = 0;
	long k;

	long top = -1;
	char key[sizeof(lib->pane_name)];

	lib->npanes = 0;
	if (lib->bm != NULL && lib->bm->n > 0 && add_pane(lib, PANE_BOOKMARKS, 0, lib->bm->n, "Bookmarks", LIB_PANE_TEXT) < 0)
		return -1;
	if (lib->recent != NULL && lib->recent->n > 0 &&
	    add_pane(lib, PANE_RECENT, 0, lib->recent->n, "Recently read", LIB_PANE_TEXT) < 0)
		return -1;
	if (idx != NULL && add_pane(lib, PANE_ALL, 0, lib->norder, "All hypertexts", LIB_PANE_TEXT) < 0)
		return -1;
	for (i = 0; i < lib->norder; i++)
	{
		const char *subj = lib_subject(idx->guides[lib->order[i]].subject);
		const char *part;
		lib_pane *last;

		if (subj == NULL)
		{
			other++;
			continue;
		}
		/* the subject, then its part, if any */
		if (top >= 0 && same_top(lib->panes[top].subj, subj))
			lib->panes[top].count++;
		else
		{
			if (add_pane(lib, PANE_SUBJECT, lib->tint[lib->order[i]], 1, subj,
				     (int) subj_top(subj)) < 0)
				return -1;
			top = (long) lib->npanes - 1;
			lib->panes[top].subj = subj;
		}
		if ((part = subj_part(subj)) == NULL)
			continue;
		last = &lib->panes[lib->npanes - 1];
		if (last->level == 1 && same_subject(last->subj, subj))
			last->count++;
		else
		{
			if (add_pane(lib, PANE_SUBJECT, lib->tint[lib->order[i]], 1, part, LIB_PANE_TEXT) < 0)
				return -1;
			last = &lib->panes[lib->npanes - 1];
			last->level = 1;
			last->subj = subj;
		}
	}
	if (other > 0 && add_pane(lib, PANE_OTHER, 0, other, "Other hypertexts", LIB_PANE_TEXT) < 0)
		return -1;

	lib->pane = -1;
	for (k = 0; k < (long) lib->npanes && lib->pane < 0; k++)
	{
		pane_key(&lib->panes[k], key, sizeof(key));
		if (lib->panes[k].kind == lib->pane_kind &&
		    (lib->pane_kind != PANE_SUBJECT || atari_casecmp(key, lib->pane_name) == 0))
			lib->pane = k;
	}
	for (k = 0; k < (long) lib->npanes && lib->pane < 0; k++)
		if (lib->panes[k].kind == PANE_ALL)
			lib->pane = k;
	if (lib->pane < 0 && lib->npanes > 0)
		lib->pane = 0;
	return 0;
}

/* the rows of the pane chosen */
static int build_pane(ehv_library *lib)
{
	const lib_pane *p = lib->pane >= 0 ? &lib->panes[lib->pane] : NULL;
	unsigned int i;

	if (p != NULL && p->kind == PANE_BOOKMARKS)
	{
		for (i = 0; i < lib->bm->n; i++)
			if (add_row(lib, LIB_BOOKMARK, i, NULL) < 0)
				return -1;
		return 0;
	}
	if (p != NULL && p->kind == PANE_RECENT)
		return build_recent(lib);
	if (lib->idx == NULL)
	{
		snprintf(lib->note, sizeof(lib->note), "Looking for hypertexts...");
		return add_row(lib, LIB_NOTE, 0, NULL);
	}
	return build_groups(lib, 0, p != NULL && (p->kind == PANE_SUBJECT || p->kind == PANE_OTHER) ? p : NULL);
}

/* the Outline: the pages read last, the bookmarks, the groups */
static int build_outline(ehv_library *lib)
{
	if (build_recent(lib) < 0 || build_bookmarks(lib, 1) < 0)
		return -1;
	if (lib->idx == NULL)
	{
		snprintf(lib->note, sizeof(lib->note), "Looking for hypertexts...");
		return add_row(lib, LIB_NOTE, 0, NULL);
	}
	return build_groups(lib, 1, NULL);
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

/*
 * The names found: rank 0 the same name, 1 starting with it, 2
 * containing it, 3 similar (when the others are few), each in the names'
 * order.  The names are read through once, from the index file (a piece
 * at a time, one open), and kept for the search: a list rebuilt for
 * another reason (a full-text hit, a bookmark) doesn't read them again.
 */
static int build_pages(ehv_library *lib, const char *folded)
{
	const ehv_index *idx = lib->idx;
	size_t ql = strlen(folded);
	unsigned int limit = ql <= 4 ? 1 : ql <= 8 ? 2 : 3;
	unsigned long plain = 0, i;
	ehv_hits rank[4];
	ehv_names nm;
	ehv_sym s;
	int k, rc = 0;

	if (lib->found_of != idx || strcmp(lib->found_q, lib->query) != 0)
	{
		ehv_hits_free(&lib->found);
		lib->found_of = NULL;
		for (k = 0; k < 4; k++)
			ehv_hits_init(&rank[k]);
		if (ehv_names_open(&nm, idx, ehv_names_piece(), 1) < 0)
			return -1;
		while (rc == 0 && ehv_names_next(&nm, &s) == 1)
		{
			if (atari_casecmp(s.name, lib->query) == 0)
				k = 0;
			else if (atari_ncasecmp(s.name, lib->query, ql) == 0)
				k = 1;
			else if (contains(s.name, folded))
				k = 2;
			else if (plain < ENOUGH_FOR_NO_SIMILAR && ql >= 3 && ehv_similarity(lib->query, s.name, limit) <= limit)
				k = 3;
			else
				continue;
			if (k < 3)
				plain++;
			if (rank[k].n < MAX_PAGE_ROWS && ehv_hits_add(&rank[k], &s) < 0)
				rc = -1;
		}
		ehv_names_close(&nm);
		/* similar names only when the plain matches are few */
		for (k = 0; k < 4; k++)
		{
			for (i = 0; i < rank[k].n; i++)
				if (rc == 0 && lib->found.n < MAX_PAGE_ROWS && (k < 3 || plain < ENOUGH_FOR_NO_SIMILAR))
					rc = ehv_hits_add(&lib->found, &rank[k].s[i]);
			ehv_hits_free(&rank[k]);
		}
		if (rc < 0)
		{
			ehv_hits_free(&lib->found);
			return -1;
		}
		lib->found_of = idx;
		strcpy(lib->found_q, lib->query);
	}
	lib->npages = lib->found.n;
	if (lib->found.n > 0 && add_row(lib, LIB_GROUP, 0, "Pages") < 0)
		return -1;
	for (i = 0; i < lib->found.n; i++)
		if (add_row(lib, LIB_PAGE, i, NULL) < 0)
			return -1;
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
		lib->rows[lib->nrows - 1].tint = lib->tint != NULL ? lib->tint[i] : 0;
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
		if (selectable(&lib->rows[i]) && (kind < 0 || lib->rows[i].kind == kind))
			return i;
	return -1;
}

/* the row chosen first: a hypertext, bookmark, page...: not a heading
   nor a page read lately, unless there is nothing else */
static long first_choice(const ehv_library *lib)
{
	unsigned long i;

	for (i = 0; i < lib->nrows; i++)
		if (selectable(&lib->rows[i]) && lib->rows[i].kind != LIB_GROUP && lib->rows[i].kind != LIB_RECENT)
			return (long) i;
	return first_selectable(lib, 0, -1);
}

static int rebuild(ehv_library *lib, int keep_selection, int want_text)
{
	unsigned char kind = 0;
	unsigned long ref = 0;
	char heading[64];
	int had = keep_selection && lib->sel >= 0 && (unsigned long) lib->sel < lib->nrows;
	int rc;

	heading[0] = '\0';
	if (had)
	{
		kind = lib->rows[lib->sel].kind;
		ref = lib->rows[lib->sel].ref;
		if (kind == LIB_GROUP)
			snprintf(heading, sizeof(heading), "%s", lib->rows[lib->sel].text);
	}
	lib->nrows = 0;
	lib->npages = lib->nguides = 0;
	lib->note[0] = '\0';
	lib->sel = -1;
	if (make_order(lib) < 0 || build_panes(lib) < 0)
		return -1;
	if (lib->query[0] != '\0')
	{
		if (build_bookmarks(lib, 0) < 0)
			return -1;
		if (lib->idx == NULL)
		{
			snprintf(lib->note, sizeof(lib->note), "Looking for hypertexts...");
			rc = add_row(lib, LIB_NOTE, 0, NULL);
		} else
			rc = build_query(lib);
	} else if (lib->layout == LIB_PANES)
		rc = build_pane(lib);
	else
		rc = build_outline(lib);
	if (rc < 0)
		return -1;
	if (want_text)
		lib->sel = first_selectable(lib, 0, LIB_TEXT);
	if (lib->sel < 0 && had && !(kind == LIB_ACTION && want_text))
	{
		unsigned long i;

		for (i = 0; i < lib->nrows; i++)
			if (lib->rows[i].kind == kind && selectable(&lib->rows[i]) &&
			    (kind == LIB_GROUP ? strcmp(lib->rows[i].text, heading) == 0 : lib->rows[i].ref == ref))
				lib->sel = (long) i;
	}
	if (lib->sel < 0)
		lib->sel = first_choice(lib);
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
	lib->order_of = NULL;             /* made again, even at the same address */
	lib->found_of = NULL;
	ehv_hits_free(&lib->found);
	return rebuild(lib, 0, 0);
}

int lib_set_bookmarks(ehv_library *lib, const ehv_bookmarks *bm)
{
	lib->bm = bm;
	return rebuild(lib, 1, 0);
}

int lib_set_recent(ehv_library *lib, const ehv_bookmarks *recent)
{
	lib->recent = recent;
	return rebuild(lib, 1, 0);
}

int lib_set_layout(ehv_library *lib, int layout)
{
	if (layout != LIB_PANES)
		layout = LIB_OUTLINE;
	if (layout == lib->layout)
		return 0;
	lib->layout = layout;
	lib->top = 0;
	return rebuild(lib, 1, 0);
}

int lib_fold(ehv_library *lib, int how)
{
	long h = lib->sel;
	int folded, want;

	if (h < 0 || (unsigned long) h >= lib->nrows)
		return 0;
	if (lib->rows[h].kind != LIB_GROUP)
	{
		if (how != 0)
			return 0;
		while (h >= 0 && (lib->rows[h].kind != LIB_GROUP || (lib->rows[h].flags & LIB_SUB)))
			h--;                       /* past a part's heading, to its subject's */
		if (h < 0)
			return 0;
	}
	if (!(lib->rows[h].flags & LIB_FOLDS))
		return 0;
	folded = (lib->rows[h].flags & LIB_FOLDED) != 0;
	want = how < 0 ? !folded : how == 0;
	if (want == folded)
	{
		if (h == lib->sel)
			return 0;
		lib->sel = h;                  /* folded already: to its heading */
		return 1;
	}
	set_folded(lib, lib->rows[h].text, want);
	lib->sel = h;
	return rebuild(lib, 1, 0) < 0 ? -1 : 1;
}

long lib_pane_select(ehv_library *lib, long i)
{
	if (lib->npanes == 0)
		return -1;
	if (i < 0)
		i = 0;
	if (i >= (long) lib->npanes)
		i = (long) lib->npanes - 1;
	if (i == lib->pane)
		return i;
	lib->pane = i;
	lib->pane_kind = lib->panes[i].kind;
	pane_key(&lib->panes[i], lib->pane_name, sizeof(lib->pane_name));
	lib->top = 0;
	rebuild(lib, 0, 0);
	return lib->pane;
}

long lib_pane_move(ehv_library *lib, long delta)
{
	return lib_pane_select(lib, lib->pane + delta);
}

const ehv_guide *lib_row_guide(const ehv_library *lib, unsigned long row)
{
	const lib_row *r;
	const ehv_bookmarks *from;
	long g;

	if (row >= lib->nrows || lib->idx == NULL)
		return NULL;
	r = &lib->rows[row];
	if (r->kind == LIB_GUIDE)
		return &lib->idx->guides[r->ref];
	from = r->kind == LIB_BOOKMARK ? lib->bm : r->kind == LIB_RECENT ? lib->recent : NULL;
	if (from == NULL || r->ref >= from->n)
		return NULL;
	g = ehv_index_find_path(lib->idx, from->b[r->ref].path);
	return g >= 0 ? &lib->idx->guides[g] : NULL;
}

int lib_selected_bookmark(const ehv_library *lib)
{
	if (lib->sel < 0 || (unsigned long) lib->sel >= lib->nrows || lib->rows[lib->sel].kind != LIB_BOOKMARK)
		return -1;
	return (int) lib->rows[lib->sel].ref;
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
		if (selectable(&lib->rows[i]))
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
	if (row < 0 || (unsigned long) row >= lib->nrows || !selectable(&lib->rows[row]))
		return lib->sel;
	lib->sel = row;
	return row;
}

int lib_activate(ehv_library *lib, const char **path, const char **node, unsigned short *line)
{
	const lib_row *r;

	*path = *node = NULL;
	*line = 0;
	if (lib->sel < 0 || (unsigned long) lib->sel >= lib->nrows)
		return LIB_NOTHING;
	r = &lib->rows[lib->sel];
	if (r->kind == LIB_BOOKMARK || r->kind == LIB_RECENT)
	{
		const ehv_bookmarks *from = r->kind == LIB_BOOKMARK ? lib->bm : lib->recent;

		if (from == NULL || r->ref >= from->n)
			return LIB_NOTHING;
		*path = from->b[r->ref].path;
		*node = from->b[r->ref].node;
		return LIB_OPEN;
	}
	if (lib->idx == NULL)
		return LIB_NOTHING;
	switch (r->kind)
	{
	case LIB_GUIDE:
		*path = lib->idx->guides[r->ref].path;
		return LIB_OPEN;
	case LIB_PAGE:
		{
			const ehv_sym *s = &lib->found.s[r->ref];

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
			const ehv_sym *s = &lib->found.s[r->ref];

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
	case LIB_BOOKMARK:
		if (lib->bm != NULL && r->ref < lib->bm->n)
			snprintf(buf, size, "  %s", lib->bm->b[r->ref].title);
		break;
	case LIB_RECENT:
		if (lib->recent != NULL && r->ref < lib->recent->n)
			snprintf(buf, size, "  %s", lib->recent->b[r->ref].title);
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
		snprintf(buf, size, "%s", guide_title(&lib->idx->guides[lib->found.s[r->ref].guide]));
		break;
	case LIB_TEXT:
		{
			unsigned long n;
			const ehv_fts_hit *h = ehv_fts_hits(lib->fts, &n);

			if (r->ref < n)
				snprintf(buf, size, "%s", guide_title(&lib->idx->guides[h[r->ref].guide]));
		}
		break;
	case LIB_BOOKMARK:
		if (lib->bm != NULL && r->ref < lib->bm->n)
			snprintf(buf, size, "%s", fs_basename(lib->bm->b[r->ref].path));
		break;
	case LIB_RECENT:
		if (lib->recent != NULL && r->ref < lib->recent->n)
			snprintf(buf, size, "%s", fs_basename(lib->recent->b[r->ref].path));
		break;
	}
}

const char *lib_version(const char *v)
{
	const char *w;

	if (v == NULL)
		return "";
	while (*v == ' ')
		v++;
	if (atari_ncasecmp(v, "$VER:", 5) == 0)
		for (v += 5; *v == ' '; v++)
			;
	/* "basic.hyp 1.0 (13.10.2026)": the file's name left out */
	for (w = v; *w != '\0' && *w != ' '; w++)
		;
	if (w - v > 4 && atari_ncasecmp(w - 4, ".hyp", 4) == 0)
		for (v = w; *v == ' '; v++)
			;
	return v;
}

void lib_row_about(const ehv_library *lib, unsigned long row, char *buf, unsigned long size)
{
	const ehv_guide *g;
	const char *ver, *base;

	if (size == 0)
		return;
	buf[0] = '\0';
	if (row >= lib->nrows || lib->rows[row].kind != LIB_GUIDE)
		return;
	g = &lib->idx->guides[lib->rows[row].ref];
	ver = lib_version(g->version);
	if (g->author != NULL && g->author[0] != '\0' && ver[0] != '\0')
		snprintf(buf, size, "%s \xfa %s", g->author, ver);
	else if (g->author != NULL && g->author[0] != '\0')
		snprintf(buf, size, "%s", g->author);
	else if (ver[0] != '\0')
		snprintf(buf, size, "%s", ver);
	else
	{
		/* its folder */
		base = fs_basename(g->path);
		snprintf(buf, size, "%.*s", (int) (base - g->path), g->path);
	}
}
