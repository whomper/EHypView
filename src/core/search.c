/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - full-text search through every indexed hypertext.
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
	/* the page being searched, a piece at a time: read and unpacked
	   PAGE_STEP bytes a step, then LINES_STEP lines (a big page took
	   2 s on an ST, more than a second on a Falcon) */
	hyp_page_job *job;
	hyp_page *pg;
	unsigned short l, first, count;
	size_t first_at, first_len;
	char *first_text;
};

#define LINES_STEP 16
#define PAGE_STEP 1024

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
	hyp_page_job_free(s->job);
	hyp_page_free(s->pg);
	free(s->first_text);
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
	{
		const unsigned char *t = atari_lower_table();

		for (i = 0; i < n; i++)
			s->line[n + 1 + i] = (char) t[(unsigned char) s->line[i]];
	}
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

/* the next piece of the page being searched (read, unpacked and parsed
   first, PAGE_STEP bytes a step; then LINES_STEP lines a step): 1 the
   page done, 0 more of it, -1 out of memory */
static int search_page(ehv_fts *s)
{
	unsigned short end;
	int rc = 0;

	if (s->pg == NULL)
	{
		int r;

		if (s->job == NULL)
		{
			hyp_hold(s->doc);            /* one open for the step's pages */
			return (s->job = hyp_page_begin(s->doc, s->entry, NULL)) == NULL;    /* damaged: skipped */
		}
		if ((r = hyp_page_more(s->job, PAGE_STEP)) == 0)
			return 0;
		if (r < 0)
		{
			hyp_page_job_free(s->job);   /* damaged page: skip it */
			s->job = NULL;
			return 1;
		}
		s->pg = hyp_page_take(s->job);
		s->job = NULL;
		s->l = s->count = 0;
		return s->pg->nlines == 0 ? (hyp_page_free(s->pg), s->pg = NULL, 1) : 0;
	}
	end = (unsigned short) (s->pg->nlines - s->l > LINES_STEP ? s->l + LINES_STEP : s->pg->nlines);
	for (; s->l < end; s->l++)
	{
		long n = line_text(s, s->pg, s->l);
		const char *m;

		if (n < 0)
		{
			rc = -1;
			break;
		}
		m = find(s->line + n + 1, (size_t) n, s->folded, s->qlen);
		if (m == NULL)
			continue;
		if (s->count == 0)
		{
			s->first = s->l;
			s->first_at = (size_t) (m - (s->line + n + 1));
			s->first_len = (size_t) n;
			s->first_text = malloc((size_t) n + 1);
			if (s->first_text == NULL)
			{
				rc = -1;
				break;
			}
			memcpy(s->first_text, s->line, (size_t) n + 1);
		}
		if (s->count < 0xFFFF)
			s->count++;
	}
	if (rc == 0 && s->l < s->pg->nlines)
		return 0;
	if (rc == 0 && s->count > 0 && s->nhits < EHV_FTS_MAX_HITS)
		rc = add_hit(s, s->first, s->count, s->first_text, s->first_len, s->first_at);
	free(s->first_text);
	s->first_text = NULL;
	hyp_page_free(s->pg);
	s->pg = NULL;
	return rc < 0 ? -1 : 1;
}

int ehv_fts_step(ehv_fts *s, unsigned int budget, int (*more)(void))
{
	int first = 1;

	while (budget > 0 && !s->done && (first || more == NULL || more()))
	{
		first = 0;
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
			s->doc = hyp_open_held(s->idx->guides[s->guide].path, NULL);
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
			int r = search_page(s);

			if (r < 0)
			{
				hyp_let_go(s->doc);
				return -1;
			}
			budget--;
			if (r == 0)
				continue;                /* more of this page */
			s->pages++;
		}
		s->entry++;
	}
	hyp_let_go(s->doc);                  /* no file kept open across an event call */
	return s->done ? 1 : 0;
}
