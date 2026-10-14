/*
 * EHypView - index of all hypertexts: storage, lookup, cache file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "fsys.h"
#include "index.h"

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

int ehv_index_merge_ref(ehv_index *idx, const ref_db *db)
{
	unsigned long m;

	for (m = 0; m < db->nmodules; m++)
	{
		const ref_module *mod = &db->modules[m];
		char file[256];
		unsigned long g, matched = 0;

		if (mod->file == NULL || mod->file[0] == '\0')
			continue;
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
	}
	return 0;
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

static int cmp_sym_order(const void *a, const void *b)
{
	const sort_item *x = a, *y = b;

	/* rank holds the guide; idx the original position */
	if (x->rank != y->rank)
		return x->rank < y->rank ? -1 : 1;
	return x->idx < y->idx ? -1 : x->idx > y->idx;
}

static unsigned long *sorted(sort_item *items, unsigned long n,
                             int (*cmp)(const void *, const void *))
{
	unsigned long *out = malloc((n ? n : 1) * sizeof(unsigned long));
	unsigned long i;

	if (out == NULL)
		return NULL;
	qsort(items, n, sizeof(sort_item), cmp);
	for (i = 0; i < n; i++)
		out[i] = items[i].idx;
	return out;
}

int ehv_index_finish(ehv_index *idx)
{
	unsigned long n = idx->nguides > idx->nsyms ? idx->nguides : idx->nsyms;
	sort_item *items = malloc((n ? n : 1) * sizeof(sort_item));
	unsigned long i, g;
	unsigned long *perm;

	free_tables(idx);
	if (items == NULL)
		return -1;

	/* group symbols by guide, keeping their order */
	for (i = 0; i < idx->nsyms; i++)
	{
		items[i].str = NULL;
		items[i].rank = idx->syms[i].guide;
		items[i].idx = i;
	}
	perm = sorted(items, idx->nsyms, cmp_sym_order);
	if (perm == NULL)
		goto fail;
	{
		ehv_sym *ns = malloc((idx->nsyms ? idx->nsyms : 1) * sizeof(ehv_sym));

		if (ns == NULL)
		{
			free(perm);
			goto fail;
		}
		for (i = 0; i < idx->nsyms; i++)
			ns[i] = idx->syms[perm[i]];
		free(perm);
		free(idx->syms);
		idx->syms = ns;
		idx->cap_syms = idx->nsyms;
	}
	for (g = 0; g < idx->nguides; g++)
		idx->guides[g].nsyms = 0;
	for (i = idx->nsyms; i-- > 0; )
	{
		ehv_guide *gd = &idx->guides[idx->syms[i].guide];

		gd->first_sym = i;
		gd->nsyms++;
	}
	for (g = 0; g < idx->nguides; g++)
		if (idx->guides[g].nsyms == 0)
			idx->guides[g].first_sym = 0;

	/* guides by key, then by scan root, then scan order */
	for (g = 0; g < idx->nguides; g++)
	{
		items[g].str = idx->guides[g].key;
		items[g].rank = idx->guides[g].root;
		items[g].idx = g;
	}
	if ((idx->by_key = sorted(items, idx->nguides, cmp_casefold)) == NULL)
		goto fail;

	for (g = 0; g < idx->nguides; g++)
	{
		items[g].str = idx->guides[g].path;
		items[g].idx = g;
	}
	if ((idx->by_path = sorted(items, idx->nguides, cmp_exact)) == NULL)
		goto fail;

	/* symbols by name, then kind, then root */
	for (i = 0; i < idx->nsyms; i++)
	{
		const ehv_sym *s = &idx->syms[i];

		items[i].str = s->name;
		items[i].rank = (unsigned long) kind_rank(s->kind) << 16 | idx->guides[s->guide].root;
		items[i].idx = i;
	}
	if ((idx->by_name = sorted(items, idx->nsyms, cmp_casefold)) == NULL)
		goto fail;

	free(items);
	return 0;

fail:
	free(items);
	free_tables(idx);
	return -1;
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

#define CACHE_MAGIC "EHYPVIEW-INDEX"

static void put_field(FILE *fp, const char *s)
{
	putc('\t', fp);
	if (s == NULL)
		return;
	for (; *s != '\0'; s++)
	{
		switch (*s)
		{
		case '\\': fputs("\\\\", fp); break;
		case '\t': fputs("\\t", fp); break;
		case '\n': fputs("\\n", fp); break;
		case '\r': fputs("\\r", fp); break;
		default:   putc(*s, fp); break;
		}
	}
}

static void put_meta(FILE *fp, char code, const char *value)
{
	char c[2];

	if (value == NULL)
		return;
	c[0] = code;
	c[1] = '\0';
	fputs("M", fp);
	put_field(fp, c);
	put_field(fp, value);
	putc('\n', fp);
}

int ehv_index_save(const ehv_index *idx, const char *path)
{
	char *tmp = malloc(strlen(path) + 5);
	FILE *fp;
	unsigned long g, i, nsyms = 0;
	int ok;

	if (tmp == NULL)
		return -1;
	strcpy(tmp, path);
	strcat(tmp, ".tmp");
	fp = fopen(tmp, "wb");
	if (fp == NULL)
	{
		free(tmp);
		return -1;
	}
	fprintf(fp, "%s %d\n", CACHE_MAGIC, EHV_CACHE_VERSION);
	for (g = 0; g < idx->nguides; g++)
	{
		const ehv_guide *gd = &idx->guides[g];

		fprintf(fp, "G\t%lu\t%lu\t%u\t%d\t%u\t%u\t%u\t%lu\t%lu",
			gd->size, gd->mtime, gd->root, gd->error, gd->os, gd->width, gd->stg_flags,
			gd->ref_size, gd->ref_mtime);
		put_field(fp, gd->path);
		put_field(fp, gd->ref_path ? gd->ref_path : "");
		putc('\n', fp);
		put_meta(fp, 'D', gd->database);
		put_meta(fp, 'S', gd->subject);
		put_meta(fp, 'A', gd->author);
		put_meta(fp, 'V', gd->version);
		put_meta(fp, 'H', gd->hostname);
		put_meta(fp, 'F', gd->default_node);
		put_meta(fp, 'P', gd->help_node);
		put_meta(fp, 'C', gd->charset);
		put_meta(fp, 'L', gd->language);
		for (i = gd->first_sym; i < gd->first_sym + gd->nsyms; i++)
		{
			const ehv_sym *s = &idx->syms[i];

			if (s->from_ref == 2)
				continue;
			fprintf(fp, "N\t%u\t%u\t%u", s->kind, s->line, s->from_ref);
			put_field(fp, s->name);
			put_field(fp, s->node == s->name ? "" : s->node);
			putc('\n', fp);
			nsyms++;
		}
	}
	fprintf(fp, "END\t%lu\t%lu\n", idx->nguides, nsyms);
	ok = !ferror(fp);
	if (fclose(fp) != 0)
		ok = 0;
	if (ok)
	{
		/* TOS cannot rename over an existing file */
		remove(path);
		ok = rename(tmp, path) == 0;
	}
	if (!ok)
		remove(tmp);
	free(tmp);
	return ok ? 0 : -1;
}

/* read one line into a growing buffer; NULL at end of file */
static char *read_line(FILE *fp, char **buf, size_t *cap)
{
	size_t n = 0;
	int c;

	while ((c = getc(fp)) != EOF && c != '\n')
	{
		if (n + 1 >= *cap)
		{
			size_t ncap = *cap ? *cap * 2 : 256;
			char *p = realloc(*buf, ncap);

			if (p == NULL)
				return NULL;
			*buf = p;
			*cap = ncap;
		}
		(*buf)[n++] = (char) c;
	}
	if (c == EOF && n == 0)
		return NULL;
	if (*buf == NULL && (*buf = malloc(1)) != NULL)
		*cap = 1;
	if (*buf == NULL)
		return NULL;
	(*buf)[n] = '\0';
	return *buf;
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

ehv_index *ehv_index_load(const char *path)
{
	FILE *fp = fopen(path, "rb");
	ehv_index *idx;
	char *buf = NULL, *line;
	size_t cap = 0;
	char *f[16];
	long cur = -1;
	int done = 0, version = 0;
	char magic[32];

	if (fp == NULL)
		return NULL;
	idx = ehv_index_new();
	if (idx == NULL)
	{
		fclose(fp);
		return NULL;
	}
	line = read_line(fp, &buf, &cap);
	if (line == NULL || sscanf(line, "%31s %d", magic, &version) != 2 ||
		strcmp(magic, CACHE_MAGIC) != 0 || version != EHV_CACHE_VERSION)
		goto fail;

	while (!done && (line = read_line(fp, &buf, &cap)) != NULL)
	{
		int n = split_fields(line, f, 16);

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
			if (g.path[0] == '\0' || (cur = ehv_index_add_guide(idx, &g)) < 0)
				goto fail;
		} else if (strcmp(f[0], "M") == 0 && n == 3 && cur >= 0)
		{
			ehv_guide *g = &idx->guides[cur];
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
				goto fail;
		} else if (strcmp(f[0], "N") == 0 && n == 6 && cur >= 0)
		{
			unsigned long kind = strtoul(f[1], NULL, 10);

			if (kind > EHV_SYM_LABEL || f[4][0] == '\0')
				goto fail;
			if (ehv_index_add_sym(idx, (unsigned long) cur, (unsigned char) kind,
					(unsigned short) strtoul(f[2], NULL, 10),
					f[4], f[5][0] != '\0' ? f[5] : NULL,
					(int) strtoul(f[3], NULL, 10)) < 0)
				goto fail;
		} else if (strcmp(f[0], "END") == 0 && n == 3)
		{
			if (strtoul(f[1], NULL, 10) != idx->nguides || strtoul(f[2], NULL, 10) != idx->nsyms)
				goto fail;
			done = 1;
		} else
		{
			goto fail;
		}
	}
	if (!done || ehv_index_finish(idx) < 0)
		goto fail;
	free(buf);
	fclose(fp);
	return idx;

fail:
	free(buf);
	fclose(fp);
	ehv_index_free(idx);
	return NULL;
}
