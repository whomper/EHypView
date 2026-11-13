/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pagebench - how long a page takes to unpack (LH5) and to parse, and a
 * checksum of what each gives, so a faster decoder or parser can be
 * shown to give the same bytes and the same lines and runs.
 *
 *   pagebench FILE.HYP [ROUNDS]       (host: the checksums)
 *   PAGEBENC.PRG                      (Atari: FIXT\BIG.HYP, 3 rounds)
 *
 * Built for the host and for the Atari (make pagebench); on the Atari
 * it prints through NatFeats and ends Hatari with NF_EXIT. The times are
 * the Atari's 200 Hz clock, the best of the rounds; on the host only the
 * checksums count.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "hypfile.h"
#include "page.h"

#ifdef EHV_TOS
#include "tos.h"
#define NOW() tos_hz200()
#else
#define NOW() 0UL
#endif

static void out(const char *s);

/* a checksum (Fletcher-32 over bytes) */
static unsigned long sum_bytes(unsigned long s, const unsigned char *p, unsigned long n)
{
	unsigned long a = s & 0xFFFF, b = s >> 16;

	while (n-- > 0)
	{
		a = (a + *p++) % 65535UL;
		b = (b + a) % 65535UL;
	}
	return (b << 16) | a;
}

static unsigned long sum_u(unsigned long s, unsigned long v)
{
	unsigned char b[4];

	b[0] = (unsigned char) (v >> 24);
	b[1] = (unsigned char) (v >> 16);
	b[2] = (unsigned char) (v >> 8);
	b[3] = (unsigned char) v;
	return sum_bytes(s, b, 4);
}

/* everything the parse made, as numbers: lines, runs (their text as
   offsets into the page and its bytes), pictures, cross references */
static unsigned long sum_page(const hyp_page *pg)
{
	unsigned long s = 1, i;

	s = sum_u(s, pg->nlines);
	s = sum_u(s, pg->nruns);
	s = sum_u(s, pg->width);
	s = sum_u(s, pg->ngfx);
	s = sum_u(s, pg->nxrefs);
	s = sum_u(s, pg->nobjrefs);
	if (pg->title != NULL)
		s = sum_bytes(s, (const unsigned char *) pg->title, strlen(pg->title));
	for (i = 0; i < pg->nlines; i++)
	{
		s = sum_u(s, pg->lines[i].first_run);
		s = sum_u(s, pg->lines[i].nruns);
	}
	for (i = 0; i < pg->nruns; i++)
	{
		const hyp_run *r = &pg->runs[i];

		s = sum_u(s, ((unsigned long) r->kind << 24) | ((unsigned long) r->attr << 16) |
			  ((unsigned long) r->fg << 8) | r->bg);
		s = sum_u(s, ((unsigned long) r->link_kind << 16) | r->target);
		s = sum_u(s, r->target_line);
		s = sum_u(s, r->len);
		/* a link's text outside the page (an empty label: the target's
		   name, in the index) by its bytes only: its address means nothing */
		if (r->text == NULL)
			s = sum_u(s, 0xFFFFFFFFUL);
		else if ((const unsigned char *) r->text >= pg->data && (const unsigned char *) r->text < pg->data + pg->size)
			s = sum_u(s, (unsigned long) ((const unsigned char *) r->text - pg->data));
		else
			s = sum_u(s, 0xFFFFFFFEUL);
		if (r->text != NULL)
			s = sum_bytes(s, (const unsigned char *) r->text, r->len);
	}
	for (i = 0; i < pg->ngfx; i++)
	{
		const hyp_gfx *g = &pg->gfx[i];

		s = sum_u(s, ((unsigned long) g->type << 24) | ((unsigned long) g->x << 16) | g->y);
		s = sum_u(s, ((unsigned long) (unsigned short) g->w << 16) | (unsigned short) g->h);
		s = sum_u(s, ((unsigned long) g->image << 16) | g->dithermask);
		s = sum_u(s, ((unsigned long) g->limage << 24) | ((unsigned long) g->has_dither << 16) |
			  ((unsigned long) g->arrows << 8) | g->style);
		s = sum_u(s, g->pattern);
	}
	for (i = 0; i < pg->nxrefs; i++)
	{
		s = sum_u(s, pg->xrefs[i].target);
		s = sum_bytes(s, (const unsigned char *) pg->xrefs[i].text, strlen(pg->xrefs[i].text));
	}
	for (i = 0; i < pg->nobjrefs; i++)
	{
		const hyp_objref *o = &pg->objrefs[i];

		s = sum_u(s, ((unsigned long) o->line << 16) | o->tree);
		s = sum_u(s, ((unsigned long) o->object << 16) | o->target);
	}
	return s;
}

/* the same object unpacked a piece at a time (hyp_unpack_more, as a
   search and a picture do), `piece` bytes a call: its checksum, which
   must be the one of hyp_load's at once; 0 when it failed */
static unsigned long sum_pieces(hyp_doc *doc, unsigned short n, unsigned long piece)
{
	int rc;
	unsigned long size, s = 0;
	unsigned char *data;
	hyp_unpack *u = hyp_unpack_begin(doc, n, &rc);

	if (u == NULL)
		return 0;
	while ((rc = hyp_unpack_more(u, piece)) == 0)
		;
	if (rc > 0 && (data = hyp_unpack_take(u, &size)) != NULL)
	{
		s = sum_bytes(1, data, size);
		free(data);
	} else
		hyp_unpack_free(u);
	return s;
}

/* every text page of the file: unpacked, then parsed; the checksums, and
   the best time of `rounds` for the biggest page */
static int bench(const char *path, int rounds)
{
	char line[200];
	int err, r;
	unsigned short n, big = 0;
	unsigned long best_unpack = 0xFFFFFFFFUL, best_all = 0xFFFFFFFFUL;
	hyp_doc *doc = hyp_open(path, &err);

	if (doc == NULL)
	{
		snprintf(line, sizeof(line), "cannot open %s: %d\n", path, err);
		out(line);
		return 1;
	}
	for (n = 0; n < doc->num_entries; n++)
	{
		unsigned char *data;
		unsigned long size;
		hyp_page *pg;

		if (!HYP_IS_TEXT(doc->entries[n].type))
		{
			/* pictures and the rest: unpacked by the same decoder */
			if (doc->entries[n].size > 0 && hyp_load(doc, n, &data, &size) == HYP_OK)
			{
				unsigned long a = sum_bytes(1, data, size);

				snprintf(line, sizeof(line), "object %u: %lu bytes %08lx%s\n", n, size, a,
					 sum_pieces(doc, n, 97) == a ? "" : " PIECES DIFFER");
				out(line);
				free(data);
			}
			continue;
		}
		if (doc->entries[n].size > doc->entries[big].size || !HYP_IS_TEXT(doc->entries[big].type))
			big = n;
		if (hyp_load(doc, n, &data, &size) != HYP_OK)
		{
			snprintf(line, sizeof(line), "page %u: cannot unpack\n", n);
			out(line);
			continue;
		}
		pg = hyp_page_load(doc, n, &err);
		snprintf(line, sizeof(line), "page %u: %lu bytes %08lx, parsed %08lx%s\n", n, size,
			 sum_bytes(1, data, size), pg != NULL ? sum_page(pg) : 0UL,
			 sum_pieces(doc, n, 97) == sum_bytes(1, data, size) ? "" : " PIECES DIFFER");
		out(line);
		free(data);
		hyp_page_free(pg);
	}
	for (r = 0; r < rounds; r++)
	{
		unsigned char *data;
		unsigned long size, t0 = NOW(), t1, t2;
		hyp_page *pg;

		if (hyp_load(doc, big, &data, &size) != HYP_OK)
			break;
		t1 = NOW();
		free(data);
		pg = hyp_page_load(doc, big, &err);
		t2 = NOW();
		hyp_page_free(pg);
		if (t1 - t0 < best_unpack)
			best_unpack = t1 - t0;
		if (t2 - t1 < best_all)
			best_all = t2 - t1;
	}
	snprintf(line, sizeof(line), "TIME page %u: unpack %lu ms, unpack and parse %lu ms, parse %lu ms\n", big,
		 best_unpack * 5, best_all * 5, (best_all - best_unpack) * 5);
	out(line);
	hyp_close(doc);
	return 0;
}

#ifdef EHV_TOS

long nf_find(const char *name);
long nf_call2(long id, long arg);
void nf_debug(const char *s);

static const char *nf_name;
static long nf_found;

static void sup_find(void)
{
	nf_found = nf_find(nf_name);
}

static long natfeat(const char *name)
{
	nf_name = name;
	Supexec(sup_find);
	return nf_found;
}

static void out(const char *s)
{
	nf_debug(s);
}

int main(void)
{
	long id;
	int rc;

	out("PAGEBENCH\n");
	rc = bench("C:\\FIXT\\BIG.HYP", 3);
	out("END\n");
	if ((id = natfeat("NF_EXIT")) > 0)
		nf_call2(id, rc);
	return rc;
}

#else

static void out(const char *s)
{
	fputs(s, stdout);
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		fputs("usage: pagebench FILE.HYP [ROUNDS]\n", stderr);
		return 2;
	}
	return bench(argv[1], argc > 2 ? atoi(argv[2]) : 1);
}

#endif
