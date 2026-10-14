/*
 * EHypView - full-text search through every indexed hypertext.
 *
 * Copyright (C) the EHypView authors.  Free software under the GNU
 * General Public License, version 2 or (at your option) any later
 * version; see COPYING.
 */
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "hypfile.h"
#include "page.h"
#include "search.h"

#define QMAX 64

struct ehv_fts {
	const ehv_index *idx;
	char query[QMAX];             /* as typed                     */
	char folded[QMAX];            /* lower case                   */
	size_t qlen;
	unsigned long guide;          /* hypertext being searched     */
	hyp_doc *doc;                 /* open for the current guide   */
	unsigned short entry;         /* next entry to look at        */
	int done;
	unsigned long pages;
	ehv_fts_hit *hits;
	unsigned long nhits, cap;
	char *line;                   /* buffer for one folded line   */
	size_t line_cap;
};

ehv_fts *ehv_fts_begin(const ehv_index *idx, const char *query)
{
	ehv_fts *s;
	size_t i, n = strlen(query);

	if (n == 0 || idx == NULL)
		return NULL;
	if (n >= QMAX)
		n = QMAX - 1;
	s = calloc(1, sizeof(*s));
	if (s == NULL)
		return NULL;
	s->idx = idx;
	memcpy(s->query, query, n);
	for (i = 0; i < n; i++)
		s->folded[i] = (char) atari_tolower((unsigned char) query[i]);
	s->qlen = n;
	return s;
}

void ehv_fts_free(ehv_fts *s)
{
	unsigned long i;

	if (s == NULL)
		return;
	hyp_close(s->doc);
	for (i = 0; i < s->nhits; i++)
		free(s->hits[i].node);
	free(s->hits);
	free(s->line);
	free(s);
}

const ehv_fts_hit *ehv_fts_hits(const ehv_fts *s, unsigned long *count)
{
	*count = s->nhits;
	return s->hits;
}

unsigned long ehv_fts_pages(const ehv_fts *s) { return s->pages; }
int ehv_fts_done(const ehv_fts *s) { return s->done; }
const char *ehv_fts_query(const ehv_fts *s) { return s->query; }

/* the text of one line, as typed and folded; -1 when out of memory */
static long line_text(ehv_fts *s, const hyp_page *pg, unsigned short l)
{
	const hyp_line *ln = &pg->lines[l];
	size_t n = 0, i;
	unsigned short r;

	for (r = 0; r < ln->nruns; r++)
		n += pg->runs[ln->first_run + r].len;
	if (2 * n + 2 > s->line_cap)
	{
		char *p = realloc(s->line, 2 * n + 2);

		if (p == NULL)
			return -1;
		s->line = p;
		s->line_cap = 2 * n + 2;
	}
	/* first half: original text; second half: folded copy */
	n = 0;
	for (r = 0; r < ln->nruns; r++)
	{
		const hyp_run *run = &pg->runs[ln->first_run + r];

		memcpy(s->line + n, run->text, run->len);
		n += run->len;
	}
	s->line[n] = '\0';
	for (i = 0; i < n; i++)
		s->line[n + 1 + i] = (char) atari_tolower((unsigned char) s->line[i]);
	s->line[2 * n + 1] = '\0';
	return (long) n;
}

static const char *find(const char *hay, size_t hlen, const char *needle, size_t nlen)
{
	size_t i;

	if (nlen > hlen)
		return NULL;
	for (i = 0; i + nlen <= hlen; i++)
		if (hay[i] == needle[0] && memcmp(hay + i, needle, nlen) == 0)
			return hay + i;
	return NULL;
}

static int add_hit(ehv_fts *s, unsigned short line, unsigned short count, const char *text, size_t len,
                   size_t at)
{
	ehv_fts_hit *h;
	size_t start, n;

	if (s->nhits == s->cap)
	{
		unsigned long ncap = s->cap ? s->cap * 2 : 32;
		ehv_fts_hit *p = realloc(s->hits, ncap * sizeof(ehv_fts_hit));

		if (p == NULL)
			return -1;
		s->hits = p;
		s->cap = ncap;
	}
	h = &s->hits[s->nhits];
	h->guide = s->guide;
	h->line = line;
	h->count = count;
	h->node = malloc(strlen(s->doc->entries[s->entry].name) + 1);
	if (h->node == NULL)
		return -1;
	strcpy(h->node, s->doc->entries[s->entry].name);

	/* some text before the match, the match, and what follows */
	start = at > 16 ? at - 16 : 0;
	while (start < at && text[start] == ' ')
		start++;
	n = len - start;
	if (n > sizeof(h->context) - 1)
		n = sizeof(h->context) - 1;
	memcpy(h->context, text + start, n);
	h->context[n] = '\0';
	s->nhits++;
	return 0;
}

/* search one text page; -1 when out of memory */
static int search_page(ehv_fts *s)
{
	hyp_page *pg = hyp_page_load(s->doc, s->entry, NULL);
	unsigned short l, first = 0, count = 0;
	size_t first_at = 0;
	char *first_text = NULL;
	size_t first_len = 0;
	int rc = 0;

	if (pg == NULL)
		return 0;                    /* damaged page: skip it */
	for (l = 0; l < pg->nlines; l++)
	{
		long n = line_text(s, pg, l);
		const char *m;

		if (n < 0)
		{
			rc = -1;
			break;
		}
		m = find(s->line + n + 1, (size_t) n, s->folded, s->qlen);
		if (m == NULL)
			continue;
		if (count == 0)
		{
			first = l;
			first_at = (size_t) (m - (s->line + n + 1));
			first_len = (size_t) n;
			first_text = malloc((size_t) n + 1);
			if (first_text == NULL)
			{
				rc = -1;
				break;
			}
			memcpy(first_text, s->line, (size_t) n + 1);
		}
		if (count < 0xFFFF)
			count++;
	}
	if (rc == 0 && count > 0 && s->nhits < EHV_FTS_MAX_HITS)
		rc = add_hit(s, first, count, first_text, first_len, first_at);
	free(first_text);
	hyp_page_free(pg);
	return rc;
}

int ehv_fts_step(ehv_fts *s, unsigned int budget)
{
	while (budget > 0 && !s->done)
	{
		if (s->doc == NULL)
		{
			/* next readable hypertext */
			while (s->guide < s->idx->nguides && s->idx->guides[s->guide].error != HYP_OK)
				s->guide++;
			if (s->guide >= s->idx->nguides || s->nhits >= EHV_FTS_MAX_HITS)
			{
				s->done = 1;
				break;
			}
			s->doc = hyp_open(s->idx->guides[s->guide].path, NULL);
			s->entry = 0;
			if (s->doc == NULL)
			{
				s->guide++;
				continue;
			}
		}
		if (s->entry >= s->doc->num_entries)
		{
			hyp_close(s->doc);
			s->doc = NULL;
			s->guide++;
			continue;
		}
		if (HYP_IS_TEXT(s->doc->entries[s->entry].type))
		{
			if (search_page(s) < 0)
				return -1;
			s->pages++;
			budget--;
		}
		s->entry++;
	}
	return s->done ? 1 : 0;
}
