/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView tests for finding hypertexts: scanner, index, cache, resolver.
 *
 *   test_index FIXTURE_DIR
 *
 * Builds a folder tree in the current directory (test_index_tree) from
 * the fixtures:
 *
 *   r1/BASIC.HYP                       basic.hyp
 *   r1/Basic.Ref                       its REF file, other spelling
 *   r1/notes.txt
 *   r1/Long Folder Name/images.hyp
 *   r1/sub/deeper/deepest/Other.Hyp    other.hyp, no REF beside it
 *   r2/basic.hyp                       the same file again
 *   r2/broken.hyp                      damaged
 *   r2/sub/                            empty
 *   r2/loop -> ..                      symbolic link back up (POSIX hosts)
 *   all.ref                            basic + other + missing "ghost"
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "charset.h"
#include "fsys.h"
#include "plat.h"
#include "index.h"
#include "scan.h"
#include "resolve.h"
#include "inf.h"
#include "libjob.h"
#include "names.h"

static int failures, checks;
static const char *fixdir;
static char tree[1024];

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

/* ---------------------------------------------------------------- */
/* tree setup                                                        */

static char *tp(const char *rel)
{
	static char buf[8][1200];
	static int k;

	k = (k + 1) % 8;
	snprintf(buf[k], sizeof(buf[k]), "%s/%s", tree, rel);
	return buf[k];
}

static void copy_file(const char *src, const char *dst)
{
	char path[1024];
	FILE *in, *out;
	char buf[4096];
	size_t n;

	snprintf(path, sizeof(path), "%s/%s", fixdir, src);
	in = fopen(path, "rb");
	out = fopen(dst, "wb");
	if (in == NULL || out == NULL)
	{
		printf("FAIL cannot copy %s to %s\n", path, dst);
		exit(1);
	}
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
		fwrite(buf, 1, n, out);
	fclose(in);
	fclose(out);
}

static void write_file(const char *path, const char *data, size_t len)
{
	FILE *fp = fopen(path, "wb");

	fwrite(data, 1, len, fp);
	fclose(fp);
}

static void rm_tree(const char *path)
{
	struct stat st;

	if (lstat(path, &st) != 0)
		return;
	if (S_ISDIR(st.st_mode))
	{
		char **names;
		long n = pf_list_dir(path, &names), i;

		for (i = 0; i < n; i++)
		{
			char *sub = fs_join(path, names[i]);

			rm_tree(sub);
			free(sub);
		}
		pf_free_list(names, n);
		rmdir(path);
	} else
	{
		unlink(path);
	}
}

static void make_tree(void)
{
	char cwd[512];

	if (getcwd(cwd, sizeof(cwd)) == NULL)
		exit(1);
	snprintf(tree, sizeof(tree), "%s/test_index_tree", cwd);
	rm_tree(tree);
	mkdir(tree, 0755);
	mkdir(tp("r1"), 0755);
	mkdir(tp("r1/Long Folder Name"), 0755);
	mkdir(tp("r1/sub"), 0755);
	mkdir(tp("r1/sub/deeper"), 0755);
	mkdir(tp("r1/sub/deeper/deepest"), 0755);
	mkdir(tp("r2"), 0755);
	mkdir(tp("r2/sub"), 0755);
	copy_file("basic.hyp", tp("r1/BASIC.HYP"));
	copy_file("basic.ref", tp("r1/Basic.Ref"));
	write_file(tp("r1/notes.txt"), "not a hypertext\n", 16);
	copy_file("images.hyp", tp("r1/Long Folder Name/images.hyp"));
	copy_file("other.hyp", tp("r1/sub/deeper/deepest/Other.Hyp"));
	copy_file("basic.hyp", tp("r2/basic.hyp"));
	write_file(tp("r2/broken.hyp"), "HDOC\0\0\0\x40\0\x05\x03\x02garbage", 19);
	if (symlink("..", tp("r2/loop")) != 0)
		printf("note: no symbolic link loop test\n");
	copy_file("all.ref", tp("all.ref"));
}

/* ---------------------------------------------------------------- */
/* helpers                                                           */

static int ends_with(const char *s, const char *tail)
{
	size_t ls, lt;

	if (s == NULL)
		return 0;
	ls = strlen(s);
	lt = strlen(tail);
	return ls >= lt && strcmp(s + ls - lt, tail) == 0;
}

static long guide_named(const ehv_index *idx, const char *tail)
{
	unsigned long g;

	for (g = 0; g < idx->nguides; g++)
		if (ends_with(idx->guides[g].path, tail))
			return (long) g;
	return -1;
}

/* all the names, in their order (a copy) */
static void all_names(const ehv_index *idx, ehv_hits *out)
{
	ehv_names nm;
	ehv_sym s;

	ehv_hits_init(out);
	if (ehv_names_open(&nm, idx, 4, 1) < 0)
		return;
	while (ehv_names_next(&nm, &s) == 1)
		ehv_hits_add(out, &s);
	ehv_names_close(&nm);
}

/* a guide's names (from_ref < 3: all; else not ALL.REF's) */
static unsigned long names_of(const ehv_index *idx, long g, int from_ref)
{
	ehv_hits h;
	unsigned long i, n = 0;

	all_names(idx, &h);
	for (i = 0; i < h.n; i++)
		if (h.s[i].guide == (unsigned long) g && (from_ref < 0 || h.s[i].from_ref == from_ref))
			n++;
	ehv_hits_free(&h);
	return n;
}

/* symbol of a guide by name and kind (until the next call) */
static ehv_hits held;

static const ehv_sym *sym_of(const ehv_index *idx, long g, const char *name, int kind)
{
	unsigned long i;

	ehv_hits_free(&held);
	if (g < 0)
		return NULL;
	ehv_index_find_symbol(idx, name, g, &held, 64);
	for (i = 0; i < held.n; i++)
		if (strcmp(held.s[i].name, name) == 0 && held.s[i].kind == kind)
			return &held.s[i];
	return NULL;
}

static int has_suggestion_sym(const ehv_target *t, const char *name)
{
	unsigned long i;

	for (i = 0; i < t->similar.n; i++)
		if (strcmp(t->similar.s[i].name, name) == 0)
			return 1;
	return 0;
}

static char *roots[4];

static ehv_index *scan_tree(const ehv_index *prev, const ref_db *allref, ehv_scan_status *st)
{
	ehv_scan *s = ehv_scan_begin((const char *const *) roots, 4, prev);
	int r;

	while ((r = ehv_scan_step(s, 3)) == 0)
		;
	CHECK(r == 1);
	*st = *ehv_scan_status_get(s);
	return ehv_scan_finish(s, allref);
}

/* ---------------------------------------------------------------- */

static void test_fsys(void)
{
	char *s;
	char **list;

	s = fs_dirname("C:\\GUIDES\\X.HYP");
	CHECK(strcmp(s, "C:\\GUIDES") == 0);
	free(s);
	s = fs_dirname("C:\\X.HYP");
	CHECK(strcmp(s, "C:\\") == 0);
	free(s);
	s = fs_dirname("X.HYP");
	CHECK(strcmp(s, "") == 0);
	free(s);
	CHECK(strcmp(fs_basename("C:X.HYP"), "X.HYP") == 0);
	CHECK(strcmp(fs_basename("/a/b/c.hyp"), "c.hyp") == 0);
	s = fs_join("C:\\GUIDES", "A.HYP");
	CHECK(strcmp(s, "C:\\GUIDES\\A.HYP") == 0);
	free(s);
	s = fs_join("/tmp/", "a");
	CHECK(strcmp(s, "/tmp/a") == 0);
	free(s);
	s = fs_join("C:", "A.HYP");
	CHECK(strcmp(s, "C:A.HYP") == 0);
	free(s);
	CHECK(fs_has_ext("FOO.HYP", ".hyp") && !fs_has_ext(".hyp", ".hyp") && !fs_has_ext("foo.hy", ".hyp"));
	CHECK(fs_has_dir("C:FOO.HYP") && fs_has_dir("a/b") && !fs_has_dir("b.hyp"));
#ifndef __MINT__
	CHECK(fs_expand_root("*:\\GUIDES", &list) == 0 && list == NULL);
#endif
	CHECK(fs_expand_root("C:\\GUIDES", &list) == 1 && strcmp(list[0], "C:\\GUIDES") == 0);
	inf_free_list(list, 1);
}

static void test_similarity(void)
{
	CHECK(ehv_similarity("Gr\x81\x9e" "e", "GRUESSE", 3) == 0);   /* umlauts spelled out */
	CHECK(ehv_similarity("appl_", "appl_init", 2) == 1);          /* prefix */
	CHECK(ehv_similarity("Detial", "Detail", 2) == 1);            /* transposition */
	CHECK(ehv_similarity("Secnd", "Second", 2) == 1);             /* deletion */
	CHECK(ehv_similarity("form do", "form_do", 1) == 0);          /* blanks/underscores */
	CHECK(ehv_similarity("abc", "xyz", 1) == 2);                  /* not similar */
}

static void test_default_roots(void)
{
	inf_file *inf = inf_parse("PATHS=C:\\GUIDES\\;c:\\guides;D:\\DOC\n"
		"HYPFOLD=C:\\GUIDES\nREF=E:\\REFS\\ALL.REF\n", NULL);
	char **r;
	unsigned int n = ehv_default_roots(inf, NULL, &r);

	CHECK(n == 5);
	if (n == 5)
	{
		CHECK(strcmp(r[0], "C:\\GUIDES\\") == 0);
		CHECK(strcmp(r[1], "D:\\DOC") == 0);
		CHECK(strcmp(r[2], "E:\\REFS") == 0);
		CHECK(strcmp(r[3], "*:\\GUIDES") == 0);
		CHECK(strcmp(r[4], "*:\\GEMSYS\\GUIDES") == 0);
	}
	inf_free_list(r, n);
	/* EHypView's own PATHS (EHYPVIEW.INF) first, the same folder once */
	n = ehv_default_roots(inf, "F:\\HELP\\; d:\\doc\\", &r);
	CHECK(n == 6);
	if (n == 6)
	{
		CHECK(strcmp(r[0], "F:\\HELP\\") == 0);
		CHECK(strcmp(r[1], "d:\\doc\\") == 0);
		CHECK(strcmp(r[2], "C:\\GUIDES\\") == 0);
		CHECK(strcmp(r[3], "E:\\REFS") == 0);
	}
	inf_free_list(r, n);
	inf_free(inf);
	n = ehv_default_roots(NULL, NULL, &r);
	CHECK(n == 2);
	inf_free_list(r, n);
	n = ehv_default_roots(NULL, "", &r);
	CHECK(n == 2);
	inf_free_list(r, n);
}

static void test_scan(const ehv_index *idx, const ehv_scan_status *st)
{
	long b1 = guide_named(idx, "/r1/BASIC.HYP");
	long b2 = guide_named(idx, "/r2/basic.hyp");
	long other = guide_named(idx, "/deepest/Other.Hyp");
	long img = guide_named(idx, "/Long Folder Name/images.hyp");
	long broken = guide_named(idx, "/r2/broken.hyp");
	unsigned long hits[8], n;
	const ehv_sym *s;
	ehv_hits h;
	unsigned long i;
	int ghost = 0;

	CHECK(idx->nguides == 5);
	CHECK(b1 >= 0 && b2 >= 0 && other >= 0 && img >= 0 && broken >= 0);
	if (b1 < 0 || b2 < 0 || other < 0 || img < 0 || broken < 0)
		return;
	CHECK(st->opened == 4 && st->failed == 1 && st->reused == 0);
	/* r1 (5 folders), r2 (2), and the tree itself via r2/loop; the loop
	   back into r1/r2 and the repeated root r1/sub are not read again */
	CHECK(st->dirs == 8);

	/* roots: r1 is root 0, r2 root 1; r1/sub was reached from r1 */
	CHECK(idx->guides[b1].root == 0 && idx->guides[other].root == 0 && idx->guides[b2].root == 1);
	CHECK(idx->guides[broken].error != HYP_OK);
	CHECK(idx->guides[b1].error == HYP_OK);

	/* metadata */
	CHECK(idx->guides[other].database && strcmp(idx->guides[other].database, "EHypView other test") == 0);
	CHECK(idx->guides[other].subject && strcmp(idx->guides[other].subject, "Test/EHypView") == 0);
	CHECK(idx->guides[other].default_node && strcmp(idx->guides[other].default_node, "Start") == 0);
	CHECK(strcmp(idx->guides[other].key, "Other") == 0);

	/* REF beside the file, matched ignoring case */
	CHECK(ends_with(idx->guides[b1].ref_path, "/r1/Basic.Ref"));
	CHECK(idx->guides[other].ref_path == NULL);
	s = sym_of(idx, b1, "Zweite", EHV_SYM_ALIAS);
	CHECK(s && strcmp(s->node, "Second") == 0 && s->from_ref == 1);
	s = sym_of(idx, b1, "secondkw", EHV_SYM_LABEL);
	CHECK(s && strcmp(s->node, "Second") == 0);
	CHECK(sym_of(idx, b1, "Pop", EHV_SYM_POPUP) != NULL);
	CHECK(sym_of(idx, b1, "Main", EHV_SYM_NODE) != NULL);

	/* ALL.REF fills in guides without their own REF */
	s = sym_of(idx, other, "Anfang", EHV_SYM_ALIAS);
	CHECK(s && strcmp(s->node, "Start") == 0 && s->from_ref == 2);
	s = sym_of(idx, b2, "Zweite", EHV_SYM_ALIAS);
	CHECK(s && s->from_ref == 2);
	/* ... but never replaces the guide's own REF data */
	CHECK(names_of(idx, b1, 2) == 0);
	CHECK(names_of(idx, b1, -1) == idx->guides[b1].nsyms && idx->guides[b1].nsyms > 0);
	for (i = 0; i < idx->norphans; i++)
		if (strcmp(idx->orphans[i], "ghost.hyp") == 0)
			ghost = 1;
	CHECK(idx->norphans == 1 && ghost);

	/* lookups */
	n = ehv_index_find_guides(idx, "BASIC.HYP", hits, 8);
	CHECK(n == 2 && hits[0] == (unsigned long) b1 && hits[1] == (unsigned long) b2);
	CHECK(ehv_index_find_guides(idx, "basic", hits, 8) == 2);
	n = ehv_index_find_guides(idx, "C:\\GUIDES\\OTHER.HYP", hits, 8);
	CHECK(n == 1 && hits[0] == (unsigned long) other);
	CHECK(ehv_index_find_guides(idx, "nosuch.hyp", hits, 8) == 0);
	CHECK(ehv_index_find_path(idx, idx->guides[img].path) == img);

	ehv_hits_init(&h);
	n = ehv_index_find_symbol(idx, "Second", -1, &h, 8);
	CHECK(n == 2 && h.n == 2 && h.s[0].guide == (unsigned long) b1);
	ehv_hits_free(&h);
	n = ehv_index_find_symbol(idx, "second", -1, &h, 8);
	CHECK(n == 2 && h.n == 2 && strcmp(h.s[0].name, "Second") == 0);
	ehv_hits_free(&h);
	n = ehv_index_find_symbol(idx, "Zweite", -1, &h, 8);
	CHECK(n == 2 && h.s[0].from_ref == 1);
	ehv_hits_free(&h);
	n = ehv_index_find_symbol(idx, "Detail", other, &h, 8);
	CHECK(n == 1 && h.n == 1);
	ehv_hits_free(&h);
	CHECK(ehv_index_find_symbol(idx, "Detail", b1, NULL, 8) == 0);
	CHECK(ehv_index_find_symbol(idx, "Second", -1, NULL, 0) == 2);

	n = ehv_index_suggest_guides(idx, "othr", hits, 8);
	CHECK(n >= 1 && hits[0] == (unsigned long) other);
}

static void test_resolve(const ehv_index *idx)
{
	long b1 = guide_named(idx, "/r1/BASIC.HYP");
	long b2 = guide_named(idx, "/r2/basic.hyp");
	long other = guide_named(idx, "/deepest/Other.Hyp");
	ehv_context ctx;
	ehv_target t;
	char req[1200];
	hyp_doc *doc;

	if (b1 < 0 || b2 < 0 || other < 0)
		return;
	memset(&ctx, 0, sizeof(ctx));
	ctx.idx = idx;
	ctx.current = idx->guides[b1].path;

	/* external links */
	CHECK(ehv_resolve_xref(&ctx, "other.hyp/Start", &t) == EHV_FOUND);
	CHECK(t.guide == other && ends_with(t.path, "/deepest/Other.Hyp") && t.node && strcmp(t.node, "Start") == 0);
	ehv_target_free(&t);

	CHECK(ehv_resolve_xref(&ctx, "OTHER.HYP/start", &t) == EHV_FOUND);
	CHECK(t.node && strcmp(t.node, "Start") == 0);
	ehv_target_free(&t);

	CHECK(ehv_resolve_xref(&ctx, "other.hyp/Main", &t) == EHV_FOUND && t.node == NULL);
	ehv_target_free(&t);

	CHECK(ehv_resolve_xref(&ctx, "other.hyp/Anfang", &t) == EHV_FOUND);
	CHECK(t.node && strcmp(t.node, "Start") == 0);
	ehv_target_free(&t);

	CHECK(ehv_resolve_xref(&ctx, "C:\\GUIDES\\OTHER.HYP/Detail", &t) == EHV_FOUND);
	CHECK(t.guide == other && t.node && strcmp(t.node, "Detail") == 0);
	ehv_target_free(&t);

	CHECK(ehv_resolve_xref(&ctx, "other.hyp/Strat", &t) == EHV_NO_NODE);
	CHECK(t.guide == other && t.path != NULL && t.node == NULL);
	CHECK(has_suggestion_sym(&t, "Start"));
	ehv_target_free(&t);

	CHECK(ehv_resolve_xref(&ctx, "othr.hyp/Start", &t) == EHV_NO_FILE);
	CHECK(t.path == NULL && t.nsuggest >= 1 && t.suggest[0] == (unsigned long) other);
	ehv_target_free(&t);

	CHECK(ehv_resolve_xref(&ctx, "nosuch.hyp/x", &t) == EHV_NO_FILE && t.nsuggest == 0);
	ehv_target_free(&t);

	CHECK(ehv_resolve_xref(&ctx, "other.hyp", &t) == EHV_FOUND && t.guide == other && t.node == NULL);
	ehv_target_free(&t);

	/* a copy next to the open file wins over the index order */
	ctx.current = idx->guides[b2].path;
	CHECK(ehv_resolve_xref(&ctx, "basic.hyp/Second", &t) == EHV_FOUND);
	CHECK(t.guide == b2);
	ehv_target_free(&t);

	/* bare names: the open hypertext first */
	doc = hyp_open(idx->guides[b2].path, NULL);
	ctx.doc = doc;
	CHECK(ehv_resolve_name(&ctx, "Second", &t) == EHV_FOUND && t.guide == b2 && t.nalt == 0);
	ehv_target_free(&t);
	CHECK(ehv_resolve_xref(&ctx, "Help", &t) == EHV_FOUND && t.guide == b2);
	ehv_target_free(&t);
	hyp_close(doc);
	ctx.doc = NULL;

	ctx.current = NULL;
	CHECK(ehv_resolve_name(&ctx, "Second", &t) == EHV_FOUND);
	CHECK(t.guide == b1 && t.nalt == 1 && t.alt[0] == (unsigned long) b2);
	ehv_target_free(&t);

	CHECK(ehv_resolve_name(&ctx, "Anfang", &t) == EHV_FOUND);
	CHECK(t.guide == other && t.node && strcmp(t.node, "Start") == 0);
	ehv_target_free(&t);

	CHECK(ehv_resolve_name(&ctx, "secondkw", &t) == EHV_FOUND);
	CHECK(t.node && strcmp(t.node, "Second") == 0 && t.line == 0);
	ehv_target_free(&t);

	CHECK(ehv_resolve_name(&ctx, "Secnd", &t) == EHV_NOT_FOUND && t.path == NULL);
	CHECK(has_suggestion_sym(&t, "Second"));
	ehv_target_free(&t);

	CHECK(ehv_resolve_name(&ctx, "Detial", &t) == EHV_NOT_FOUND);
	CHECK(has_suggestion_sym(&t, "Detail"));
	ehv_target_free(&t);

	/* help requests */
	CHECK(ehv_resolve_request(&ctx, "'*:\\OTHER.HYP'", &t) == EHV_FOUND);
	CHECK(t.guide == other && t.node == NULL);
	ehv_target_free(&t);

	CHECK(ehv_resolve_request(&ctx, "*:\\OTHER.HYP Detail", &t) == EHV_FOUND);
	CHECK(t.node && strcmp(t.node, "Detail") == 0);
	ehv_target_free(&t);

	snprintf(req, sizeof(req), "%s Second", tp("r1/BASIC.HYP"));
	CHECK(ehv_resolve_request(&ctx, req, &t) == EHV_FOUND);
	CHECK(t.path && strcmp(t.path, tp("r1/BASIC.HYP")) == 0 && t.node && strcmp(t.node, "Second") == 0);
	ehv_target_free(&t);

	snprintf(req, sizeof(req), "'%s'", tp("r1/Long Folder Name/images.hyp"));
	CHECK(ehv_resolve_request(&ctx, req, &t) == EHV_FOUND && ends_with(t.path, "images.hyp"));
	ehv_target_free(&t);

	snprintf(req, sizeof(req), "%s Zweite", tp("r1/Basic.Ref"));
	CHECK(ehv_resolve_request(&ctx, req, &t) == EHV_FOUND);
	CHECK(t.guide == b1 && t.node && strcmp(t.node, "Second") == 0);
	ehv_target_free(&t);

	CHECK(ehv_resolve_request(&ctx, "Anfang", &t) == EHV_FOUND && t.guide == other);
	ehv_target_free(&t);
	CHECK(ehv_resolve_request(&ctx, "  ", &t) == EHV_NOT_FOUND);
	ehv_target_free(&t);

	/* without an index: the folder of the open file and its REF */
	ctx.idx = NULL;
	ctx.current = tp("r1/BASIC.HYP");
	CHECK(ehv_resolve_xref(&ctx, "basic.hyp/zweite", &t) == EHV_FOUND);
	CHECK(t.path && strcmp(t.path, tp("r1/BASIC.HYP")) == 0 && t.node && strcmp(t.node, "Second") == 0);
	ehv_target_free(&t);
	CHECK(ehv_resolve_xref(&ctx, "other.hyp/Start", &t) == EHV_NO_FILE);
	ehv_target_free(&t);
}

static void test_split_args(void)
{
	char **a;
	unsigned int n = ehv_split_args(" 'it''s here'  two \"x y\"", &a);

	CHECK(n == 3);
	if (n == 3)
	{
		CHECK(strcmp(a[0], "it's here") == 0);
		CHECK(strcmp(a[1], "two") == 0);
		CHECK(strcmp(a[2], "x y") == 0);
	}
	inf_free_list(a, n);
	CHECK(ehv_split_args("", &a) == 0 && a == NULL);
}

static unsigned long own_syms(const ehv_index *idx, long g)
{
	return names_of(idx, g, 0) + names_of(idx, g, 1);
}

/* the same names, in the same order */
static int same_names(const ehv_index *a, const ehv_index *b)
{
	ehv_hits x, y;
	unsigned long k;
	int same;

	all_names(a, &x);
	all_names(b, &y);
	same = x.n == y.n && x.n == a->nsyms && y.n == b->nsyms;
	for (k = 0; same && k < x.n; k++)
		same = strcmp(x.s[k].name, y.s[k].name) == 0 && strcmp(x.s[k].node, y.s[k].node) == 0 &&
		       x.s[k].guide == y.s[k].guide && x.s[k].kind == y.s[k].kind &&
		       x.s[k].from_ref == y.s[k].from_ref && x.s[k].line == y.s[k].line;
	ehv_hits_free(&x);
	ehv_hits_free(&y);
	return same;
}

static void test_cache(ehv_index *idx, const ref_db *allref)
{
	const char *cache = tp("cache.idx");
	ehv_index *loaded, *again;
	ehv_scan_status st;
	unsigned long g;
	FILE *fp;
	long size;
	char *data;
	int k;

	CHECK(ehv_index_save(idx, cache) == 0);
	loaded = ehv_index_load(cache);
	CHECK(loaded != NULL);
	if (loaded == NULL)
		return;
	CHECK(loaded->nguides == idx->nguides);
	for (g = 0; g < idx->nguides; g++)
	{
		long lg = ehv_index_find_path(loaded, idx->guides[g].path);

		CHECK(lg >= 0);
		if (lg < 0)
			continue;
		CHECK(own_syms(loaded, lg) == own_syms(idx, (long) g));
		CHECK(loaded->guides[lg].size == idx->guides[g].size);
		CHECK(loaded->guides[lg].error == idx->guides[g].error);
		CHECK((loaded->guides[lg].database == NULL) == (idx->guides[g].database == NULL));
	}
	{
		CHECK(ehv_index_find_symbol(loaded, "Detail", -1, NULL, 1) == 1);
		/* ALL.REF's names are kept too, with the tables */
		CHECK(ehv_index_find_symbol(loaded, "Zweite", -1, NULL, 1) ==
		      ehv_index_find_symbol(idx, "Zweite", -1, NULL, 1));
		CHECK(loaded->nsyms == idx->nsyms && loaded->norphans == idx->norphans);
		CHECK(memcmp(loaded->by_key, idx->by_key, idx->nguides * sizeof(unsigned long)) == 0);
		CHECK(memcmp(loaded->by_path, idx->by_path, idx->nguides * sizeof(unsigned long)) == 0);
		CHECK(same_names(loaded, idx));
		/* the names stay in the file: the saved index reads them there too */
		CHECK(loaded->mem == NULL && loaded->file != NULL && loaded->nblocks > 0);
		CHECK(idx->mem == NULL && idx->file != NULL && strcmp(idx->file, cache) == 0);
	}

	/* rescan with the cache: nothing is read again */
	again = scan_tree(loaded, allref, &st);
	CHECK(again != NULL && again->nguides == 5);
	CHECK(st.opened == 0 && st.reused == 5 && st.failed == 1);
	CHECK(again && again->norphans == 1);
	ehv_index_free(again);

	/* change one file: only that one is read */
	copy_file("other_raw.hyp", tp("r1/sub/deeper/deepest/Other.Hyp"));
	again = scan_tree(loaded, allref, &st);
	CHECK(st.opened == 1 && st.reused == 4);
	if (again != NULL)
	{
		long o = guide_named(again, "/deepest/Other.Hyp");

		CHECK(o >= 0 && sym_of(again, o, "Anfang", EHV_SYM_ALIAS) != NULL);
	}
	ehv_index_free(again);
	ehv_index_free(loaded);

	/* damaged caches are refused, never crash */
	fp = fopen(cache, "rb");
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	rewind(fp);
	data = malloc((size_t) size);
	if (fread(data, 1, (size_t) size, fp) != (size_t) size)
		size = 0;
	fclose(fp);
	write_file(cache, data, (size_t) size / 2);
	CHECK(ehv_index_load(cache) == NULL);
	write_file(cache, "EHYPVIEW-INDEX 99\nEND\t0\t0\n", 27);
	CHECK(ehv_index_load(cache) == NULL);
	CHECK(ehv_index_load(tp("no-such-cache")) == NULL);
	for (k = 0; k < 300 && size > 0; k++)
	{
		unsigned int seed = (unsigned int) k * 2654435761u;
		long pos = (long) (seed % (unsigned long) size);
		char save = data[pos];

		data[pos] = (char) (k % 3 == 0 ? '\t' : k % 3 == 1 ? '\n' : (char) (save ^ 0x55));
		write_file(cache, data, (size_t) size);
		if ((again = ehv_index_load(cache)) != NULL)
		{
			/* its names read, whatever they are now */
			ehv_hits h;

			all_names(again, &h);
			ehv_hits_free(&h);
			ehv_index_find_symbol(again, "Second", -1, NULL, 8);
			ehv_index_suggest(again, "Secnd", -1, NULL, 8);
		}
		ehv_index_free(again);
		data[pos] = save;
	}
	free(data);
	checks++;
}

/* ---------------------------------------------------------------- */
/* the library's index in short steps (libjob.h)                     */

/* the tables and the names in the order the lookups expect */
static int tables_sorted(const ehv_index *idx)
{
	ehv_hits h;
	unsigned long k, n = 0;
	int ok = 1;

	all_names(idx, &h);
	if (h.n != idx->nsyms)
		ok = 0;
	for (k = 0; ok && k + 1 < h.n; k++)
	{
		const ehv_sym *a = &h.s[k], *b = &h.s[k + 1];
		int c = atari_casecmp(a->name, b->name);

		if (c > 0)
			ok = 0;
		if (c == 0 && a->kind == b->kind && idx->guides[a->guide].root == idx->guides[b->guide].root &&
		    a->guide > b->guide)
			ok = 0;                        /* not by guide */
	}
	for (k = 0; k < idx->nguides; k++)
		n += idx->guides[k].nsyms;
	if (n != idx->nsyms)
		ok = 0;
	ehv_hits_free(&h);
	for (k = 0; k + 1 < idx->nguides; k++)
		if (strcmp(idx->guides[idx->by_path[k]].path, idx->guides[idx->by_path[k + 1]].path) > 0 ||
		    atari_casecmp(idx->guides[idx->by_key[k]].key, idx->guides[idx->by_key[k + 1]].key) > 0)
			ok = 0;
	return ok;
}

static int same_index(const ehv_index *a, const ehv_index *b)
{
	unsigned long k;

	if (a->nguides != b->nguides || a->nsyms != b->nsyms || a->norphans != b->norphans)
		return 0;
	for (k = 0; k < a->nguides; k++)
		if (strcmp(a->guides[k].path, b->guides[k].path) != 0 || a->by_key[k] != b->by_key[k] ||
		    a->by_path[k] != b->by_path[k] || a->guides[k].nsyms != b->guides[k].nsyms)
			return 0;
	return same_names(a, b);
}

/* the lowest file number free: the same before and after a step when
   the step left no file open (guidelines 2.2: an accessory's files
   belong to the program in front, whose end closes them) */
static int free_fd(void)
{
	int fd = open("/dev/null", O_RDONLY);

	if (fd >= 0)
		close(fd);
	return fd;
}

static ehv_index *run_job_counted(const char *cache, const char *allref, int *nindex, int *unchanged, int *steps);

/* the last job's opens and looks (plat_posix.c's counts; the AERA
   guidelines, 6.5) */
static unsigned long job_opens, job_looks;

/* the job to its end; *nindex: indices it handed out; the last one */
static ehv_index *run_job(const char *cache, const char *allref, int *nindex, int *unchanged, int *steps)
{
	unsigned long o0 = pf_opens, l0 = pf_lookups;
	ehv_index *idx = run_job_counted(cache, allref, nindex, unchanged, steps);

	job_opens = pf_opens - o0;
	job_looks = pf_lookups - l0;
	return idx;
}

static ehv_index *run_job_counted(const char *cache, const char *allref, int *nindex, int *unchanged, int *steps)
{
	ehv_libjob *j = ehv_libjob_begin(cache, (const char *const *) roots, 4, allref);
	ehv_index *idx = NULL;
	int r, fd = free_fd(), kept = 0;

	*nindex = *steps = 0;
	CHECK(j != NULL);
	if (j == NULL)
		return NULL;
	while ((r = ehv_libjob_step(j)) >= 0 && r != EHV_JOB_DONE)
	{
		++*steps;
		if (free_fd() != fd)
			kept++;
		if (r == EHV_JOB_INDEX)
		{
			ehv_index_free(idx);
			idx = ehv_libjob_take(j);
			++*nindex;
		}
	}
	CHECK(r == EHV_JOB_DONE);
	CHECK(kept == 0);                  /* no file open between two steps */
	*unchanged = ehv_libjob_unchanged(j);
	ehv_libjob_free(j);
	return idx;
}

/* the index file in pieces: written from a small buffer, its head read
   a small piece a step, each piece one open (the AERA guidelines, 6.5);
   the names sorted a batch at a time, through NAME.RUN */
static ehv_index *pieces_index(const char *build_to)
{
	ehv_index *idx = ehv_index_new();
	ehv_guide g;
	unsigned long i;
	char name[32], node[32];

	memset(&g, 0, sizeof(g));
	g.path = "C:\\GUIDES\\PIECES.HYP";
	g.size = 1234;
	CHECK(idx != NULL && ehv_index_add_guide(idx, &g) == 0);
	g.path = "C:\\GUIDES\\MORE.HYP";
	CHECK(idx != NULL && ehv_index_add_guide(idx, &g) == 1);
	if (idx != NULL && build_to != NULL)
		CHECK(ehv_index_build_to(idx, build_to) == 0);
	for (i = 0; idx != NULL && i < 3000; i++)
	{
		/* in an order that isn't the names' */
		unsigned long k = (i * 7919UL) % 3000;

		sprintf(name, "page number %05lu", k);
		sprintf(node, "Node %lu", k / 10);
		CHECK(ehv_index_add_sym(idx, k % 2, k % 3 == 0 ? EHV_SYM_ALIAS : EHV_SYM_NODE, 0, name,
					k % 3 == 0 ? node : NULL, 0) == 0);
		while (ehv_index_build_busy(idx))
			CHECK(ehv_index_build_step(idx) >= 0);
	}
	CHECK(idx != NULL && ehv_index_finish(idx) == 0);
	return idx;
}

static void test_pieces(void)
{
	ehv_index *idx, *back = NULL, *batched;
	ehv_loader *l;
	unsigned long o0, size, head;
	pf_info st;
	ehv_hits h;
	int r, more;

	idx = pieces_index(NULL);
	if (idx == NULL)
		return;
	CHECK(idx->mem != NULL && idx->file == NULL && idx->nsyms == 3000 && tables_sorted(idx));
	CHECK(idx->guides[0].nsyms == 1500 && idx->guides[1].nsyms == 1500);
	ehv_hits_init(&h);
	CHECK(ehv_index_find_symbol(idx, "PAGE NUMBER 01233", -1, &h, 4) == 1 && h.n == 1 &&
	      h.s[0].guide == 1 && h.s[0].kind == EHV_SYM_ALIAS && strcmp(h.s[0].node, "Node 123") == 0);
	ehv_hits_free(&h);

	/* sorted a batch at a time, the batches in memory, or in NAME.RUN
	   and the blocks in NAME.ID$: the same names */
	ehv_index_batch(4096);
	batched = pieces_index(NULL);
	CHECK(batched != NULL && same_names(batched, idx) && tables_sorted(batched));
	ehv_index_free(batched);
	batched = pieces_index(tp("batch.idx"));
	CHECK(batched != NULL && batched->file != NULL && batched->mem == NULL && same_names(batched, idx));
	CHECK(pf_stat(tp("batch.RUN"), &st) != 0);          /* gone once merged */
	CHECK(pf_stat(tp("batch.id$"), &st) == 0 && st.size == batched->nblocks * EHV_BLOCK);
	CHECK(batched != NULL && ehv_index_save(batched, tp("batch.idx")) == 0);
	CHECK(pf_stat(tp("batch.id$"), &st) != 0 && pf_stat(tp("batch.idx"), &st) == 0);
	CHECK(batched != NULL && strcmp(batched->file, tp("batch.idx")) == 0 && same_names(batched, idx));
	ehv_index_free(batched);
	back = ehv_index_load(tp("batch.idx"));
	CHECK(back != NULL && same_index(back, idx));
	ehv_index_free(back);
	back = NULL;
	remove(tp("batch.idx"));
	ehv_index_batch(32768);

	ehv_index_pieces(4096, 16384);
	o0 = pf_opens;
	{
		/* in steps, as the library job does: a buffer at a time, each
		   one open at the most */
		ehv_saver *v = ehv_index_save_begin(idx, tp("pieces.idx"));

		CHECK(v != NULL);
		more = 0;
		while (v != NULL)
		{
			unsigned long o1 = pf_opens;

			r = ehv_index_save_some(v, 1);
			more += pf_opens - o1 > 1;
			if (r != 0)
				break;
		}
		CHECK(v != NULL && r == 1 && more == 0);
		ehv_index_save_abort(v);
	}
	CHECK(pf_stat(tp("pieces.idx"), &st) == 0 && st.size > idx->nblocks * EHV_BLOCK);
	CHECK(pf_opens - o0 >= 2);
	size = st.size;
	head = size - idx->nblocks * EHV_BLOCK - 32;
	CHECK(idx->mem == NULL && idx->file != NULL && same_names(idx, idx));

	o0 = pf_opens;
	l = ehv_index_load_begin(tp("pieces.idx"));
	CHECK(l != NULL);
	while (l != NULL && (r = ehv_index_load_some(l, 1)) == 0)
		;
	CHECK(l != NULL && r == 1);
	if (l != NULL && r == 1)
		back = ehv_index_load_take(l);
	else
		ehv_index_load_abort(l);
	CHECK(pf_opens - o0 == (head + 4095) / 4096);    /* the head only, a piece a step, each one open */
	CHECK(back != NULL && same_index(back, idx));

	/* the whole head in one read and one write when they fit */
	ehv_index_pieces(1024L * 1024, 1024L * 1024);
	o0 = pf_opens;
	CHECK(ehv_index_save(idx, tp("pieces.idx")) == 0);
	CHECK(pf_opens - o0 == 2);                      /* the blocks read, the file written */
	ehv_index_free(back);
	o0 = pf_opens;
	back = ehv_index_load(tp("pieces.idx"));
	CHECK(pf_opens - o0 == 1);
	CHECK(back != NULL && same_index(back, idx));

	/* a name looked up: one open, a block or two read */
	o0 = pf_opens;
	CHECK(back != NULL && ehv_index_find_symbol(back, "page number 02999", 0, NULL, 1) == 0 &&
	      ehv_index_find_symbol(back, "page number 02999", 1, NULL, 1) == 1);
	CHECK(pf_opens - o0 == 2);

	/* the hypertexts taken from it, one read again */
	{
		ehv_index *re = ehv_index_new();
		ehv_guide g;

		memset(&g, 0, sizeof(g));
		g.path = "C:\\GUIDES\\NEW.HYP";
		CHECK(re != NULL && back != NULL && ehv_index_add_guide(re, &back->guides[1]) == 0 &&
		      ehv_index_take_names(re, back, 1, 0) == 0 && ehv_index_add_guide(re, &g) == 1 &&
		      ehv_index_add_sym(re, 1, EHV_SYM_NODE, 0, "page number 00001", NULL, 0) == 0 &&
		      ehv_index_finish(re) == 0);
		CHECK(re != NULL && re->nsyms == 1501 && re->guides[0].nsyms == 1500 && tables_sorted(re));
		ehv_hits_init(&h);
		CHECK(re != NULL && ehv_index_find_symbol(re, "page number 00001", -1, &h, 4) == 2 &&
		      h.n == 2 && h.s[0].guide == 0 && h.s[1].guide == 1);
		ehv_hits_free(&h);
		ehv_index_free(re);
	}
	ehv_index_free(back);
	ehv_index_free(idx);
	ehv_index_pieces(32768, 32768);
	remove(tp("pieces.idx"));
}

static void test_libjob(const ref_db *db)
{
	char cache[1024], allref[1024], allref2[1024];
	ehv_index *want, *got;
	ehv_scan_status st;
	pf_info before, after;
	unsigned long looks;
	int n, same, steps;
	char *text;
	unsigned long len;

	snprintf(cache, sizeof(cache), "%s", tp("job.idx"));      /* tp(): one buffer */
	snprintf(allref, sizeof(allref), "%s", tp("all.ref"));
	snprintf(allref2, sizeof(allref2), "%s", tp("all2.ref"));
	remove(cache);
	want = scan_tree(NULL, db, &st);
	CHECK(want != NULL && tables_sorted(want));

	/* no cache yet: scanned, sorted, saved; one index handed out */
	got = run_job(cache, allref, &n, &same, &steps);
	CHECK(got != NULL && n == 1 && !same && steps > 10);
	CHECK(got != NULL && want != NULL && same_index(got, want));
	CHECK(pf_stat(cache, &before) == 0);
	ehv_index_free(got);
	/* every folder looked at once; each hypertext (the damaged ones too)
	   and .REF opened once, ALL.REF, and the index (looked for, its
	   names written as they are sorted, here in one go, then its head):
	   the AERA guidelines, 6.5 */
	looks = job_looks;
	{
		unsigned long g, refs = 0;

		for (g = 0; want != NULL && g < want->nguides; g++)
			refs += want->guides[g].ref_path != NULL;
		CHECK(job_opens == st.opened + st.failed + refs + 4);
	}

	/* again: the index read is handed out, nothing changed, nothing
	   written; one open (the index), the folders listed and nothing
	   looked up one by one */
	sleep(1);
	got = run_job(cache, allref, &n, &same, &steps);
	CHECK(got != NULL && n == 1 && same);
	CHECK(job_opens == 1 && job_looks == looks);
	CHECK(got != NULL && want != NULL && same_index(got, want) && tables_sorted(got));
	CHECK(pf_stat(cache, &after) == 0 && after.mtime == before.mtime);
	ehv_index_free(got);

	/* ALL.REF changed: built again (its names merged afresh) */
	{
		char *ref = pf_load(allref, &len);

		CHECK(ref != NULL);
		write_file(allref2, ref, len);
		free(ref);
	}
	got = run_job(cache, allref2, &n, &same, &steps);
	CHECK(got != NULL && n == 2 && !same && same_index(got, want));
	CHECK(job_looks == looks);              /* built at once, no checking pass first */
	ehv_index_free(got);
	got = run_job(cache, allref2, &n, &same, &steps);
	CHECK(got != NULL && n == 1 && same);
	ehv_index_free(got);

	/* no ALL.REF any more: its names go */
	got = run_job(cache, NULL, &n, &same, &steps);
	CHECK(got != NULL && n == 2 && !same && got->norphans == 0 && tables_sorted(got));
	ehv_index_free(got);

	/* a hypertext changed: read again */
	got = run_job(cache, allref, &n, &same, &steps);
	CHECK(got != NULL && n == 2 && !same && same_index(got, want));
	ehv_index_free(got);
	copy_file("other.hyp", tp("r1/sub/deeper/deepest/Other.Hyp"));
	got = run_job(cache, allref, &n, &same, &steps);
	CHECK(got != NULL && n == 2 && !same && tables_sorted(got));
	/* the folders read once (the checking pass's, taken by the building
	   one); the index's head, the changed hypertext, ALL.REF, the old
	   index's names (one read), the new index's names and its head */
	CHECK(job_looks == looks && job_opens <= 6);
	ehv_index_free(got);

	/* one gone, then back: built again each time */
	{
		char gone[1024];

		snprintf(gone, sizeof(gone), "%s", tp("r2/basic.hyp"));
		remove(gone);
		got = run_job(cache, allref, &n, &same, &steps);
		CHECK(got != NULL && n == 2 && !same && got->nguides == 4 && tables_sorted(got));
		ehv_index_free(got);
		copy_file("basic.hyp", gone);
		got = run_job(cache, allref, &n, &same, &steps);
		CHECK(got != NULL && n == 2 && !same && got->nguides == 5 && tables_sorted(got));
		ehv_index_free(got);
		got = run_job(cache, allref, &n, &same, &steps);
		CHECK(got != NULL && n == 1 && same);
		ehv_index_free(got);
	}

	/* a version 2 file (EHypView 1.15, all text): not read, built again */
	write_file(cache, "EHYPVIEW-INDEX 2\nC\t0\t0\nEND\t0\t0\n", 31);
	CHECK(ehv_index_load(cache) == NULL);
	got = run_job(cache, allref, &n, &same, &steps);
	CHECK(got != NULL && n == 1 && !same && same_index(got, want) && tables_sorted(got));
	ehv_index_free(got);
	text = pf_load(cache, &len);
	CHECK(text != NULL && len > 32 && strncmp(text + len - 32, "\nEHV ", 5) == 0);
	free(text);
	got = run_job(cache, allref, &n, &same, &steps);
	CHECK(got != NULL && n == 1 && same);
	ehv_index_free(got);
	ehv_index_free(want);
}

int main(int argc, char **argv)
{
	ehv_index *idx;
	ehv_scan_status st;
	ref_db *allref;

	fixdir = argc > 1 ? argv[1] : "tests/fixtures";
	make_tree();
	roots[0] = strdup(tp("r1"));
	roots[1] = strdup(tp("r2"));
	roots[2] = strdup(tp("r1/sub"));        /* already inside r1 */
	roots[3] = strdup(tp("missing"));

	test_fsys();
	test_similarity();
	test_default_roots();
	test_split_args();

	allref = ref_open(tp("all.ref"), NULL);
	CHECK(allref != NULL && allref->nmodules == 3);
	idx = scan_tree(NULL, allref, &st);
	CHECK(idx != NULL);
	if (idx != NULL)
	{
		test_scan(idx, &st);
		test_resolve(idx);
		test_cache(idx, allref);
		test_pieces();
		test_libjob(allref);
	}
	ehv_index_free(idx);
	ref_close(allref);
	rm_tree(tree);
	free(roots[0]);
	free(roots[1]);
	free(roots[2]);
	free(roots[3]);

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
