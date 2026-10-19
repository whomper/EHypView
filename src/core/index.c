/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - index of all hypertexts: storage, lookup, cache file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "fsys.h"
#include "index.h"
#include "plat.h"

ehv_index *ehv_index_new(void)
{
	return calloc(1, sizeof(ehv_index));
}

static void free_tables(ehv_index *idx)
{
	free(idx->by_key);
	free(idx->by_path);
	free(idx->by_name);
	idx->by_key = idx->by_path = idx->by_name = NULL;
}

void ehv_index_free(ehv_index *idx)
{
	if (idx == NULL)
		return;
	free_tables(idx);
	free(idx->guides);
	free(idx->syms);
	free(idx->orphans);
	pool_free(&idx->pool);
	free(idx);
}

/* ---------------------------------------------------------------- */
/* building                                                          */

static int grow(void **arr, unsigned long *cap, unsigned long n, size_t sz)
{
	unsigned long ncap;
	void *p;

	if (n < *cap)
		return 0;
	ncap = *cap ? *cap * 2 : 64;
	p = realloc(*arr, ncap * sz);
	if (p == NULL)
		return -1;
	*arr = p;
	*cap = ncap;
	return 0;
}

int ehv_index_reserve(ehv_index *idx, unsigned long nguides, unsigned long nsyms)
{
	if (nguides > idx->cap_guides)
	{
		void *p = realloc(idx->guides, nguides * sizeof(ehv_guide));

		if (p == NULL)
			return -1;
		idx->guides = p;
		idx->cap_guides = nguides;
	}
	if (nsyms > idx->cap_syms)
	{
		void *p = realloc(idx->syms, nsyms * sizeof(ehv_sym));

		if (p == NULL)
			return -1;
		idx->syms = p;
		idx->cap_syms = nsyms;
	}
	return 0;
}

/* pool copy that reports failure for non-NULL input */
static int pdup(ehv_index *idx, const char **dst, const char *src)
{
	*dst = pool_strdup(&idx->pool, src);
	return src != NULL && *dst == NULL ? -1 : 0;
}

long ehv_index_add_guide(ehv_index *idx, const ehv_guide *src)
{
	ehv_guide *g;
	const char *base;
	size_t keylen;

	if (grow((void **) &idx->guides, &idx->cap_guides, idx->nguides, sizeof(ehv_guide)) < 0)
		return -1;
	g = &idx->guides[idx->nguides];
	*g = *src;
	g->first_sym = idx->nsyms;
	g->nsyms = 0;
	if (pdup(idx, &g->path, src->path) < 0 ||
		pdup(idx, &g->ref_path, src->ref_path) < 0 ||
		pdup(idx, &g->database, src->database) < 0 ||
		pdup(idx, &g->subject, src->subject) < 0 ||
		pdup(idx, &g->author, src->author) < 0 ||
		pdup(idx, &g->version, src->version) < 0 ||
		pdup(idx, &g->hostname, src->hostname) < 0 ||
		pdup(idx, &g->default_node, src->default_node) < 0 ||
		pdup(idx, &g->help_node, src->help_node) < 0 ||
		pdup(idx, &g->charset, src->charset) < 0 ||
		pdup(idx, &g->language, src->language) < 0)
		return -1;

	/* the key used for file name lookups: "AES.HYP" -> "AES" */
	base = fs_basename(g->path);
	keylen = strlen(base);
	if (fs_has_ext(base, ".hyp"))
		keylen -= 4;
	g->key = pool_strndup(&idx->pool, base, keylen);
	if (g->key == NULL)
		return -1;
	return (long) idx->nguides++;
}

int ehv_index_add_sym(ehv_index *idx, unsigned long guide, unsigned char kind,
                      unsigned short line, const char *name, const char *node, int from_ref)
{
	ehv_sym *s;

	if (grow((void **) &idx->syms, &idx->cap_syms, idx->nsyms, sizeof(ehv_sym)) < 0)
		return -1;
	s = &idx->syms[idx->nsyms];
	s->guide = guide;
	s->kind = kind;
	s->from_ref = (unsigned char) from_ref;
	s->line = line;
	s->name = pool_strdup(&idx->pool, name);
	if (s->name == NULL)
		return -1;
	if (node == NULL || node == name || strcmp(node, name) == 0)
		s->node = s->name;          /* pages: share the string */
	else if ((s->node = pool_strdup(&idx->pool, node)) == NULL)
		return -1;
	idx->nsyms++;
	idx->guides[guide].nsyms++;
	return 0;
}

long ehv_index_copy_guide(ehv_index *idx, const ehv_index *from, unsigned long g)
{
	const ehv_guide *src = &from->guides[g];
	long ng = ehv_index_add_guide(idx, src);
	unsigned long i;

	if (ng < 0)
		return -1;
	for (i = src->first_sym; i < src->first_sym + src->nsyms; i++)
	{
		const ehv_sym *s = &from->syms[i];

		if (s->from_ref == 2)
			continue;               /* ALL.REF data is merged afresh */
		if (ehv_index_add_sym(idx, (unsigned long) ng, s->kind, s->line, s->name, s->node, s->from_ref) < 0)
			return -1;
	}
	return ng;
}

/* one ALL.REF module merged into the guides it names */
static int merge_module(ehv_index *idx, const ref_db *db, unsigned long m)
{
	const ref_module *mod = &db->modules[m];
	char file[256];
	unsigned long g, matched = 0;

	if (mod->file == NULL || mod->file[0] == '\0')
		return 0;
	ref_module_filename(mod, file, sizeof(file));
	for (g = 0; g < idx->nguides; g++)
	{
		ehv_guide *gd = &idx->guides[g];
		const char *base = fs_basename(file);
		size_t kl = strlen(gd->key);
		unsigned long s;

		if (atari_ncasecmp(gd->key, base, kl) != 0 || atari_casecmp(base + kl, ".hyp") != 0)
			continue;
		matched++;
		if (gd->ref_path != NULL)
			continue;            /* its own REF file wins */
		for (s = mod->first; s < mod->first + mod->count; s++)
		{
			const ref_symbol *rs = &db->syms[s];
			unsigned char kind;

			if (rs->kind == REF_ALIAS)
				kind = EHV_SYM_ALIAS;
			else if (rs->kind == REF_LABEL)
				kind = EHV_SYM_LABEL;
			else
				continue;        /* pages come from the HYP itself */
			if (rs->node == NULL)
				continue;
			if (ehv_index_add_sym(idx, g, kind, rs->line, rs->name, rs->node, 2) < 0)
				return -1;
		}
	}
	if (matched == 0)
	{
		const char **p = realloc(idx->orphans, (idx->norphans + 1) * sizeof(char *));

		if (p == NULL)
			return -1;
		idx->orphans = p;
		if ((p[idx->norphans] = pool_strdup(&idx->pool, file)) == NULL)
			return -1;
		idx->norphans++;
	}
	return 0;
}

int ehv_index_merge_ref_some(ehv_index *idx, const ref_db *db, unsigned long *next, unsigned int budget)
{
	while (*next < db->nmodules)
	{
		if (budget-- == 0)
			return 0;
		if (merge_module(idx, db, (*next)++) < 0)
			return -1;
	}
	return 1;
}

int ehv_index_merge_ref(ehv_index *idx, const ref_db *db)
{
	unsigned long next = 0;

	return ehv_index_merge_ref_some(idx, db, &next, (unsigned int) -1) < 0 ? -1 : 0;
}

/* ---------------------------------------------------------------- */
/* lookup tables                                                     */

static int kind_rank(unsigned char kind)
{
	return kind == EHV_SYM_ALIAS ? 1 : kind == EHV_SYM_LABEL ? 2 : 0;
}

typedef struct {
	const char *str;
	unsigned long rank;       /* secondary key */
	unsigned long idx;        /* tertiary key, keeps the sort stable */
} sort_item;

static int cmp_casefold(const void *a, const void *b)
{
	const sort_item *x = a, *y = b;
	int c = atari_casecmp(x->str, y->str);

	if (c != 0)
		return c;
	if (x->rank != y->rank)
		return x->rank < y->rank ? -1 : 1;
	return x->idx < y->idx ? -1 : x->idx > y->idx;
}

static int cmp_exact(const void *a, const void *b)
{
	const sort_item *x = a, *y = b;
	int c = strcmp(x->str, y->str);

	if (c != 0)
		return c;
	return x->idx < y->idx ? -1 : x->idx > y->idx;
}

/*
 * A merge sort that stops after a number of moves and goes on where it
 * stopped: a sort of thousands of names in one go would hold an Atari
 * for seconds.  Stable, so with the comparisons' last key (the position)
 * it puts things in the same order as before (qsort, to version 1.6).
 */
typedef struct {
	sort_item *a, *b;
	unsigned long n, width, lo, mid, hi, i, j, k;
	int merging;
	int (*cmp)(const void *, const void *);
} msort;

static void ms_init(msort *m, sort_item *a, sort_item *b, unsigned long n,
                    int (*cmp)(const void *, const void *))
{
	memset(m, 0, sizeof(*m));
	m->a = a;
	m->b = b;
	m->n = n;
	m->width = 1;
	m->cmp = cmp;
}

/* 1 once sorted (in m->a), 0 when the budget ran out */
static int ms_step(msort *m, unsigned long budget)
{
	while (m->width < m->n)
	{
		if (!m->merging)
		{
			if (m->lo >= m->n)
			{
				sort_item *t = m->a;

				m->a = m->b;           /* a pass done: the next one from b */
				m->b = t;
				m->width *= 2;
				m->lo = 0;
				continue;
			}
			m->mid = m->lo + m->width < m->n ? m->lo + m->width : m->n;
			m->hi = m->mid + m->width < m->n ? m->mid + m->width : m->n;
			m->i = m->k = m->lo;
			m->j = m->mid;
			m->merging = 1;
		}
		while (m->k < m->hi)
		{
			if (budget == 0)
				return 0;
			budget--;
			if (m->i < m->mid && (m->j >= m->hi || m->cmp(&m->a[m->i], &m->a[m->j]) <= 0))
				m->b[m->k++] = m->a[m->i++];
			else
				m->b[m->k++] = m->a[m->j++];
		}
		m->merging = 0;
		m->lo = m->hi;
	}
	return 1;
}

/* what ehv_index_finish_some() does next */
enum { F_START, F_COUNT, F_PLACE, F_GROUPED, F_KEY, F_PATH, F_NAME, F_SORT, F_DONE };

struct ehv_finish {
	ehv_index *idx;
	int phase, table;               /* F_SORT: which table, F_KEY... */
	unsigned long i;
	unsigned long *start;           /* F_COUNT, F_PLACE: per guide */
	ehv_sym *ns;
	sort_item *items, *tmp;
	msort m;
};

#define MOVES  64                       /* sort moves per unit of budget */

ehv_finish *ehv_index_finish_begin(ehv_index *idx)
{
	ehv_finish *f = calloc(1, sizeof(*f));

	if (f != NULL)
		f->idx = idx;
	return f;
}

void ehv_index_finish_abort(ehv_finish *f)
{
	if (f == NULL)
		return;
	free(f->start);
	free(f->ns);
	free(f->items);
	free(f->tmp);
	free(f);
}

/* the sorted order into a table */
static unsigned long *table_of(const sort_item *items, unsigned long n)
{
	unsigned long *out = malloc((n ? n : 1) * sizeof(unsigned long));
	unsigned long i;

	if (out != NULL)
		for (i = 0; i < n; i++)
			out[i] = items[i].idx;
	return out;
}

int ehv_index_finish_some(ehv_finish *f, unsigned int budget)
{
	ehv_index *idx = f->idx;

	while (f->phase != F_DONE)
	{
		if (budget-- == 0)
			return 0;
		switch (f->phase)
		{
		case F_START:
		{
			unsigned long n = idx->nguides > idx->nsyms ? idx->nguides : idx->nsyms;

			free_tables(idx);
			f->items = malloc((n ? n : 1) * sizeof(sort_item));
			f->tmp = malloc((n ? n : 1) * sizeof(sort_item));
			f->start = calloc(idx->nguides + 1, sizeof(unsigned long));
			f->ns = malloc((idx->nsyms ? idx->nsyms : 1) * sizeof(ehv_sym));
			if (f->items == NULL || f->tmp == NULL || f->start == NULL || f->ns == NULL)
				return -1;
			f->i = 0;
			f->phase = F_COUNT;
			break;
		}
		case F_COUNT:
			/* the symbols grouped by guide, keeping their order: how
			   many each guide has */
		{
			unsigned long end = f->i + MOVES < idx->nsyms ? f->i + MOVES : idx->nsyms;

			for (; f->i < end; f->i++)
				f->start[idx->syms[f->i].guide + 1]++;
			if (f->i == idx->nsyms)
			{
				unsigned long g;

				for (g = 0; g < idx->nguides; g++)
					f->start[g + 1] += f->start[g];
				for (g = 0; g < idx->nguides; g++)
				{
					idx->guides[g].first_sym = f->start[g];
					idx->guides[g].nsyms = f->start[g + 1] - f->start[g];
				}
				f->i = 0;
				f->phase = F_PLACE;
			}
			break;
		}
		case F_PLACE:
		{
			unsigned long end = f->i + MOVES < idx->nsyms ? f->i + MOVES : idx->nsyms;

			for (; f->i < end; f->i++)
				f->ns[f->start[idx->syms[f->i].guide]++] = idx->syms[f->i];
			if (f->i == idx->nsyms)
				f->phase = F_GROUPED;
			break;
		}
		case F_GROUPED:
		{
			unsigned long g;

			free(idx->syms);
			idx->syms = f->ns;
			idx->cap_syms = idx->nsyms;
			f->ns = NULL;
			free(f->start);
			f->start = NULL;
			for (g = 0; g < idx->nguides; g++)
				if (idx->guides[g].nsyms == 0)
					idx->guides[g].first_sym = 0;
			f->i = 0;
			f->phase = F_KEY;
			break;
		}
		case F_KEY:
		case F_PATH:
		case F_NAME:
		{
			/* the items to sort, MOVES * 4 at a time: guides by key,
			   then by scan root, then scan order; by path; symbols by
			   name, then kind, then root */
			unsigned long n = f->phase == F_NAME ? idx->nsyms : idx->nguides;
			unsigned long end = f->i + MOVES * 4 < n ? f->i + MOVES * 4 : n;

			for (; f->i < end; f->i++)
			{
				sort_item *it = &f->items[f->i];

				if (f->phase == F_KEY)
				{
					it->str = idx->guides[f->i].key;
					it->rank = idx->guides[f->i].root;
				} else if (f->phase == F_PATH)
				{
					it->str = idx->guides[f->i].path;
					it->rank = 0;
				} else
				{
					const ehv_sym *sy = &idx->syms[f->i];

					it->str = sy->name;
					it->rank = (unsigned long) kind_rank(sy->kind) << 16 | idx->guides[sy->guide].root;
				}
				it->idx = f->i;
			}
			if (f->i < n)
				break;
			ms_init(&f->m, f->items, f->tmp, n, f->phase == F_PATH ? cmp_exact : cmp_casefold);
			f->table = f->phase;
			f->i = 0;
			f->phase = F_SORT;
			break;
		}
		case F_SORT:
			if (ms_step(&f->m, MOVES))
			{
				unsigned long *t = table_of(f->m.a, f->m.n);

				if (t == NULL)
					return -1;
				if (f->table == F_KEY)
				{
					idx->by_key = t;
					f->phase = F_PATH;
				} else if (f->table == F_PATH)
				{
					idx->by_path = t;
					f->phase = F_NAME;
				} else
				{
					idx->by_name = t;
					f->phase = F_DONE;
				}
			}
			break;
		}
	}
	return 1;
}

int ehv_index_finish(ehv_index *idx)
{
	ehv_finish *f = ehv_index_finish_begin(idx);
	int r = f != NULL ? ehv_index_finish_some(f, (unsigned int) -1) : -1;

	ehv_index_finish_abort(f);
	if (r < 0)
		free_tables(idx);
	return r < 0 ? -1 : 0;
}

long ehv_index_find_path(const ehv_index *idx, const char *path)
{
	unsigned long lo = 0, hi = idx->nguides;

	if (idx->by_path == NULL)
		return -1;
	while (lo < hi)
	{
		unsigned long mid = lo + (hi - lo) / 2;
		int c = strcmp(idx->guides[idx->by_path[mid]].path, path);

		if (c == 0)
			return (long) idx->by_path[mid];
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return -1;
}

/* first position in a casefold-sorted table whose string is >= key */
static unsigned long lower_bound(const ehv_index *idx, const unsigned long *table, unsigned long n,
                                 int syms, const char *key)
{
	unsigned long lo = 0, hi = n;

	while (lo < hi)
	{
		unsigned long mid = lo + (hi - lo) / 2;
		const char *s = syms ? idx->syms[table[mid]].name : idx->guides[table[mid]].key;

		if (atari_casecmp(s, key) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

unsigned long ehv_index_find_guides(const ehv_index *idx, const char *file,
                                    unsigned long *hits, unsigned long max)
{
	const char *base = fs_basename(file);
	char key[256];
	size_t len = strlen(base);
	unsigned long pos, n = 0;

	if (idx->by_key == NULL)
		return 0;
	if (fs_has_ext(base, ".hyp"))
		len -= 4;
	if (len >= sizeof(key))
		return 0;
	memcpy(key, base, len);
	key[len] = '\0';

	for (pos = lower_bound(idx, idx->by_key, idx->nguides, 0, key); pos < idx->nguides; pos++)
	{
		unsigned long g = idx->by_key[pos];

		if (atari_casecmp(idx->guides[g].key, key) != 0)
			break;
		if (n < max)
			hits[n] = g;
		n++;
	}
	return n;
}

unsigned long ehv_index_find_symbol(const ehv_index *idx, const char *name, long guide,
                                    unsigned long *hits, unsigned long max)
{
	unsigned long first, pos, n = 0;
	int pass;

	if (idx->by_name == NULL || name == NULL)
		return 0;
	first = lower_bound(idx, idx->by_name, idx->nsyms, 1, name);
	for (pass = 0; pass < 2 && n == 0; pass++)
	{
		for (pos = first; pos < idx->nsyms; pos++)
		{
			unsigned long i = idx->by_name[pos];
			const ehv_sym *s = &idx->syms[i];

			if (atari_casecmp(s->name, name) != 0)
				break;
			if (guide >= 0 && s->guide != (unsigned long) guide)
				continue;
			if (pass == 0 && strcmp(s->name, name) != 0)
				continue;
			if (n < max)
				hits[n] = i;
			n++;
		}
	}
	return n;
}

/* ---------------------------------------------------------------- */
/* "similar" matching                                                */

#define NORM_MAX 64

/* fold case, spell umlauts out, drop blanks, '_' and '-' */
static size_t normalize(const char *s, char *out)
{
	size_t n = 0;

	for (; *s != '\0' && n < NORM_MAX - 2; s++)
	{
		unsigned char c = atari_tolower((unsigned char) *s);
		const char *rep = NULL;

		switch (c)
		{
		case ' ': case '_': case '-': case '\t':
			continue;
		case 0x84: rep = "ae"; break;
		case 0x94: rep = "oe"; break;
		case 0x81: rep = "ue"; break;
		case 0x9E: rep = "ss"; break;
		}
		if (rep != NULL)
		{
			out[n++] = rep[0];
			out[n++] = rep[1];
		} else
		{
			out[n++] = (char) c;
		}
	}
	out[n] = '\0';
	return n;
}

static unsigned int min3(unsigned int a, unsigned int b, unsigned int c)
{
	unsigned int m = a < b ? a : b;

	return m < c ? m : c;
}

/* edit distance with adjacent transpositions, on normalized strings */
static unsigned int distance(const char *a, size_t la, const char *b, size_t lb, unsigned int limit)
{
	unsigned int rows[3][NORM_MAX + 1];
	unsigned int *prev2 = rows[0], *prev = rows[1], *cur = rows[2];
	size_t i, j;

	if ((la > lb ? la - lb : lb - la) > limit)
		return limit + 1;
	for (j = 0; j <= lb; j++)
		prev[j] = (unsigned int) j;
	for (i = 1; i <= la; i++)
	{
		unsigned int best;
		unsigned int *t;

		cur[0] = (unsigned int) i;
		best = cur[0];
		for (j = 1; j <= lb; j++)
		{
			unsigned int cost = a[i - 1] == b[j - 1] ? 0 : 1;

			cur[j] = min3(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost);
			if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] &&
				prev2[j - 2] + 1 < cur[j])
				cur[j] = prev2[j - 2] + 1;
			if (cur[j] < best)
				best = cur[j];
		}
		if (best > limit)
			return limit + 1;
		t = prev2;
		prev2 = prev;
		prev = cur;
		cur = t;
	}
	return prev[lb] > limit ? limit + 1 : prev[lb];
}

unsigned int ehv_similarity(const char *a, const char *b, unsigned int limit)
{
	char na[NORM_MAX], nb[NORM_MAX];
	size_t la = normalize(a, na), lb = normalize(b, nb);

	if (la == lb && memcmp(na, nb, la) == 0)
		return 0;
	/* a typed prefix ("appl_" for "appl_init") is a good match */
	if (la >= 3 && la < lb && memcmp(na, nb, la) == 0)
		return limit >= 1 ? 1 : limit + 1;
	return distance(na, la, nb, lb, limit);
}

static unsigned int limit_for(const char *name)
{
	char n[NORM_MAX];
	size_t len = normalize(name, n);

	return len <= 4 ? 1 : len <= 8 ? 2 : 3;
}

typedef struct {
	unsigned long idx;
	unsigned int dist;
	unsigned long rank;
} cand;

/* insert into a best-first list of at most max entries */
static void keep_best(cand *best, unsigned long *n, unsigned long max, cand c)
{
	unsigned long pos = *n;

	while (pos > 0 && (best[pos - 1].dist > c.dist ||
		(best[pos - 1].dist == c.dist && best[pos - 1].rank > c.rank)))
		pos--;
	if (pos >= max)
		return;
	if (*n < max)
		(*n)++;
	memmove(&best[pos + 1], &best[pos], (*n - 1 - pos) * sizeof(cand));
	best[pos] = c;
}

unsigned long ehv_index_suggest(const ehv_index *idx, const char *name, long guide,
                                unsigned long *hits, unsigned long max)
{
	unsigned int limit = limit_for(name);
	unsigned long first = 0, end = idx->nsyms, i, n = 0, k;
	cand *best;

	if (max == 0)
		return 0;
	if (guide >= 0)
	{
		first = idx->guides[guide].first_sym;
		end = first + idx->guides[guide].nsyms;
	}
	best = malloc(max * sizeof(cand));
	if (best == NULL)
		return 0;
	for (i = first; i < end; i++)
	{
		const ehv_sym *s = &idx->syms[i];
		unsigned int d = ehv_similarity(name, s->name, limit);
		int dup = 0;
		cand c;

		if (d > limit)
			continue;
		/* the same name in many guides ("Main", "Index"): list it once */
		for (k = 0; k < n; k++)
			if (atari_casecmp(idx->syms[best[k].idx].name, s->name) == 0)
				dup = 1;
		if (dup)
			continue;
		c.idx = i;
		c.dist = d;
		c.rank = (unsigned long) kind_rank(s->kind) << 16 | idx->guides[s->guide].root;
		keep_best(best, &n, max, c);
	}
	for (k = 0; k < n; k++)
		hits[k] = best[k].idx;
	free(best);
	return n;
}

unsigned long ehv_index_suggest_guides(const ehv_index *idx, const char *file,
                                       unsigned long *hits, unsigned long max)
{
	const char *base = fs_basename(file);
	char key[NORM_MAX];
	size_t len = strlen(base);
	unsigned int limit;
	unsigned long g, n = 0, k;
	cand *best;

	if (max == 0)
		return 0;
	if (fs_has_ext(base, ".hyp"))
		len -= 4;
	if (len >= sizeof(key))
		len = sizeof(key) - 1;
	memcpy(key, base, len);
	key[len] = '\0';
	limit = limit_for(key);
	best = malloc(max * sizeof(cand));
	if (best == NULL)
		return 0;
	for (g = 0; g < idx->nguides; g++)
	{
		unsigned int d = ehv_similarity(key, idx->guides[g].key, limit);
		cand c;

		if (d > limit)
			continue;
		c.idx = g;
		c.dist = d;
		c.rank = idx->guides[g].root;
		keep_best(best, &n, max, c);
	}
	for (k = 0; k < n; k++)
		hits[k] = best[k].idx;
	free(best);
	return n;
}

/* ---------------------------------------------------------------- */
/* cache file                                                        */

/*
 * Text, one record per line, fields split by tabs:
 *
 *   EHYPVIEW-INDEX 2
 *   C  nguides nsyms              how many follow, to make room at once
 *   R  size mtime path            ALL.REF as merged (when there was one)
 *   G  ... path ref               a hypertext, then its
 *   M  code value                   @database, @subject...
 *   N  kind line from name node     and its names, ALL.REF's too
 *   O  path                       an ALL.REF module with no hypertext
 *   K / P / S  numbers            the tables, sorted: by_key, by_path,
 *                                 by_name (version 2)
 *   END nguides nsyms
 *
 * Version 2 keeps the tables and ALL.REF's names, so loading is a
 * read through the file (in steps), with nothing to sort; a version 1
 * file (to EHypView 1.6) is loaded and sorted afresh.
 */
#define CACHE_MAGIC "EHYPVIEW-INDEX"
#define LINES       8                  /* lines read or written per step */
#define MARGIN      16384L             /* room for a step's lines past the buffer */

static unsigned long read_piece = 32768L, write_buffer = 32768L;
static void *(*buf_alloc)(unsigned long want, unsigned long *got);
static void (*buf_release)(void *p);

void ehv_index_pieces(unsigned long rp, unsigned long wb)
{
	read_piece = rp > 512 ? rp : 512;
	write_buffer = wb > MARGIN ? wb : MARGIN;
}

void ehv_index_buffers(void *(*alloc)(unsigned long want, unsigned long *got), void (*release)(void *p))
{
	buf_alloc = alloc;
	buf_release = release;
}
#define PER_LINE    32                 /* table numbers per line */

/* the cache text is built in memory and written in pieces */
typedef struct {
	char *data;
	unsigned long len, cap;
	int failed;
	int fixed;                /* from buf_alloc: can't grow */
} outbuf;

static void out_bytes(outbuf *o, const char *s, unsigned long n)
{
	if (o->failed)
		return;
	if (o->len + n + 1 > o->cap)
	{
		unsigned long ncap = o->cap ? o->cap : 16384;
		char *p;

		if (o->fixed)
		{
			o->failed = 1;           /* past MARGIN: not saved this time */
			return;
		}
		while (ncap < o->len + n + 1)
			ncap *= 2;
		p = realloc(o->data, ncap);
		if (p == NULL)
		{
			o->failed = 1;
			return;
		}
		o->data = p;
		o->cap = ncap;
	}
	memcpy(o->data + o->len, s, n);
	o->len += n;
}

static void out_str(outbuf *o, const char *s)
{
	out_bytes(o, s, strlen(s));
}

/* a number, as sprintf("%lu") would write it but much quicker */
static void out_num(outbuf *o, char before, unsigned long v)
{
	char b[12];
	int n = (int) sizeof(b);

	do
		b[--n] = (char) ('0' + v % 10);
	while ((v /= 10) != 0);
	b[--n] = before;
	out_bytes(o, b + n, (unsigned long) (sizeof(b) - (size_t) n));
}

static void put_field(outbuf *o, const char *s)
{
	out_bytes(o, "\t", 1);
	if (s == NULL)
		return;
	while (*s != '\0')
	{
		/* the plain run, then the one character to write escaped */
		size_t n = strcspn(s, "\\\t\n\r");

		out_bytes(o, s, (unsigned long) n);
		s += n;
		switch (*s)
		{
		case '\\': out_bytes(o, "\\\\", 2); break;
		case '\t': out_bytes(o, "\\t", 2); break;
		case '\n': out_bytes(o, "\\n", 2); break;
		case '\r': out_bytes(o, "\\r", 2); break;
		default: return;
		}
		s++;
	}
}

static void put_meta(outbuf *o, char code, const char *value)
{
	char c[2];

	if (value == NULL)
		return;
	c[0] = code;
	c[1] = '\0';
	out_str(o, "M");
	put_field(o, c);
	put_field(o, value);
	out_str(o, "\n");
}

static void put_guide(outbuf *o, const ehv_index *idx, unsigned long g)
{
	const ehv_guide *gd = &idx->guides[g];

	out_str(o, "G");
	out_num(o, '\t', gd->size);
	out_num(o, '\t', gd->mtime);
	out_num(o, '\t', gd->root);
	if (gd->error < 0)                  /* HYP_ERR_*: negative */
	{
		out_str(o, "\t");
		out_num(o, '-', (unsigned long) -gd->error);
	} else
		out_num(o, '\t', (unsigned long) gd->error);
	out_num(o, '\t', gd->os);
	out_num(o, '\t', gd->width);
	out_num(o, '\t', gd->stg_flags);
	out_num(o, '\t', gd->ref_size);
	out_num(o, '\t', gd->ref_mtime);
	put_field(o, gd->path);
	put_field(o, gd->ref_path ? gd->ref_path : "");
	out_str(o, "\n");
	put_meta(o, 'D', gd->database);
	put_meta(o, 'S', gd->subject);
	put_meta(o, 'A', gd->author);
	put_meta(o, 'V', gd->version);
	put_meta(o, 'H', gd->hostname);
	put_meta(o, 'F', gd->default_node);
	put_meta(o, 'P', gd->help_node);
	put_meta(o, 'C', gd->charset);
	put_meta(o, 'L', gd->language);
}

static void put_sym(outbuf *o, const ehv_sym *sy)
{
	out_str(o, "N");
	out_num(o, '\t', sy->kind);
	out_num(o, '\t', sy->line);
	out_num(o, '\t', sy->from_ref);
	put_field(o, sy->name);
	put_field(o, sy->node == sy->name ? "" : sy->node);
	out_str(o, "\n");
}

/* what ehv_index_save_some() does next */
enum { S_OPEN, S_HEAD, S_GUIDES, S_ORPHANS, S_TABLES, S_END, S_CLOSE, S_DONE };

struct ehv_saver {
	const ehv_index *idx;
	char *path, *tmp;
	outbuf o;                       /* written out when full, and at the end */
	unsigned long full;             /* when it is */
	int phase, table;
	int h;                          /* open only within a step (guidelines 2.2) */
	int made;                       /* tmp is on the disk */
	unsigned long i, k;             /* S_GUIDES: guide, and its name (0: the guide itself) */
};

ehv_saver *ehv_index_save_begin(const ehv_index *idx, const char *path)
{
	ehv_saver *v = calloc(1, sizeof(*v));

	if (v == NULL)
		return NULL;
	v->idx = idx;
	v->h = -1;
	v->path = malloc(strlen(path) + 1);
	v->tmp = pf_tmp_name(path);
	if (v->path == NULL || v->tmp == NULL)
	{
		ehv_index_save_abort(v);
		return NULL;
	}
	strcpy(v->path, path);
	/* the buffer at once, as big as can be up to write_buffer, and no
	   bigger than the file will be: an accessory has what it took at
	   boot, and an ST little of it */
	{
		unsigned long est = idx->nguides * 512UL + idx->nsyms * 64UL + (2 * idx->nguides + idx->nsyms) * 8UL;
		unsigned long cap = (est < write_buffer ? est : write_buffer) + MARGIN, got = 0;

		if (buf_alloc != NULL && (v->o.data = buf_alloc(cap, &got)) != NULL && got > 2 * MARGIN)
		{
			v->o.fixed = 1;
			cap = got;
		} else
		{
			if (v->o.data != NULL && buf_release != NULL)
				buf_release(v->o.data);
			v->o.data = NULL;
			while ((v->o.data = malloc(cap)) == NULL && cap > 2 * MARGIN)
				cap /= 2;
		}
		if (v->o.data == NULL)
		{
			ehv_index_save_abort(v);
			return NULL;
		}
		v->o.cap = cap;
		v->full = cap - MARGIN;
	}
	return v;
}

void ehv_index_save_abort(ehv_saver *v)
{
	if (v == NULL)
		return;
	if (v->h >= 0)
		pf_close(v->h);
	if (v->made)
		pf_remove(v->tmp);
	if (v->o.fixed && buf_release != NULL)
		buf_release(v->o.data);
	else
		free(v->o.data);
	free(v->path);
	free(v->tmp);
	free(v);
}

/* what is built goes to the file (a step of its own), opened for it:
   made by the first, added to by the next ones (the accessory's files
   belong to the program in front, which may end, and close them,
   between two steps) */
static int flush(ehv_saver *v)
{
	if (v->o.failed)
		return -1;
	if (v->o.len > 0)
	{
		if (v->h < 0 && (v->h = pf_open(v->tmp, v->made ? PF_APPEND : PF_WRITE)) < 0)
			return -1;
		v->made = 1;
		if (pf_write(v->h, v->o.data, (long) v->o.len) != (long) v->o.len)
			return -1;
	}
	v->o.len = 0;
	return 0;
}

static int save_steps(ehv_saver *v, unsigned int budget)
{
	const ehv_index *idx = v->idx;
	int k;

	while (v->phase != S_DONE)
	{
		if (budget-- == 0)
			return 0;
		if (v->o.len >= v->full)
		{
			if (flush(v) < 0)
				return -1;
			continue;
		}
		switch (v->phase)
		{
		case S_OPEN:                 /* made by the first flush() */
			v->phase = S_HEAD;
			break;
		case S_HEAD:
		{
			char head[32];

			sprintf(head, "%s %d\n", CACHE_MAGIC, EHV_CACHE_VERSION);
			out_str(&v->o, head);
		}
			out_str(&v->o, "C");
			out_num(&v->o, '\t', idx->nguides);
			out_num(&v->o, '\t', idx->nsyms);
			out_str(&v->o, "\n");
			if (idx->allref_path != NULL)
			{
				out_str(&v->o, "R");
				out_num(&v->o, '\t', idx->allref_size);
				out_num(&v->o, '\t', idx->allref_mtime);
				put_field(&v->o, idx->allref_path);
				out_str(&v->o, "\n");
			}
			v->i = v->k = 0;
			v->phase = S_GUIDES;
			break;
		case S_GUIDES:
			/* a few lines: a guide, or some of its names */
			for (k = 0; k < LINES && v->i < idx->nguides; k++)
			{
				const ehv_guide *gd = &idx->guides[v->i];

				if (v->k == 0)
					put_guide(&v->o, idx, v->i);
				if (v->k < gd->nsyms)
					put_sym(&v->o, &idx->syms[gd->first_sym + v->k]);
				if (++v->k >= gd->nsyms)
				{
					v->i++;
					v->k = 0;
				}
			}
			if (v->i >= idx->nguides)
				v->phase = S_ORPHANS;
			break;
		case S_ORPHANS:
			for (v->i = 0; v->i < idx->norphans; v->i++)
			{
				out_str(&v->o, "O");
				put_field(&v->o, idx->orphans[v->i]);
				out_str(&v->o, "\n");
			}
			v->i = 0;
			v->table = 0;
			v->phase = idx->by_key != NULL && idx->by_path != NULL && idx->by_name != NULL ? S_TABLES : S_END;
			break;
		case S_TABLES:
			for (k = 0; k < 4 && v->phase == S_TABLES; k++)
			{
				/* a line of one table, PER_LINE numbers */
				static const char code[3] = { 'K', 'P', 'S' };
				const unsigned long *t = v->table == 0 ? idx->by_key : v->table == 1 ? idx->by_path : idx->by_name;
				unsigned long n = v->table < 2 ? idx->nguides : idx->nsyms, m;

				if (v->i < n)
				{
					out_bytes(&v->o, &code[v->table], 1);
					for (m = 0; m < PER_LINE && v->i < n; m++)
						out_num(&v->o, m ? ' ' : '\t', t[v->i++]);
					out_str(&v->o, "\n");
				}
				if (v->i >= n)
				{
					v->i = 0;
					if (++v->table == 3)
						v->phase = S_END;
				}
			}
			break;
		case S_END:
			out_str(&v->o, "END");
			out_num(&v->o, '\t', idx->nguides);
			out_num(&v->o, '\t', idx->nsyms);
			out_str(&v->o, "\n");
			if (flush(v) < 0)
				return -1;
			v->phase = S_CLOSE;
			break;
		case S_CLOSE:
			if (v->h >= 0)
				pf_close(v->h);
			v->h = -1;
			/* TOS cannot rename over an existing file */
			pf_remove(v->path);
			if (pf_rename(v->tmp, v->path) != 0)
				return -1;
			v->made = 0;
			v->phase = S_DONE;
			break;
		}
	}
	return 1;
}

int ehv_index_save_some(ehv_saver *v, unsigned int budget)
{
	int r = save_steps(v, budget);

	if (v->h >= 0)
	{
		pf_close(v->h);
		v->h = -1;
	}
	return r;
}

int ehv_index_save(const ehv_index *idx, const char *path)
{
	ehv_saver *v = ehv_index_save_begin(idx, path);
	int r = v != NULL ? ehv_index_save_some(v, (unsigned int) -1) : -1;

	ehv_index_save_abort(v);
	return r < 0 ? -1 : 0;
}

/* next line of a loaded file, terminated in place; NULL at the end */
static char *next_line(char **pos, char *end)
{
	char *line = *pos, *nl;

	if (line >= end)
		return NULL;
	nl = memchr(line, '\n', (size_t) (end - line));
	if (nl == NULL)
		nl = end;
	*nl = '\0';
	*pos = nl + 1;
	return line;
}

/* split a line at tabs in place and unescape; returns field count */
static int split_fields(char *line, char **fields, int max)
{
	int n = 0;
	char *p = line;

	while (n < max)
	{
		char *out = p;

		fields[n++] = p;
		while (*p != '\0' && *p != '\t')
		{
			if (*p == '\\' && p[1] != '\0')
			{
				p++;
				*out++ = *p == 't' ? '\t' : *p == 'n' ? '\n' : *p == 'r' ? '\r' : *p;
				p++;
			} else
			{
				*out++ = *p++;
			}
		}
		if (*p == '\0')
		{
			*out = '\0';
			break;
		}
		p++;
		*out = '\0';
	}
	return n;
}

struct ehv_loader {
	ehv_index *idx;
	char *path;
	int h;                          /* open only within a step (guidelines 2.2) */
	int version, done;
	char *buf, *pos, *end;
	unsigned long size, got;
	long cur;                       /* the guide being read */
	unsigned long nt[3];            /* numbers read into each table */
	ehv_finish *fin;                /* a version 1 file: sorted afresh */
};

/* the file is looked at in the first step: opened, its size from the
   open file (one open, no look at the folder) */
ehv_loader *ehv_index_load_begin(const char *path)
{
	ehv_loader *l = calloc(1, sizeof(*l));

	if (l == NULL)
		return NULL;
	l->cur = -1;
	l->idx = ehv_index_new();
	l->h = -1;
	if ((l->path = malloc(strlen(path) + 1)) != NULL)
		strcpy(l->path, path);
	if (l->idx == NULL || l->path == NULL)
	{
		ehv_index_load_abort(l);
		return NULL;
	}
	return l;
}

void ehv_index_load_abort(ehv_loader *l)
{
	if (l == NULL)
		return;
	if (l->h >= 0)
		pf_close(l->h);
	ehv_index_finish_abort(l->fin);
	ehv_index_free(l->idx);
	free(l->buf);
	free(l->path);
	free(l);
}

/* a number of a line; quicker than strtoul() */
static unsigned long num_of(const char *p)
{
	unsigned long v = 0;

	while (*p >= '0' && *p <= '9')
		v = v * 10 + (unsigned long) (*p++ - '0');
	return v;
}

/* the numbers of a table line, after its tab */
static int read_table(ehv_loader *l, int t, char *p)
{
	ehv_index *idx = l->idx;
	unsigned long **tab = t == 0 ? &idx->by_key : t == 1 ? &idx->by_path : &idx->by_name;
	unsigned long n = t < 2 ? idx->nguides : idx->nsyms;

	if (l->version < 2)
		return -1;
	if (*tab == NULL && (*tab = malloc((n ? n : 1) * sizeof(unsigned long))) == NULL)
		return -1;
	while (*p != '\0')
	{
		char *e;
		unsigned long v = strtoul(p, &e, 10);

		if (e == p || v >= n || l->nt[t] >= n)
			return -1;
		(*tab)[l->nt[t]++] = v;
		p = e;
		while (*p == ' ')
			p++;
	}
	return 0;
}

/* one line of the file; 1 at END, -1 when it is not right */
static int read_line(ehv_loader *l, char *line)
{
	ehv_index *idx = l->idx;
	char *f[16];
	int n;

	if (line[0] == 'K' || line[0] == 'P' || line[0] == 'S')
	{
		if (line[1] != '\t')
			return -1;
		return read_table(l, line[0] == 'K' ? 0 : line[0] == 'P' ? 1 : 2, line + 2);
	}
	n = split_fields(line, f, 16);
	if (f[0][0] == 'N' && f[0][1] == '\0' && n == 6 && l->cur >= 0)
	{
		/* the most of them, first */
		unsigned long kind = num_of(f[1]), from = num_of(f[3]);

		if (kind > EHV_SYM_LABEL || f[4][0] == '\0' || from > 2 || (from == 2 && l->version < 2))
			return -1;
		return ehv_index_add_sym(idx, (unsigned long) l->cur, (unsigned char) kind,
				(unsigned short) num_of(f[2]), f[4], f[5][0] != '\0' ? f[5] : NULL, (int) from);
	}
	if (strcmp(f[0], "C") == 0 && n == 3 && l->version >= 2 && idx->nguides == 0)
		return ehv_index_reserve(idx, num_of(f[1]), num_of(f[2]));
	if (strcmp(f[0], "G") == 0 && n == 12)
	{
		ehv_guide g;

		memset(&g, 0, sizeof(g));
		g.size = strtoul(f[1], NULL, 10);
		g.mtime = strtoul(f[2], NULL, 10);
		g.root = (unsigned short) strtoul(f[3], NULL, 10);
		g.error = (int) strtol(f[4], NULL, 10);
		g.os = (unsigned char) strtoul(f[5], NULL, 10);
		g.width = (unsigned short) strtoul(f[6], NULL, 10);
		g.stg_flags = (unsigned short) strtoul(f[7], NULL, 10);
		g.ref_size = strtoul(f[8], NULL, 10);
		g.ref_mtime = strtoul(f[9], NULL, 10);
		g.path = f[10];
		g.ref_path = f[11][0] != '\0' ? f[11] : NULL;
		if (g.path[0] == '\0' || (l->cur = ehv_index_add_guide(idx, &g)) < 0)
			return -1;
	} else if (strcmp(f[0], "M") == 0 && n == 3 && l->cur >= 0)
	{
		ehv_guide *g = &idx->guides[l->cur];
		const char **dst;

		switch (f[1][0])
		{
		case 'D': dst = &g->database; break;
		case 'S': dst = &g->subject; break;
		case 'A': dst = &g->author; break;
		case 'V': dst = &g->version; break;
		case 'H': dst = &g->hostname; break;
		case 'F': dst = &g->default_node; break;
		case 'P': dst = &g->help_node; break;
		case 'C': dst = &g->charset; break;
		case 'L': dst = &g->language; break;
		default: dst = NULL; break;
		}
		if (dst != NULL && pdup(idx, dst, f[2]) < 0)
			return -1;
	} else if (strcmp(f[0], "N") == 0 && n == 6 && l->cur >= 0)
	{
		unsigned long kind = strtoul(f[1], NULL, 10);
		unsigned long from = strtoul(f[3], NULL, 10);

		if (kind > EHV_SYM_LABEL || f[4][0] == '\0' || from > 2 || (from == 2 && l->version < 2))
			return -1;
		if (ehv_index_add_sym(idx, (unsigned long) l->cur, (unsigned char) kind,
				(unsigned short) strtoul(f[2], NULL, 10),
				f[4], f[5][0] != '\0' ? f[5] : NULL, (int) from) < 0)
			return -1;
	} else if (strcmp(f[0], "R") == 0 && n == 4 && l->version >= 2 && idx->nguides == 0)
	{
		idx->allref_size = strtoul(f[1], NULL, 10);
		idx->allref_mtime = strtoul(f[2], NULL, 10);
		if (pdup(idx, &idx->allref_path, f[3]) < 0)
			return -1;
	} else if (strcmp(f[0], "O") == 0 && n == 2 && l->version >= 2)
	{
		const char **p = realloc(idx->orphans, (idx->norphans + 1) * sizeof(char *));

		if (p == NULL)
			return -1;
		idx->orphans = p;
		if (pdup(idx, &p[idx->norphans], f[1]) < 0)
			return -1;
		idx->norphans++;
	} else if (strcmp(f[0], "END") == 0 && n == 3)
	{
		if (strtoul(f[1], NULL, 10) != idx->nguides || strtoul(f[2], NULL, 10) != idx->nsyms)
			return -1;
		return 1;
	} else
	{
		return -1;
	}
	return 0;
}

static int load_steps(ehv_loader *l, unsigned int budget)
{
	ehv_index *idx = l->idx;

	while (budget-- > 0)
	{
		if (l->buf == NULL && !l->done)
		{
			/* the first step: the file opened, its size from it */
			pf_info st;

			if ((l->h = pf_open(l->path, PF_READ)) < 0 || pf_finfo(l->h, &st) != 0 || st.is_dir)
				return -1;
			l->size = st.size;
			if (l->size == 0 || (l->buf = malloc(l->size + 1)) == NULL)
				return -1;
		}
		if (l->got < l->size)
		{
			/* the file, a big piece at a time */
			unsigned long left = l->size - l->got;
			long n = left > read_piece ? (long) read_piece : (long) left;

			/* opened again in each step, where the last one left off */
			if (l->h < 0)
			{
				if ((l->h = pf_open(l->path, PF_READ)) < 0)
					return -1;
				if (l->got > 0 && pf_seek(l->h, (long) l->got) != (long) l->got)
					return -1;
			}
			if (pf_read(l->h, l->buf + l->got, n) != n)
				return -1;
			l->got += (unsigned long) n;
			if (l->got == l->size)
			{
				char *line, magic[32];

				pf_close(l->h);
				l->h = -1;
				l->pos = l->buf;
				l->end = l->buf + l->size;
				line = next_line(&l->pos, l->end);
				if (line == NULL || strncmp(line, CACHE_MAGIC " ", sizeof(CACHE_MAGIC)) != 0)
					return -1;
				l->version = (int) strtol(line + sizeof(CACHE_MAGIC), NULL, 10);
				sprintf(magic, "%s %d", CACHE_MAGIC, l->version);
				if ((l->version != 1 && l->version != 2) || strcmp(line, magic) != 0)
					return -1;
			}
			continue;
		}
		if (!l->done)
		{
			int k;

			for (k = 0; k < LINES; k++)
			{
				char *line = next_line(&l->pos, l->end);
				int r;

				if (line == NULL)
					return -1;                 /* no END */
				if ((r = read_line(l, line)) < 0)
					return -1;
				if (r == 1)
				{
					l->done = 1;
					break;
				}
			}
			if (!l->done)
				continue;
			free(l->buf);
			l->buf = NULL;
			{
				unsigned long g;

				for (g = 0; g < idx->nguides; g++)
					if (idx->guides[g].nsyms == 0)
						idx->guides[g].first_sym = 0;   /* as ehv_index_finish() leaves it */
			}
			if (l->version >= 2)
			{
				/* the tables as saved, all of them */
				if (l->nt[0] != idx->nguides || l->nt[1] != idx->nguides || l->nt[2] != idx->nsyms)
					return -1;
				return 1;
			}
			/* version 1: no tables nor ALL.REF names; sorted here, and
			   saved again by the next scan */
			idx->old_file = 1;
			if ((l->fin = ehv_index_finish_begin(idx)) == NULL)
				return -1;
			continue;
		}
		{
			int r = ehv_index_finish_some(l->fin, 1);

			if (r != 0)
				return r;
		}
	}
	return 0;
}

int ehv_index_load_some(ehv_loader *l, unsigned int budget)
{
	int r = load_steps(l, budget);

	if (l->h >= 0)
	{
		pf_close(l->h);
		l->h = -1;
	}
	return r;
}

ehv_index *ehv_index_load_take(ehv_loader *l)
{
	ehv_index *idx = l->idx;

	l->idx = NULL;
	ehv_index_load_abort(l);
	return idx;
}

ehv_index *ehv_index_load(const char *path)
{
	ehv_loader *l = ehv_index_load_begin(path);
	int r;

	if (l == NULL)
		return NULL;
	while ((r = ehv_index_load_some(l, 64)) == 0)
		;
	if (r < 0)
	{
		ehv_index_load_abort(l);
		return NULL;
	}
	return ehv_index_load_take(l);
}
