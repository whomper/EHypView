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

/* the cache text is built in memory and written in one go */
typedef struct {
	char *data;
	unsigned long len, cap;
	int failed;
} outbuf;

static void out_bytes(outbuf *o, const char *s, unsigned long n)
{
	if (o->failed)
		return;
	if (o->len + n + 1 > o->cap)
	{
		unsigned long ncap = o->cap ? o->cap : 16384;
		char *p;

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

static void put_field(outbuf *o, const char *s)
{
	out_bytes(o, "\t", 1);
	if (s == NULL)
		return;
	for (; *s != '\0'; s++)
	{
		switch (*s)
		{
		case '\\': out_bytes(o, "\\\\", 2); break;
		case '\t': out_bytes(o, "\\t", 2); break;
		case '\n': out_bytes(o, "\\n", 2); break;
		case '\r': out_bytes(o, "\\r", 2); break;
		default:   out_bytes(o, s, 1); break;
		}
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

int ehv_index_save(const ehv_index *idx, const char *path)
{
	outbuf o;
	char num[128];
	unsigned long g, i, nsyms = 0;
	int rc;

	memset(&o, 0, sizeof(o));
	sprintf(num, "%s %d\n", CACHE_MAGIC, EHV_CACHE_VERSION);
	out_str(&o, num);
	for (g = 0; g < idx->nguides; g++)
	{
		const ehv_guide *gd = &idx->guides[g];

		sprintf(num, "G\t%lu\t%lu\t%u\t%d\t%u\t%u\t%u\t%lu\t%lu",
			gd->size, gd->mtime, gd->root, gd->error, gd->os, gd->width, gd->stg_flags,
			gd->ref_size, gd->ref_mtime);
		out_str(&o, num);
		put_field(&o, gd->path);
		put_field(&o, gd->ref_path ? gd->ref_path : "");
		out_str(&o, "\n");
		put_meta(&o, 'D', gd->database);
		put_meta(&o, 'S', gd->subject);
		put_meta(&o, 'A', gd->author);
		put_meta(&o, 'V', gd->version);
		put_meta(&o, 'H', gd->hostname);
		put_meta(&o, 'F', gd->default_node);
		put_meta(&o, 'P', gd->help_node);
		put_meta(&o, 'C', gd->charset);
		put_meta(&o, 'L', gd->language);
		for (i = gd->first_sym; i < gd->first_sym + gd->nsyms; i++)
		{
			const ehv_sym *sy = &idx->syms[i];

			if (sy->from_ref == 2)
				continue;
			sprintf(num, "N\t%u\t%u\t%u", sy->kind, sy->line, sy->from_ref);
			out_str(&o, num);
			put_field(&o, sy->name);
			put_field(&o, sy->node == sy->name ? "" : sy->node);
			out_str(&o, "\n");
			nsyms++;
		}
	}
	sprintf(num, "END\t%lu\t%lu\n", idx->nguides, nsyms);
	out_str(&o, num);
	rc = o.failed ? -1 : pf_save(path, o.data, o.len);
	free(o.data);
	return rc;
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

ehv_index *ehv_index_load(const char *path)
{
	unsigned long size;
	char *buf = pf_load(path, &size), *pos, *end, *line;
	ehv_index *idx;
	char *f[16];
	long cur = -1;
	int done = 0;
	char magic[32];

	if (buf == NULL)
		return NULL;
	idx = ehv_index_new();
	if (idx == NULL)
	{
		free(buf);
		return NULL;
	}
	pos = buf;
	end = buf + size;
	line = next_line(&pos, end);
	sprintf(magic, "%s %d", CACHE_MAGIC, EHV_CACHE_VERSION);
	if (line == NULL || strcmp(line, magic) != 0)
		goto fail;

	while (!done && (line = next_line(&pos, end)) != NULL)
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
	return idx;

fail:
	free(buf);
	ehv_index_free(idx);
	return NULL;
}
