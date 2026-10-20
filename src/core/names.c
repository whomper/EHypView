/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the index's names (index.h): sorted by name, ignoring case,
 * in blocks of EHV_BLOCK bytes at the start of EHYPVIEW.IDX, or in
 * memory.  A lookup reads the block or two a name is in; a search reads
 * them all, in pieces.
 *
 * They are built with little memory: gathered a batch at a time (24 KB),
 * each batch sorted and written to NAME.RUN, then the batches, and the
 * names an index read before has for the hypertexts that didn't change,
 * merged into the blocks, NAME.ID$, which the saver makes the cache.
 *
 * A name, in a block or a batch:
 *
 *   1  guide (2 bytes)  kind  from  line (2 bytes)  name NUL  node NUL
 *
 * (bytes, numbers high byte first; an empty node: the name itself).  A
 * block starts with the index's mark (4 bytes) and ends with a 0, or
 * where the next name would not fit.
 */
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "index.h"
#include "names.h"
#include "plat.h"

#define LEN_MAX  255                  /* of a name or a node, kept */
#define REC_MAX  (7 + LEN_MAX + 1 + LEN_MAX + 1)
#define MARK     4                    /* a block's first bytes */
#define ROOM     (16 * REC_MAX)       /* a scan step's names, past the batch */
#define MOVES    256                  /* sort moves a step */
#define RECORDS  16                   /* names merged a step */
#define MERGE    16384L               /* the merge's pieces, all of them */

unsigned long ehv_names_misread;

static unsigned long batch_bytes = 24576L;

void ehv_index_batch(unsigned long bytes)
{
	batch_bytes = bytes > 65535L - ROOM ? 65535L - ROOM : bytes;
}

static unsigned long get32(const unsigned char *p)
{
	return (unsigned long) p[0] << 24 | (unsigned long) p[1] << 16 | (unsigned long) p[2] << 8 | p[3];
}

static void put32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char) (v >> 24);
	p[1] = (unsigned char) (v >> 16);
	p[2] = (unsigned char) (v >> 8);
	p[3] = (unsigned char) v;
}

static unsigned long rec_guide(const unsigned char *r)
{
	return (unsigned long) r[1] << 8 | r[2];
}

static unsigned long rec_size(const unsigned char *r)
{
	const unsigned char *p = r + 7;

	p += strlen((const char *) p) + 1;
	p += strlen((const char *) p) + 1;
	return (unsigned long) (p - r);
}

static unsigned long rec_put(unsigned char *r, unsigned long guide, unsigned char kind, unsigned char from,
                             unsigned short line, const char *name, const char *node)
{
	size_t nl = strlen(name), dl = 0;
	unsigned char *p = r + 7;

	if (node != NULL && node != name && strcmp(node, name) != 0)
		dl = strlen(node);
	else
		node = "";
	if (nl > LEN_MAX)
		nl = LEN_MAX;
	if (dl > LEN_MAX)
		dl = LEN_MAX;
	r[0] = 1;
	r[1] = (unsigned char) (guide >> 8);
	r[2] = (unsigned char) guide;
	r[3] = kind;
	r[4] = from;
	r[5] = (unsigned char) (line >> 8);
	r[6] = (unsigned char) line;
	memcpy(p, name, nl);
	p[nl] = '\0';
	p += nl + 1;
	memcpy(p, node, dl);
	p[dl] = '\0';
	return (unsigned long) (p + dl + 1 - r);
}

/* a name read from a file, checked: its size, 0 when it doesn't fit in
   room bytes or isn't right */
static unsigned long rec_check(const unsigned char *r, unsigned long room, unsigned long nguides)
{
	const unsigned char *a, *b, *end = r + room;

	if (room < 9 || r[0] != 1 || rec_guide(r) >= nguides || r[3] > EHV_SYM_LABEL || r[4] > 2 || r[7] == '\0')
		return 0;
	if ((a = memchr(r + 7, '\0', (size_t) (end - r - 7))) == NULL ||
	    (b = memchr(a + 1, '\0', (size_t) (end - a - 1))) == NULL)
		return 0;
	return (unsigned long) (b + 1 - r);
}

static void rec_get(const unsigned char *r, ehv_sym *s)
{
	s->guide = rec_guide(r);
	s->kind = r[3];
	s->from_ref = r[4];
	s->line = (unsigned short) (r[5] << 8 | r[6]);
	s->name = (const char *) r + 7;
	s->node = s->name + strlen(s->name) + 1;
	if (*s->node == '\0')
		s->node = s->name;
}

static int kind_rank(unsigned char kind)
{
	return kind == EHV_SYM_ALIAS ? 1 : kind == EHV_SYM_LABEL ? 2 : 0;
}

/* the names' order: by name ignoring case, pages before aliases before
   labels, earlier roots first, then by hypertext */
static int rec_cmp(const ehv_index *idx, const unsigned char *a, const unsigned char *b)
{
	int c = atari_casecmp((const char *) a + 7, (const char *) b + 7);
	unsigned long ga, gb, ra, rb;

	if (c != 0)
		return c;
	ga = rec_guide(a);
	gb = rec_guide(b);
	ra = (unsigned long) kind_rank(a[3]) << 16 | idx->guides[ga].root;
	rb = (unsigned long) kind_rank(b[3]) << 16 | idx->guides[gb].root;
	if (ra != rb)
		return ra < rb ? -1 : 1;
	return ga < gb ? -1 : ga > gb;
}

/* ---------------------------------------------------------------- */
/* names found                                                       */

void ehv_hits_init(ehv_hits *h)
{
	memset(h, 0, sizeof(*h));
}

void ehv_hits_free(ehv_hits *h)
{
	unsigned long i;

	for (i = 0; i < h->n; i++)
		free((char *) h->s[i].name);
	free(h->s);
	ehv_hits_init(h);
}

static int hits_room(ehv_hits *h)
{
	if (h->n == h->cap)
	{
		unsigned long ncap = h->cap ? h->cap * 2 : 8;
		ehv_sym *p = realloc(h->s, ncap * sizeof(ehv_sym));

		if (p == NULL)
			return -1;
		h->s = p;
		h->cap = ncap;
	}
	return 0;
}

/* a copy of s, its strings in one piece */
static int sym_copy(ehv_sym *d, const ehv_sym *s)
{
	size_t nl = strlen(s->name) + 1, dl = s->node != NULL && s->node != s->name ? strlen(s->node) + 1 : 0;
	char *m = malloc(nl + dl);

	if (m == NULL)
		return -1;
	*d = *s;
	memcpy(m, s->name, nl);
	if (dl > 0)
		memcpy(m + nl, s->node, dl);
	d->name = m;
	d->node = dl > 0 ? m + nl : m;
	return 0;
}

int ehv_hits_add(ehv_hits *h, const ehv_sym *s)
{
	if (hits_room(h) < 0 || sym_copy(&h->s[h->n], s) < 0)
		return -1;
	h->n++;
	return 0;
}

/* the names of `from` added to h, which owns them then */
static void hits_take(ehv_hits *h, ehv_hits *from)
{
	unsigned long i;

	for (i = 0; i < from->n; i++)
	{
		if (hits_room(h) < 0)
		{
			free((char *) from->s[i].name);
			continue;
		}
		h->s[h->n++] = from->s[i];
	}
	from->n = 0;
	ehv_hits_free(from);
}

/* ---------------------------------------------------------------- */
/* reading                                                           */

/* blocks b... (n of them) of idx into buf: from memory, or from its file
   (h: open already, else opened here); each must carry the index's mark */
static int read_blocks(const ehv_index *idx, int h, unsigned long b, unsigned long n, unsigned char *buf)
{
	unsigned long i;

	if (b + n > idx->nblocks)
		return -1;
	if (idx->mem != NULL)
		memcpy(buf, idx->mem + b * EHV_BLOCK, n * EHV_BLOCK);
	else
	{
		int own = h < 0, r;

		if (idx->file == NULL || (own && (h = pf_open(idx->file, PF_READ)) < 0))
			return -1;
		r = pf_read_at(h, b * EHV_BLOCK, buf, n * EHV_BLOCK);
		if (own)
			pf_close(h);
		if (r != 0)
			return -1;
	}
	for (i = 0; i < n; i++)
		if (get32(buf + i * EHV_BLOCK) != idx->gen)
		{
			ehv_names_misread++;          /* written again since it was read */
			return -1;
		}
	return 0;
}

int ehv_names_open(ehv_names *nm, const ehv_index *idx, unsigned long piece, int keep_open)
{
	memset(nm, 0, sizeof(*nm));
	nm->idx = idx;
	nm->h = -1;
	nm->last = idx->nblocks;
	nm->keep_open = keep_open;
	if (idx->nblocks == 0 || idx->mem != NULL)
		return 0;                         /* nothing to read, or read in place */
	if (piece == 0)
		piece = 1;
	if (piece > idx->nblocks)
		piece = idx->nblocks;
	while ((nm->buf = malloc(piece * EHV_BLOCK)) == NULL && piece > 1)
		piece /= 2;
	if (nm->buf == NULL)
		return -1;
	nm->piece = piece;
	if (keep_open && (idx->file == NULL || (nm->h = pf_open(idx->file, PF_READ)) < 0))
	{
		free(nm->buf);
		nm->buf = NULL;
		return -1;
	}
	return 0;
}

int ehv_names_seek(ehv_names *nm, unsigned long block)
{
	nm->next = block < nm->last ? block : nm->last;
	nm->nbuf = nm->bi = 0;
	return 0;
}

int ehv_names_next(ehv_names *nm, ehv_sym *s)
{
	const ehv_index *idx = nm->idx;
	unsigned long sz;

	for (;;)
	{
		const unsigned char *blk;

		if (nm->bi >= nm->nbuf)
		{
			unsigned long n;

			if (nm->failed)
				return -1;
			if (nm->next >= nm->last)
				return 0;
			if (idx->mem != NULL)
			{
				n = nm->last - nm->next;
				nm->data = idx->mem + nm->next * EHV_BLOCK;
			} else
			{
				n = nm->last - nm->next < nm->piece ? nm->last - nm->next : nm->piece;
				if (read_blocks(idx, nm->h, nm->next, n, nm->buf) < 0)
				{
					nm->failed = 1;
					return -1;
				}
				nm->data = nm->buf;
			}
			nm->nbuf = n;
			nm->bi = 0;
			nm->pos = MARK;
			nm->next += n;
		}
		blk = nm->data + nm->bi * EHV_BLOCK;
		if (nm->pos >= EHV_BLOCK || blk[nm->pos] == 0)
		{
			nm->bi++;
			nm->pos = MARK;
			continue;
		}
		if ((sz = rec_check(blk + nm->pos, EHV_BLOCK - nm->pos, idx->nguides)) == 0)
		{
			nm->failed = 1;               /* damaged */
			nm->bi = nm->nbuf;
			return -1;
		}
		rec_get(blk + nm->pos, s);
		nm->pos += sz;
		return 1;
	}
}

void ehv_names_close(ehv_names *nm)
{
	if (nm->h >= 0)
		pf_close(nm->h);
	free(nm->buf);
	memset(nm, 0, sizeof(*nm));
	nm->h = -1;
}

/* the block a name (ignoring case) may begin in: the last one whose
   first name comes before it */
static unsigned long start_block(const ehv_index *idx, const char *name)
{
	unsigned long lo = 0, hi = idx->nblocks;

	while (lo < hi)
	{
		unsigned long mid = lo + (hi - lo) / 2;

		if (atari_casecmp(idx->first[mid], name) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo > 0 ? lo - 1 : 0;
}

unsigned long ehv_index_find_symbol(const ehv_index *idx, const char *name, long guide,
                                    ehv_hits *hits, unsigned long max)
{
	ehv_names nm;
	ehv_hits other;
	ehv_sym s;
	unsigned long exact = 0, fold = 0;

	if (name == NULL || idx->nblocks == 0 || ehv_names_open(&nm, idx, 2, 1) < 0)
		return 0;
	ehv_names_seek(&nm, start_block(idx, name));
	ehv_hits_init(&other);
	while (ehv_names_next(&nm, &s) == 1)
	{
		int c = atari_casecmp(s.name, name);

		if (c < 0)
			continue;
		if (c > 0)
			break;
		if (guide >= 0 && s.guide != (unsigned long) guide)
			continue;
		if (strcmp(s.name, name) == 0)
		{
			if (hits != NULL && exact < max)
				ehv_hits_add(hits, &s);
			exact++;
		} else
		{
			if (hits != NULL && exact == 0 && fold < max)
				ehv_hits_add(&other, &s);
			fold++;
		}
	}
	ehv_names_close(&nm);
	if (exact == 0 && hits != NULL)
		hits_take(hits, &other);
	ehv_hits_free(&other);
	return exact > 0 ? exact : fold;
}

typedef struct {
	unsigned int dist;
	unsigned long rank;
	ehv_sym s;
} cand;

unsigned long ehv_index_suggest(const ehv_index *idx, const char *name, long guide,
                                ehv_hits *hits, unsigned long max)
{
	unsigned int limit = ehv_similar_limit(name);
	unsigned long n = 0, k;
	ehv_names nm;
	ehv_sym s;
	cand *best;

	if (max == 0 || idx->nblocks == 0)
		return 0;
	if ((best = malloc(max * sizeof(cand))) == NULL)
		return 0;
	if (ehv_names_open(&nm, idx, ehv_names_piece(), 1) < 0)
	{
		free(best);
		return 0;
	}
	while (ehv_names_next(&nm, &s) == 1)
	{
		unsigned int d;
		unsigned long rank, pos;
		int dup = 0;

		if (guide >= 0 && s.guide != (unsigned long) guide)
			continue;
		if ((d = ehv_similarity(name, s.name, limit)) > limit)
			continue;
		/* the same name in many guides ("Main", "Index"): listed once */
		for (k = 0; k < n && !dup; k++)
			dup = atari_casecmp(best[k].s.name, s.name) == 0;
		if (dup)
			continue;
		rank = (unsigned long) kind_rank(s.kind) << 16 | idx->guides[s.guide].root;
		for (pos = n; pos > 0 && (best[pos - 1].dist > d || (best[pos - 1].dist == d && best[pos - 1].rank > rank)); pos--)
			;
		if (pos >= max)
			continue;
		if (n == max)
			free((char *) best[--n].s.name);
		memmove(&best[pos + 1], &best[pos], (n - pos) * sizeof(cand));
		if (sym_copy(&best[pos].s, &s) < 0)
		{
			memmove(&best[pos], &best[pos + 1], (n - pos) * sizeof(cand));
			continue;
		}
		best[pos].dist = d;
		best[pos].rank = rank;
		n++;
	}
	ehv_names_close(&nm);
	for (k = 0; k < n; k++)
	{
		if (hits != NULL && hits_room(hits) == 0)
			hits->s[hits->n++] = best[k].s;
		else
			free((char *) best[k].s.name);
	}
	free(best);
	return n;
}

/* ---------------------------------------------------------------- */
/* building                                                          */

/* what is merged: the blocks of the index read before (its names for
   the hypertexts taken), a batch in NAME.RUN or memory, or the last
   batch, still where it was gathered */
enum { IN_OLD, IN_RUN, IN_BATCH };

typedef struct {
	int kind;
	unsigned long off, end;           /* a run: its bytes left; old: blocks */
	unsigned char *buf;
	unsigned long len, pos, cap;      /* the batch: pos, its names (nrec) */
	int done;                         /* all of it read */
} input;

struct ehv_build {
	char *runs;                       /* NAME.RUN; NULL: the batches in memory */
	char *target;                     /* NAME.ID$; NULL: the blocks in memory */

	/* the batch being gathered, and sorted (offs: where each name is) */
	unsigned char *arena;
	unsigned long alen, acap, limit;
	unsigned short *offs, *tmp;
	unsigned long nrec, caprec;
	int sorted, merging;
	unsigned long width, lo, mid, hi, i, j, k;
	unsigned long wrote;              /* names of it written */

	/* the batches sorted */
	unsigned char *runmem;
	unsigned long runlen, runcap, runfile;
	struct run { unsigned long off, len; } *run;
	unsigned long nruns, caprun;

	/* the index read before, for the hypertexts that didn't change */
	const ehv_index *from;
	long *map;                        /* its guides here, -1: not taken */

	/* merging into blocks */
	int started, made, done;
	input *in;
	unsigned long nin;
	unsigned char *out, *scratch;
	unsigned long nout;               /* blocks in out, written when full */
	unsigned long cur, bpos;          /* the block being filled, in out */
	unsigned long nblocks, cap_first, gen;
	unsigned char *mem;               /* the blocks, when not to a file */
	unsigned long nmem;
};

static ehv_build *build_of(ehv_index *idx)
{
	ehv_build *b = idx->build;

	if (b != NULL)
		return b;
	if ((b = calloc(1, sizeof(*b))) == NULL)
		return NULL;
	b->limit = batch_bytes;
	b->width = 1;
	idx->build = b;
	return b;
}

/* NAME.XYZ beside the cache: NAME.RUN */
static char *beside(const char *cache, const char *ext)
{
	size_t n = strlen(cache), d = n;
	char *p;

	while (d > 0 && cache[d - 1] != '.' && cache[d - 1] != '\\' && cache[d - 1] != '/' && cache[d - 1] != ':')
		d--;
	if (d == 0 || cache[d - 1] != '.')
		d = n + 1;                        /* no extension: add one */
	if ((p = malloc(d + strlen(ext) + 1)) == NULL)
		return NULL;
	memcpy(p, cache, d - 1);
	p[d - 1] = '.';
	strcpy(p + d, ext);
	return p;
}

int ehv_index_build_to(ehv_index *idx, const char *cache)
{
	ehv_build *b = build_of(idx);

	if (b == NULL)
		return -1;
	free(b->runs);
	free(b->target);
	b->runs = beside(cache, "RUN");
	b->target = pf_tmp_name(cache);
	if (b->runs == NULL || b->target == NULL)
	{
		free(b->runs);
		free(b->target);
		b->runs = b->target = NULL;
		return -1;
	}
	return 0;
}

const char *ehv_names_target(const ehv_index *idx)
{
	return idx->build != NULL && idx->build->done && idx->build->made ? idx->build->target : NULL;
}

void ehv_names_build_free(ehv_index *idx)
{
	ehv_build *b = idx->build;
	unsigned long i;

	if (b == NULL)
		return;
	if (b->runs != NULL && b->runfile > 0)
		pf_remove(b->runs);
	if (b->made && !b->done)
		pf_remove(b->target);         /* not finished */
	for (i = 0; i < b->nin; i++)
		free(b->in[i].buf);
	free(b->in);
	free(b->out);
	free(b->scratch);
	free(b->mem);
	free(b->runs);
	free(b->target);
	free(b->arena);
	free(b->offs);
	free(b->tmp);
	free(b->runmem);
	free(b->run);
	free(b->map);
	free(b);
	idx->build = NULL;
}

int ehv_index_take_names(ehv_index *idx, const ehv_index *from, unsigned long g, unsigned long ng)
{
	ehv_build *b = build_of(idx);
	unsigned long i;

	if (b == NULL || g >= from->nguides || (b->from != NULL && b->from != from))
		return -1;
	if (b->from == NULL)
	{
		if ((b->map = malloc((from->nguides ? from->nguides : 1) * sizeof(long))) == NULL)
			return -1;
		for (i = 0; i < from->nguides; i++)
			b->map[i] = -1;
		b->from = from;
	}
	b->map[g] = (long) ng;
	return 0;
}

int ehv_index_build_busy(const ehv_index *idx)
{
	return idx->build != NULL && idx->build->nrec > 0 && idx->build->alen >= idx->build->limit;
}

/* 1 once the batch is sorted (in offs), 0 when the budget ran out */
static int batch_sort(const ehv_index *idx, ehv_build *b, unsigned long budget)
{
	while (b->width < b->nrec)
	{
		if (!b->merging)
		{
			if (b->lo >= b->nrec)
			{
				unsigned short *t = b->offs;

				b->offs = b->tmp;          /* a pass done: the next one from it */
				b->tmp = t;
				b->width *= 2;
				b->lo = 0;
				continue;
			}
			b->mid = b->lo + b->width < b->nrec ? b->lo + b->width : b->nrec;
			b->hi = b->mid + b->width < b->nrec ? b->mid + b->width : b->nrec;
			b->i = b->k = b->lo;
			b->j = b->mid;
			b->merging = 1;
		}
		while (b->k < b->hi)
		{
			if (budget == 0)
				return 0;
			budget--;
			if (b->i < b->mid && (b->j >= b->hi ||
				rec_cmp(idx, b->arena + b->offs[b->i], b->arena + b->offs[b->j]) <= 0))
				b->tmp[b->k++] = b->offs[b->i++];
			else
				b->tmp[b->k++] = b->offs[b->j++];
		}
		b->merging = 0;
		b->lo = b->hi;
	}
	return 1;
}

/* some of the sorted batch to NAME.RUN (or memory), opened for it: 1
   when all of it is */
static int batch_write(ehv_build *b, unsigned long most)
{
	unsigned long start = b->runs != NULL ? b->runfile : b->runlen, n = 0;

	if (b->wrote == 0)
	{
		/* the batch's place in the runs */
		if (b->nruns == b->caprun)
		{
			unsigned long ncap = b->caprun ? b->caprun * 2 : 8;
			struct run *p = realloc(b->run, ncap * sizeof(*p));

			if (p == NULL)
				return -1;
			b->run = p;
			b->caprun = ncap;
		}
		b->run[b->nruns].off = start;
		b->run[b->nruns].len = 0;
		b->nruns++;
	}
	if (b->runs != NULL)
	{
		unsigned char *stage = malloc(4096);
		unsigned long used = 0;
		int h = -1, bad = 0;

		if (stage == NULL)
			return -1;
		if ((h = pf_open(b->runs, b->runfile > 0 ? PF_APPEND : PF_WRITE)) < 0)
		{
			free(stage);
			if (b->runfile > 0)
				return -1;
			free(b->runs);               /* can't be written: in memory, then */
			b->runs = NULL;
			free(b->target);
			b->target = NULL;
			if (b->wrote == 0)
				b->nruns--;
			return batch_write(b, most);
		}
		while (b->wrote < b->nrec && n < most)
		{
			const unsigned char *r = b->arena + b->offs[b->wrote];
			unsigned long sz = rec_size(r);

			if (used + sz > 4096)
			{
				if (pf_write(h, stage, (long) used) != (long) used)
				{
					bad = 1;
					break;
				}
				used = 0;
			}
			memcpy(stage + used, r, sz);
			used += sz;
			n += sz;
			b->wrote++;
		}
		if (!bad && used > 0 && pf_write(h, stage, (long) used) != (long) used)
			bad = 1;
		pf_close(h);
		free(stage);
		if (bad)
			return -1;
		b->runfile += n;
	} else
	{
		if (b->runlen + b->alen > b->runcap)
		{
			unsigned long ncap = b->runcap ? b->runcap : 16384;
			unsigned char *p;

			while (ncap < b->runlen + b->alen)
				ncap *= 2;
			if ((p = realloc(b->runmem, ncap)) == NULL)
				return -1;
			b->runmem = p;
			b->runcap = ncap;
		}
		while (b->wrote < b->nrec)
		{
			const unsigned char *r = b->arena + b->offs[b->wrote++];
			unsigned long sz = rec_size(r);

			memcpy(b->runmem + b->runlen, r, sz);
			b->runlen += sz;
			n += sz;
		}
	}
	b->run[b->nruns - 1].len += n;
	if (b->wrote < b->nrec)
		return 0;
	b->alen = b->nrec = b->wrote = 0;
	b->sorted = b->merging = 0;
	b->width = 1;
	b->lo = 0;
	return 1;
}

/* a step of putting the batch away: 1 = more, 0 = done, -1 = failed */
static int batch_step(const ehv_index *idx, ehv_build *b, unsigned long moves)
{
	int r;

	if (b->nrec == 0)
		return 0;
	if (!b->sorted)
	{
		if (!batch_sort(idx, b, moves))
			return 1;
		b->sorted = 1;
		return 1;
	}
	r = batch_write(b, ehv_names_piece() * EHV_BLOCK);   /* 32 KB a step, 16 KB on a 68000 */
	return r < 0 ? -1 : r == 0;
}

int ehv_index_build_step(ehv_index *idx)
{
	if (!ehv_index_build_busy(idx))
		return 0;
	return batch_step(idx, idx->build, MOVES);
}

int ehv_index_add_sym(ehv_index *idx, unsigned long guide, unsigned char kind,
                      unsigned short line, const char *name, const char *node, int from_ref)
{
	ehv_build *b = build_of(idx);
	unsigned long sz;

	if (b == NULL || guide >= idx->nguides || guide > 0xFFFFUL || name == NULL || name[0] == '\0')
		return -1;
	if (b->arena == NULL)
	{
		b->acap = b->limit + ROOM;
		if ((b->arena = malloc(b->acap)) == NULL)
			return -1;
	}
	if (b->alen + REC_MAX > b->acap || b->width > 1 || b->lo > 0 || b->merging || b->sorted || b->wrote > 0)
	{
		/* full, and the caller didn't put it away: all of it now */
		int r;

		while ((r = batch_step(idx, b, (unsigned long) -1)) == 1)
			;
		if (r < 0)
			return -1;
	}
	if (b->nrec == b->caprec)
	{
		unsigned long ncap = b->caprec ? b->caprec * 2 : 256;
		unsigned short *o = realloc(b->offs, ncap * sizeof(unsigned short));

		if (o == NULL)
			return -1;
		b->offs = o;
		if ((o = realloc(b->tmp, ncap * sizeof(unsigned short))) == NULL)
			return -1;
		b->tmp = o;
		b->caprec = ncap;
	}
	sz = rec_put(b->arena + b->alen, guide, kind, (unsigned char) from_ref, line, name, node);
	b->offs[b->nrec++] = (unsigned short) b->alen;
	b->alen += sz;
	return 0;
}

/* ---------------------------------------------------------------- */
/* merging                                                           */

/* the index's mark: from what it is made of, so the same hypertexts
   give the same mark, and others another */
static unsigned long mark_of(const ehv_index *idx, const ehv_build *b)
{
	unsigned long h = 5381UL + idx->nguides, g;
	const char *p;

	for (g = 0; g < idx->nguides; g++)
	{
		const ehv_guide *gd = &idx->guides[g];

		h = (h * 33) ^ gd->size;
		h = (h * 33) ^ gd->mtime;
		h = (h * 33) ^ gd->ref_size;
		h = (h * 33) ^ gd->ref_mtime;
		for (p = gd->path; *p != '\0'; p++)
			h = (h * 33) ^ (unsigned char) *p;
	}
	h = (h * 33) ^ idx->allref_size;
	h = (h * 33) ^ idx->allref_mtime;
	h = (h * 33) ^ (b->runs != NULL ? b->runfile : b->runlen);
	h = (h * 33) ^ b->alen;
	h = (h * 33) ^ b->nrec;
	if (b->from != NULL)
		h = (h * 33) ^ b->from->gen;
	h &= 0xFFFFFFFFUL;                    /* as the blocks keep it */
	return h != 0 ? h : 1;
}

static int merge_begin(ehv_index *idx, ehv_build *b)
{
	unsigned long i, n = b->nruns, each, k = 0;
	int old = 0;

	if (b->from != NULL && b->from->nblocks > 0)
		for (i = 0; i < b->from->nguides && !old; i++)
			old = b->map[i] >= 0;
	n += (unsigned long) old + (b->nrec > 0);
	b->nout = ehv_names_out();
	b->in = calloc(n ? n : 1, sizeof(input));
	b->out = malloc(b->nout * EHV_BLOCK);
	if (b->in == NULL || b->out == NULL)
		return -1;
	each = n > (b->nrec > 0) ? MERGE / (n - (b->nrec > 0)) : MERGE;
	if (each < 2 * EHV_BLOCK)
		each = 2 * EHV_BLOCK;
	if (each > 8192)
		each = 8192;
	for (i = 0; i < n; i++)
	{
		input *in = &b->in[i];
		unsigned long cap = each;

		b->nin++;
		if (b->nrec > 0 && i == n - 1)
		{
			in->kind = IN_BATCH;        /* never written: read in place */
			in->done = 1;
			continue;
		}
		if (old && i == 0)
		{
			/* the old index's names in one read when they are few
			   (8 KB at the most) */
			unsigned long all = (b->from->nblocks + 1) * EHV_BLOCK;

			if (cap < all)
				cap = all < 8192 ? all : 8192 > cap ? 8192 : cap;
			if ((b->scratch = malloc(cap)) == NULL)
				return -1;
		}
		if ((in->buf = malloc(cap)) == NULL)
			return -1;
		in->cap = cap;
		if (old && i == 0)
		{
			in->kind = IN_OLD;
			in->end = b->from->nblocks;
		} else
		{
			in->kind = IN_RUN;
			in->off = b->run[k].off;
			in->end = in->off + b->run[k].len;
			k++;
		}
		in->done = in->off >= in->end;
	}
	b->gen = mark_of(idx, b);
	b->started = 1;
	return 0;
}

static int needs_more(const input *in)
{
	return !in->done && in->len - in->pos < REC_MAX;
}

/* an input's next name, or NULL */
static const unsigned char *head_of(const ehv_build *b, const input *in)
{
	if (in->kind == IN_BATCH)
		return in->pos < b->nrec ? b->arena + b->offs[in->pos] : NULL;
	return in->pos < in->len ? in->buf + in->pos : NULL;
}

/* the old index's names for the hypertexts taken, numbered as here:
   its blocks read a piece at a time (one read), checked, into in */
static int refill_old(ehv_build *b, input *in, int h)
{
	const ehv_index *f = b->from;
	unsigned long n = (in->cap - in->len) / EHV_BLOCK, k;

	if (n > in->end - in->off)
		n = in->end - in->off;
	if (n == 0)
		return 0;
	if (read_blocks(f, h, in->off, n, b->scratch) < 0)
		return -1;
	in->off += n;
	for (k = 0; k < n; k++)
	{
		unsigned char *blk = b->scratch + k * EHV_BLOCK;
		unsigned long pos = MARK;

		while (pos < EHV_BLOCK && blk[pos] != 0)
		{
			unsigned char *r = blk + pos;
			unsigned long sz = rec_check(r, EHV_BLOCK - pos, f->nguides), g;

			if (sz == 0)
				return -1;                   /* damaged */
			pos += sz;
			g = rec_guide(r);
			if (r[4] == 2 || b->map[g] < 0)
				continue;                    /* ALL.REF's are merged afresh */
			memcpy(in->buf + in->len, r, sz);
			in->buf[in->len + 1] = (unsigned char) (b->map[g] >> 8);
			in->buf[in->len + 2] = (unsigned char) b->map[g];
			in->len += sz;
		}
	}
	return 0;
}

/* an input nearly empty: all of them half empty are filled again with
   it, NAME.RUN and the old index each opened once, so they are opened
   the fewer times */
static int refill(ehv_build *b)
{
	int hr = -1, ho = -1, rc = 0;
	unsigned long i;

	for (i = 0; i < b->nin && rc == 0; i++)
	{
		input *in = &b->in[i];

		if (in->kind == IN_BATCH || in->done || in->len - in->pos >= in->cap / 2)
			continue;
		memmove(in->buf, in->buf + in->pos, in->len - in->pos);
		in->len -= in->pos;
		in->pos = 0;
		if (in->kind == IN_OLD)
		{
			if (b->from->mem == NULL && ho < 0 && (ho = pf_open(b->from->file, PF_READ)) < 0)
				rc = -1;
			else
				rc = refill_old(b, in, ho);
		} else
		{
			unsigned long n = in->cap - in->len < in->end - in->off ? in->cap - in->len : in->end - in->off;

			if (b->runs == NULL)
				memcpy(in->buf + in->len, b->runmem + in->off, n);
			else if ((hr < 0 && (hr = pf_open(b->runs, PF_READ)) < 0) ||
				 pf_read_at(hr, in->off, in->buf + in->len, n) != 0)
				rc = -1;
			in->off += n;
			in->len += n;
		}
		in->done = in->off >= in->end;
	}
	if (hr >= 0)
		pf_close(hr);
	if (ho >= 0)
		pf_close(ho);
	return rc;
}

/* the blocks filled so far: to NAME.ID$ (made by the first), or memory */
static int flush_out(ehv_build *b)
{
	unsigned long n = b->cur;

	if (n == 0)
		return 0;
	if (b->target != NULL)
	{
		int h = pf_open(b->target, b->made ? PF_APPEND : PF_WRITE);

		if (h < 0 && !b->made)
		{
			free(b->target);           /* can't be written: in memory */
			b->target = NULL;
		} else
		{
			long len = (long) (n * EHV_BLOCK);
			int ok = h >= 0 && pf_write(h, b->out, len) == len;

			if (h >= 0)
				pf_close(h);
			b->made = 1;
			if (!ok)
				return -1;
		}
	}
	if (b->target == NULL)
	{
		unsigned char *p = realloc(b->mem, (b->nmem + n) * EHV_BLOCK);

		if (p == NULL)
			return -1;
		memcpy(p + b->nmem * EHV_BLOCK, b->out, n * EHV_BLOCK);
		b->mem = p;
		b->nmem += n;
	}
	b->cur = 0;
	return 0;
}

static int end_block(ehv_build *b)
{
	unsigned char *blk = b->out + b->cur * EHV_BLOCK;

	if (b->bpos == 0)
		return 0;
	memset(blk + b->bpos, 0, EHV_BLOCK - b->bpos);
	b->bpos = 0;
	b->cur++;
	b->nblocks++;
	return 0;
}

/* a name into the blocks */
static int emit(ehv_index *idx, ehv_build *b, const unsigned char *r, unsigned long sz)
{
	unsigned char *blk = b->out + b->cur * EHV_BLOCK;
	unsigned long g = rec_guide(r);

	if (b->bpos == 0)
	{
		/* a new block: its mark, and its first name kept */
		if (b->nblocks == b->cap_first)
		{
			unsigned long ncap = b->cap_first ? b->cap_first * 2 : 64;
			const char **p = realloc((void *) idx->first, ncap * sizeof(char *));

			if (p == NULL)
				return -1;
			idx->first = p;
			b->cap_first = ncap;
		}
		if ((idx->first[b->nblocks] = pool_strdup(&idx->pool, (const char *) r + 7)) == NULL)
			return -1;
		put32(blk, b->gen);
		b->bpos = MARK;
	}
	memcpy(blk + b->bpos, r, sz);
	b->bpos += sz;
	idx->guides[g].nsyms++;
	idx->nsyms++;
	return 0;
}

/* 1 when all are merged.  A step reads, writes or merges: each of
   them is short */
static int merge_some(ehv_index *idx, ehv_build *b, unsigned long budget)
{
	unsigned long i;

	if (b->cur == b->nout)
		return flush_out(b) < 0 ? -1 : 0;   /* the blocks filled, written */
	for (i = 0; i < b->nin; i++)
		if (needs_more(&b->in[i]))
			return refill(b) < 0 ? -1 : 0;   /* more of them read */
	while (budget-- > 0)
	{
		input *best = NULL;
		const unsigned char *r, *rb = NULL;
		unsigned long sz;

		for (i = 0; i < b->nin; i++)
			if ((r = head_of(b, &b->in[i])) != NULL && (rb == NULL || rec_cmp(idx, r, rb) < 0))
			{
				best = &b->in[i];
				rb = r;
			}
		if (best == NULL)
		{
			/* all of them: the last block, and where they are */
			end_block(b);
			return flush_out(b) < 0 ? -1 : 1;
		}
		sz = rec_size(rb);
		if (b->bpos + sz > EHV_BLOCK)
		{
			end_block(b);
			if (b->cur == b->nout)
				return 0;                    /* written in the next step */
		}
		if (emit(idx, b, rb, sz) < 0)
			return -1;
		best->pos += best->kind == IN_BATCH ? 1 : sz;
		if (needs_more(best))
			break;
	}
	return 0;
}

int ehv_names_next_io(const ehv_index *idx)
{
	const ehv_build *b = idx->build;
	unsigned long i;

	if (b == NULL || b->done)
		return 0;
	if (!b->started)
		return 1;
	if (b->cur == b->nout)
		return 1;
	for (i = 0; i < b->nin; i++)
		if (needs_more(&b->in[i]))
			return 1;
	return 0;
}

int ehv_names_finish_some(ehv_index *idx, unsigned int budget)
{
	ehv_build *b = idx->build;
	int r;

	if (b == NULL)
	{
		/* no names at all */
		idx->nblocks = 0;
		return 1;
	}
	if (b->done)
		return 1;
	while (budget-- > 0)
	{
		if (!b->started && b->nrec > 0 && !b->sorted)
		{
			/* the last batch sorted, and merged from where it is */
			if (b->wrote > 0 || batch_sort(idx, b, MOVES))
				b->sorted = 1;
			if (b->wrote > 0 && (r = batch_step(idx, b, MOVES)) < 0)
				return -1;
			continue;
		}
		if (!b->started && merge_begin(idx, b) < 0)
			return -1;
		if ((r = merge_some(idx, b, RECORDS)) < 0)
			return -1;
		if (r == 0)
			continue;
		/* the blocks are the index's now */
		idx->nblocks = b->nblocks;
		idx->gen = b->gen;
		free(idx->file);
		idx->file = NULL;
		free(idx->mem);
		idx->mem = NULL;
		if (b->target != NULL && b->made)
		{
			if ((idx->file = malloc(strlen(b->target) + 1)) == NULL)
				return -1;
			strcpy(idx->file, b->target);
		} else
		{
			idx->mem = b->mem;
			b->mem = NULL;
			if (idx->mem == NULL && idx->nblocks > 0)
				return -1;
		}
		b->done = 1;
		/* what the building needed, given back; NAME.RUN gone */
		{
			char *target = b->target;
			int made = b->made;

			b->target = NULL;
			ehv_names_build_free(idx);
			if (made && target != NULL)
			{
				/* the saver adds the head to it (ehv_names_target()) */
				if ((idx->build = calloc(1, sizeof(ehv_build))) != NULL)
				{
					idx->build->target = target;
					idx->build->made = 1;
					idx->build->done = 1;
				} else
					free(target);
			} else
				free(target);
		}
		return 1;
	}
	return 0;
}
