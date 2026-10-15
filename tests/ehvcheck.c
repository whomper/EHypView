/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ehvcheck - exercise the EHypView core through the platform layer and
 * print a report that does not depend on the platform.
 *
 * Built twice from this file: for the host (plat_posix.c) and as
 * EHVCHECK.PRG for the Atari (plat_tos.c, GEMDOS).  tests/run_hatari.sh
 * runs the PRG in Hatari/EmuTOS and compares its report with the host's.
 * It can also be run on a real Atari: copy build/hatari/ to a drive and
 * start EHVCHECK.PRG (it reads FIXT next to it unless given a folder).
 *
 *   ehvcheck [ROOT]
 *
 * ROOT holds BASIC.HYP, BASIC.REF, BIG.HYP, BROKEN.HYP, ALL.REF,
 * IMAGES\IMAGES.HYP and SUB\DEEP\OTHER.HYP (see the Makefile).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "fsys.h"
#include "plat.h"
#include "hypfile.h"
#include "page.h"
#include "index.h"
#include "scan.h"
#include "resolve.h"

#ifdef EHV_TOS
#include "tos.h"
#endif

static const char *root;
static size_t root_len;

/* ---------------------------------------------------------------- */
/* output                                                            */

static void out(const char *s)
{
#ifdef EHV_TOS
	char buf[300];
	const char *p;

	nf_debug(s);
	/* the screen wants CR LF */
	for (p = s; *p != '\0'; )
	{
		size_t n = 0;

		while (*p != '\0' && *p != '\n' && n < sizeof(buf) - 3)
			buf[n++] = *p++;
		if (*p == '\n')
		{
			buf[n++] = '\r';
			buf[n++] = '\n';
			p++;
		}
		buf[n] = '\0';
		Cconws(buf);
	}
#else
	fputs(s, stdout);
#endif
}

static void outf(const char *fmt, const char *a, const char *b)
{
	char line[400];

	snprintf(line, sizeof(line), fmt, a ? a : "-", b ? b : "-");
	out(line);
}

/* a path relative to the root, lower case, '/' separated */
static const char *rel(const char *path)
{
	static char buf[4][300];
	static int k;
	char *d;
	const char *p = path;

	if (path == NULL)
		return "-";
	k = (k + 1) % 4;
	d = buf[k];
	if (strlen(p) >= root_len && atari_ncasecmp(p, root, root_len) == 0)
		p += root_len;
	while (fs_is_sep(*p))
		p++;
	while (*p != '\0' && d < buf[k] + sizeof(buf[k]) - 1)
	{
		char c = *p++;

		*d++ = fs_is_sep(c) ? '/' : (char) atari_tolower((unsigned char) c);
	}
	*d = '\0';
	return buf[k];
}

static unsigned long fnv(const unsigned char *p, unsigned long n)
{
	unsigned long h = 2166136261UL;

	while (n-- > 0)
		h = ((h ^ *p++) * 16777619UL) & 0xFFFFFFFFUL;
	return h;
}

static char *path_in_root(const char *name)
{
	return fs_join(root, name);
}

/* ---------------------------------------------------------------- */

static int failures;

static void check(int ok, const char *what)
{
	if (!ok)
	{
		failures++;
		outf("FAIL %s%s\n", what, "");
	}
}

static int cmp_guides(const void *a, const void *b, const ehv_index *idx)
{
	const ehv_guide *x = &idx->guides[*(const unsigned long *) a];
	const ehv_guide *y = &idx->guides[*(const unsigned long *) b];
	char rx[300];

	strcpy(rx, rel(x->path));
	return strcmp(rx, rel(y->path));
}

static const ehv_index *sort_idx;

static int cmp_guides_q(const void *a, const void *b)
{
	return cmp_guides(a, b, sort_idx);
}

static void report_guide(const ehv_index *idx, unsigned long g)
{
	const ehv_guide *gd = &idx->guides[g];
	char line[400];
	hyp_doc *doc;
	unsigned short i;
	int err;

	snprintf(line, sizeof(line), "guide %s err=%d names=%lu ownref=%s\n",
		rel(gd->path), gd->error, gd->nsyms, gd->ref_path ? "yes" : "no");
	out(line);
	if (gd->error != HYP_OK)
		return;
	outf("  database=%s subject=%s\n", gd->database, gd->subject);

	doc = hyp_open(gd->path, &err);
	check(doc != NULL, "hyp_open");
	if (doc == NULL)
		return;
	for (i = 0; i < doc->num_entries; i++)
	{
		const hyp_entry *e = &doc->entries[i];

		if (HYP_IS_TEXT(e->type))
		{
			hyp_page *pg = hyp_page_load(doc, i, &err);

			check(pg != NULL, "page");
			if (pg == NULL)
				continue;
			snprintf(line, sizeof(line), "  page %s lines=%u runs=%lu gfx=%u xrefs=%u sum=%08lx\n",
				e->name, pg->nlines, pg->nruns, pg->ngfx, pg->nxrefs, fnv(pg->data, pg->size));
			out(line);
			hyp_page_free(pg);
		} else if (e->type == HYP_NODE_IMAGE)
		{
			unsigned char *data;
			unsigned long size;

			err = hyp_load(doc, i, &data, &size);
			check(err == HYP_OK, "image");
			if (err != HYP_OK)
				continue;
			snprintf(line, sizeof(line), "  image %s size=%lu sum=%08lx\n", e->name, size, fnv(data, size));
			out(line);
			free(data);
		}
	}
	hyp_close(doc);
}

static void report_target(const char *what, const char *arg, const ehv_target *t, const ehv_index *idx)
{
	char line[400];
	char sugg[120] = "";
	unsigned int i;

	for (i = 0; i < t->nsuggest && i < 3; i++)
	{
		const char *s = t->status == EHV_NO_FILE ? idx->guides[t->suggest[i]].key
			: idx->syms[t->suggest[i]].name;

		if (strlen(sugg) + strlen(s) + 2 < sizeof(sugg))
		{
			strcat(sugg, " ");
			strcat(sugg, s);
		}
	}
	snprintf(line, sizeof(line), "%s %s -> status=%d file=%s node=%s line=%u alt=%u suggest=%s\n",
		what, arg, t->status, rel(t->path), t->node ? t->node : "(default)", t->line, t->nalt,
		sugg[0] ? sugg + 1 : "-");
	out(line);
}

static void report_index(const ehv_index *idx, const ehv_scan_status *st)
{
	char line[300];
	unsigned long *order = malloc((idx->nguides + 1) * sizeof(unsigned long));
	unsigned long g;

	snprintf(line, sizeof(line), "scan: dirs=%lu opened=%lu reused=%lu failed=%lu guides=%lu names=%lu orphans=%lu\n",
		st->dirs, st->opened, st->reused, st->failed, idx->nguides, idx->nsyms, idx->norphans);
	out(line);
	for (g = 0; g < idx->norphans; g++)
		outf("orphan %s%s\n", rel(idx->orphans[g]), "");
	if (order == NULL)
		return;
	for (g = 0; g < idx->nguides; g++)
		order[g] = g;
	sort_idx = idx;
	qsort(order, idx->nguides, sizeof(unsigned long), cmp_guides_q);
	for (g = 0; g < idx->nguides; g++)
		report_guide(idx, order[g]);
	free(order);
}

static ehv_index *scan(const ehv_index *prev, const ref_db *allref, ehv_scan_status *st)
{
	const char *roots[1];
	ehv_scan *s;
	int r;

	roots[0] = root;
	s = ehv_scan_begin(roots, 1, prev);
	if (s == NULL)
		return NULL;
	while ((r = ehv_scan_step(s, 4)) == 0)
		;
	*st = *ehv_scan_status_get(s);
	if (r < 0)
	{
		ehv_scan_abort(s);
		return NULL;
	}
	return ehv_scan_finish(s, allref);
}

static int run(void)
{
	char *allref_path = path_in_root("ALL.REF");
	char *cache_path = path_in_root("EHV.IDX");
	char *basic_path;
	ref_db *allref;
	ehv_index *idx, *loaded, *again;
	ehv_scan_status st;
	ehv_context ctx;
	ehv_target t;
	unsigned long hits[4];
	char line[200];

	out("EHVCHECK 1\n");
	pf_remove(cache_path);
	allref = ref_open(allref_path, NULL);
	check(allref != NULL, "ALL.REF");

	idx = scan(NULL, allref, &st);
	check(idx != NULL, "scan");
	if (idx == NULL)
		return 1;
	report_index(idx, &st);

	/* resolving */
	memset(&ctx, 0, sizeof(ctx));
	ctx.idx = idx;
	basic_path = NULL;
	if (ehv_index_find_guides(idx, "BASIC.HYP", hits, 1) > 0)
		basic_path = (char *) idx->guides[hits[0]].path;
	ctx.current = basic_path;

	ehv_resolve_xref(&ctx, "other.hyp/Start", &t);
	report_target("xref", "other.hyp/Start", &t, idx);
	ehv_target_free(&t);
	ehv_resolve_xref(&ctx, "OTHER.HYP/anfang", &t);
	report_target("xref", "OTHER.HYP/anfang", &t, idx);
	ehv_target_free(&t);
	ehv_resolve_xref(&ctx, "other.hyp/Strat", &t);
	report_target("xref", "other.hyp/Strat", &t, idx);
	ehv_target_free(&t);
	ehv_resolve_xref(&ctx, "othr.hyp/Start", &t);
	report_target("xref", "othr.hyp/Start", &t, idx);
	ehv_target_free(&t);
	ehv_resolve_xref(&ctx, "basic.hyp/Zweite", &t);
	report_target("xref", "basic.hyp/Zweite", &t, idx);
	ehv_target_free(&t);
	ehv_resolve_name(&ctx, "Detail", &t);
	report_target("name", "Detail", &t, idx);
	ehv_target_free(&t);
	ehv_resolve_name(&ctx, "Detial", &t);
	report_target("name", "Detial", &t, idx);
	ehv_target_free(&t);
	ehv_resolve_request(&ctx, "'*:\\OTHER.HYP' Detail", &t);
	report_target("request", "'*:\\OTHER.HYP' Detail", &t, idx);
	ehv_target_free(&t);
	ehv_resolve_request(&ctx, "secondkw", &t);
	report_target("request", "secondkw", &t, idx);
	ehv_target_free(&t);

	/* without an index: the folder of the open file and its REF */
	ctx.idx = NULL;
	ehv_resolve_xref(&ctx, "BASIC.HYP/zweite", &t);
	report_target("noindex", "BASIC.HYP/zweite", &t, idx);
	ehv_target_free(&t);

	/* cache */
	check(ehv_index_save(idx, cache_path) == 0, "cache save");
	loaded = ehv_index_load(cache_path);
	check(loaded != NULL, "cache load");
	if (loaded != NULL)
	{
		snprintf(line, sizeof(line), "cache: guides=%lu\n", loaded->nguides);
		out(line);
		again = scan(loaded, allref, &st);
		check(again != NULL, "rescan");
		if (again != NULL)
		{
			snprintf(line, sizeof(line), "rescan: opened=%lu reused=%lu failed=%lu guides=%lu names=%lu\n",
				st.opened, st.reused, st.failed, again->nguides, again->nsyms);
			out(line);
			ehv_index_free(again);
		}
		ehv_index_free(loaded);
	}
	pf_remove(cache_path);

	ehv_index_free(idx);
	ref_close(allref);
	free(allref_path);
	free(cache_path);
	snprintf(line, sizeof(line), "END failures=%d\n", failures);
	out(line);
	return failures ? 1 : 0;
}

#ifdef EHV_TOS

long nf_find(const char *name);
long nf_call2(long id, long arg);

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

extern void *_BasePage;

int main(void)
{
	/* command line: length byte, then the text */
	static char arg[130];
	const unsigned char *cmd = (const unsigned char *) _BasePage + 0x80;
	int n = cmd[0] < 127 ? cmd[0] : 126, rc;
	long id;

	memcpy(arg, cmd + 1, (size_t) n);
	arg[n] = '\0';
	while (n > 0 && (arg[n - 1] == ' ' || arg[n - 1] == '\r' || arg[n - 1] == '\n'))
		arg[--n] = '\0';
	if (arg[0] != '\0')
		root = arg;
	else
	{
		/* FIXT next to the program (the current folder) */
		static char cwd[160];
		short drv = (short) Dgetdrv();

		cwd[0] = (char) ('A' + drv);
		cwd[1] = ':';
		Dgetpath(cwd + 2, 0);
		if (cwd[2] == '\0')
			strcpy(cwd + 2, "\\");
		root = fs_join(cwd, "FIXT");
	}
	root_len = strlen(root);
	rc = run();

	/* under Hatari: end the emulator with the result */
	if ((id = natfeat("NF_EXIT")) > 0)
		nf_call2(id, rc);
	Cconws("Press a key.\r\n");
	Bconin(2);
	return rc;
}

#else

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		fputs("usage: ehvcheck ROOT\n", stderr);
		return 2;
	}
	root = argv[1];
	root_len = strlen(root);
	return run();
}

#endif
