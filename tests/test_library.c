/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView tests for the library (src/view/library.c) and the full-text
 * search (src/core/search.c).
 *
 *   test_library FIXTURE_DIR
 *
 * Builds test_library_tree/ with basic.hyp (+ basic.ref), other.hyp,
 * images.hyp and big.hyp.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "library.h"
#include "scan.h"
#include "search.h"

static int failures, checks;
static const char *fixdir;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define TREE "test_library_tree"

static void copy(const char *name)
{
	char src[1024], dst[1024], buf[4096];
	FILE *in, *out;
	size_t n;

	snprintf(src, sizeof(src), "%s/%s", fixdir, name);
	snprintf(dst, sizeof(dst), "%s/%s", TREE, name);
	in = fopen(src, "rb");
	out = fopen(dst, "wb");
	if (in == NULL || out == NULL)
	{
		printf("FAIL cannot copy %s\n", name);
		exit(1);
	}
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
		fwrite(buf, 1, n, out);
	fclose(in);
	fclose(out);
}

static void remove_tree(void)
{
	static const char *names[] = { "basic.hyp", "basic.ref", "other.hyp", "images.hyp", "big.hyp" };
	char p[256];
	unsigned int i;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
	{
		snprintf(p, sizeof(p), "%s/%s", TREE, names[i]);
		unlink(p);
	}
	rmdir(TREE);
}

static int ends_with(const char *s, const char *tail)
{
	size_t ls, lt;

	if (s == NULL)
		return 0;
	ls = strlen(s);
	lt = strlen(tail);
	return ls >= lt && strcmp(s + ls - lt, tail) == 0;
}

/* a search typed, and its slices through the names run to the end (the
   program runs them between its event calls) */
static int search(ehv_library *lib, const char *q)
{
	int r = lib_set_query(lib, q), n = 0;

	while (r == 0 && (r = lib_search_step(lib, 3)) != 1 && r >= 0)
		r = ++n > 10000 ? -1 : 0;
	return r < 0 ? -1 : 0;
}

static void row(const ehv_library *lib, long r, char *buf)
{
	lib_row_text(lib, (unsigned long) r, buf, 120);
}

static void test_all(const ehv_index *idx)
{
	ehv_library lib;
	char t[120];
	const char *path, *node;
	unsigned short line;

	lib_init(&lib, NULL);
	CHECK(lib.nrows == 1 && lib.rows[0].kind == LIB_NOTE && lib.sel == -1);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_NOTHING);
	CHECK(lib_set_index(&lib, idx) == 0);

	/* grouped by subject, titles sorted, no subject last; "Test/EHypView":
	   EHypView under Test */
	CHECK(lib.nrows == 7 && lib.nguides == 4);
	if (lib.nrows == 7)
	{
		CHECK(lib.rows[0].kind == LIB_GROUP && strcmp(lib.rows[0].text, "Test") == 0);
		CHECK(lib.rows[1].kind == LIB_GROUP && strcmp(lib.rows[1].text, "EHypView") == 0);
		CHECK(lib.rows[1].flags == LIB_SUB && lib.rows[1].ref == 3);
		row(&lib, 2, t);
		CHECK(strcmp(t, "  EHypView basic test") == 0);
		row(&lib, 3, t);
		CHECK(strcmp(t, "  EHypView image test") == 0);
		row(&lib, 4, t);
		CHECK(strcmp(t, "  EHypView other test") == 0);
		CHECK(lib.rows[5].kind == LIB_GROUP && strcmp(lib.rows[5].text, "Other hypertexts") == 0);
		row(&lib, 6, t);
		CHECK(strcmp(t, "  EHypView big test") == 0);
		lib_row_detail(&lib, 6, t, sizeof(t));
		CHECK(strcmp(t, "big") == 0);
	}
	CHECK(lib.sel == 2);
	CHECK(lib_move(&lib, 1) == 3);
	CHECK(lib_move(&lib, 10) == 6);           /* over the heading, stops at the end */
	CHECK(lib_move(&lib, -1) == 5);           /* the heading: it folds */
	CHECK(lib_move(&lib, -10) == 0);          /* not the part's heading */
	CHECK(lib_select(&lib, 1) == 0);
	CHECK(lib_select(&lib, 5) == 5);
	CHECK(lib_select(&lib, 6) == 6);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_OPEN);
	CHECK(ends_with(path, "big.hyp") && node == NULL && line == 0);
	lib_free(&lib);
}

static void test_bookmarks(const ehv_index *idx)
{
	ehv_library lib;
	ehv_bookmarks bm;
	char t[120];
	const char *path, *node;
	unsigned short line;

	bm_init(&bm);
	bm_add(&bm, "C:\\GUIDES\\BASIC.HYP", "Second", "Basic test - Second page");
	bm_add(&bm, "C:\\GUIDES\\OTHER.HYP", "Detail", "Other test - Details");

	/* with no index yet, the bookmarks already show */
	lib_init(&lib, NULL);
	CHECK(lib_set_bookmarks(&lib, &bm) == 0);
	CHECK(lib.nrows == 4 && lib.rows[0].kind == LIB_GROUP && strcmp(lib.rows[0].text, "Bookmarks") == 0);
	CHECK(lib.rows[1].kind == LIB_BOOKMARK && lib.rows[3].kind == LIB_NOTE);
	CHECK(lib.sel == 1 && lib_selected_bookmark(&lib) == 0);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_OPEN);
	CHECK(strcmp(path, "C:\\GUIDES\\BASIC.HYP") == 0 && strcmp(node, "Second") == 0 && line == 0);

	/* first in the full list, the hypertexts after them */
	CHECK(lib_set_index(&lib, idx) == 0);
	CHECK(lib.nrows == 3 + 7 && lib.nguides == 4);
	row(&lib, 1, t);
	CHECK(strcmp(t, "  Basic test - Second page") == 0);
	lib_row_detail(&lib, 2, t, sizeof(t));
	CHECK(strcmp(t, "OTHER.HYP") == 0);
	CHECK(lib.rows[3].kind == LIB_GROUP && strcmp(lib.rows[3].text, "Test") == 0);
	CHECK(lib_move(&lib, 1) == 2 && lib_selected_bookmark(&lib) == 1);
	CHECK(lib_move(&lib, 1) == 3 && lib_selected_bookmark(&lib) == -1);   /* the heading */
	CHECK(lib_move(&lib, 1) == 5);

	/* typing: the ones whose title or page has the text, in any case */
	CHECK(search(&lib, "detail") == 0);
	CHECK(lib.rows[0].kind == LIB_GROUP && strcmp(lib.rows[0].text, "Bookmarks") == 0);
	CHECK(lib.rows[1].kind == LIB_BOOKMARK && lib.rows[1].ref == 1);
	CHECK(lib.nrows > 2 && lib.rows[2].kind == LIB_GROUP && strcmp(lib.rows[2].text, "Pages") == 0);
	CHECK(search(&lib, "SECOND PAGE") == 0);
	CHECK(lib.rows[0].kind == LIB_GROUP && lib.rows[1].kind == LIB_BOOKMARK && lib.rows[1].ref == 0);
	CHECK(search(&lib, "zweite") == 0);              /* none match: no heading */
	CHECK(lib.rows[0].kind != LIB_BOOKMARK && strcmp(lib.rows[0].text ? lib.rows[0].text : "", "Bookmarks") != 0);

	/* one removed: the list follows */
	CHECK(search(&lib, "") == 0);
	bm_remove(&bm, 0);
	CHECK(lib_set_bookmarks(&lib, &bm) == 0 && lib.nrows == 2 + 7);
	row(&lib, 1, t);
	CHECK(strcmp(t, "  Other test - Details") == 0);
	/* and all of them: no heading left */
	bm_remove(&bm, 0);
	CHECK(lib_set_bookmarks(&lib, &bm) == 0 && lib.nrows == 7);
	CHECK(lib_selected_bookmark(&lib) == -1);
	CHECK(lib_set_bookmarks(&lib, NULL) == 0 && lib.nrows == 7);
	lib_free(&lib);
	bm_free(&bm);
}

static long pane_of(const ehv_library *lib, const char *name)
{
	unsigned long i;

	for (i = 0; i < lib->npanes; i++)
		if (strcmp(lib->panes[i].text, name) == 0)
			return (long) i;
	return -1;
}

static void test_layouts(const ehv_index *idx)
{
	ehv_library lib;
	ehv_bookmarks recent;
	char t[120];
	const char *path, *node;
	unsigned short line;
	long p;

	/* the Outline: headings fold, with their counts and tints */
	lib_init(&lib, idx);
	CHECK(lib.layout == LIB_OUTLINE && lib.nrows == 7 && lib.sel == 2);
	CHECK(lib.rows[0].flags == LIB_FOLDS && lib.rows[0].ref == 3 && lib.rows[0].tint == 1);
	CHECK(lib.rows[1].flags == LIB_SUB && lib.rows[1].tint == 1);
	CHECK(lib.rows[2].tint == 1 && lib.rows[5].ref == 1 && lib.rows[5].tint == 0 && lib.rows[6].tint == 0);
	CHECK(lib_fold(&lib, 1) == 0);            /* a hypertext: nothing to unfold */
	CHECK(lib_fold(&lib, 0) == 1);            /* its group */
	CHECK(lib.nrows == 3 && lib.sel == 0 && lib.rows[0].flags == (LIB_FOLDS | LIB_FOLDED));
	CHECK(lib.rows[0].ref == 3 && lib.nguides == 4);
	CHECK(lib_move(&lib, 1) == 1 && lib_move(&lib, 1) == 2);
	CHECK(search(&lib, "image") == 0 && lib.rows[0].flags == 0);   /* searching: none fold */
	CHECK(lib.rows[1].kind == LIB_GUIDE && lib.rows[1].tint == 1);
	CHECK(search(&lib, "") == 0 && lib.nrows == 3);                /* still folded */
	lib_select(&lib, 0);
	CHECK(lib_fold(&lib, -1) == 1 && lib.nrows == 7 && lib.sel == 0);
	CHECK(lib_fold(&lib, 1) == 0);

	/* its second line */
	lib_row_about(&lib, 2, t, sizeof(t));
	CHECK(strcmp(t, "EHypView project \xfa 1.0 (13.10.2026)") == 0);
	lib_row_about(&lib, 6, t, sizeof(t));
	CHECK(ends_with(t, TREE "/"));            /* no author nor version: its folder */
	CHECK(strcmp(lib_version("$VER: basic.hyp 1.0 (13.10.2026)"), "1.0 (13.10.2026)") == 0);
	CHECK(strcmp(lib_version("2.16"), "2.16") == 0 && strcmp(lib_version(NULL), "") == 0);
	CHECK(lib_row_guide(&lib, 2) == &idx->guides[lib.rows[2].ref] && lib_row_guide(&lib, 0) == NULL);

	/* the pages read last, first; the first hypertext chosen */
	bm_init(&recent);
	bm_add(&recent, "C:\\GUIDES\\BASIC.HYP", "Second", "Basic test - Second page");
	bm_add(&recent, "C:\\GUIDES\\OTHER.HYP", "Main", "Other test - Main");
	CHECK(lib_set_recent(&lib, &recent) == 0);
	CHECK(lib.nrows == 9 && lib.rows[0].kind == LIB_RECENT && lib.rows[1].kind == LIB_RECENT);
	CHECK(lib.sel == 2);                      /* the heading it was on */
	lib_free(&lib);
	lib_init(&lib, idx);
	lib_set_recent(&lib, &recent);
	CHECK(lib.sel == 4);
	lib_row_detail(&lib, 1, t, sizeof(t));
	CHECK(strcmp(t, "OTHER.HYP") == 0);
	CHECK(lib_select(&lib, 0) == 0 && lib_activate(&lib, &path, &node, &line) == LIB_OPEN);
	CHECK(strcmp(path, "C:\\GUIDES\\BASIC.HYP") == 0 && strcmp(node, "Second") == 0);
	CHECK(lib_row_guide(&lib, 0) == NULL);    /* not in the index */
	CHECK(lib_move(&lib, 1) == 1 && lib_move(&lib, 1) == 2);

	/* two panes: the subjects, All chosen at first */
	CHECK(lib_set_layout(&lib, LIB_PANES) == 0);
	CHECK(lib.npanes == 5 && lib.panes[0].kind == PANE_RECENT && lib.panes[0].count == 2);
	CHECK(lib.panes[1].kind == PANE_ALL && lib.panes[1].count == 4 && lib.pane == 1);
	CHECK(lib.panes[2].kind == PANE_SUBJECT && lib.panes[2].count == 3 && lib.panes[2].tint == 1);
	CHECK(lib.panes[2].level == 0 && strcmp(lib.panes[2].text, "Test") == 0);
	CHECK(lib.panes[3].kind == PANE_SUBJECT && lib.panes[3].level == 1 && lib.panes[3].count == 3);
	CHECK(lib.panes[3].tint == 1 && strcmp(lib.panes[3].text, "EHypView") == 0);
	CHECK(lib.panes[4].kind == PANE_OTHER && lib.panes[4].count == 1);
	CHECK(lib.nrows == 7 && lib.rows[0].kind == LIB_GROUP && lib.rows[0].flags == 0);
	p = pane_of(&lib, "Test");
	CHECK(lib_pane_select(&lib, p) == p && lib.nrows == 4 && lib.rows[0].flags == LIB_SUB && lib.sel == 1);
	CHECK(lib.nguides == 3);
	CHECK(lib_pane_move(&lib, 1) == p + 1 && lib.nrows == 3 && lib.rows[0].kind == LIB_GUIDE);
	CHECK(lib.nguides == 3);
	CHECK(lib_pane_move(&lib, 1) == p + 2 && lib.nrows == 1);
	lib_row_detail(&lib, 0, t, sizeof(t));
	CHECK(strcmp(t, "big") == 0);
	CHECK(lib_pane_move(&lib, 5) == 4 && lib_pane_move(&lib, -10) == 0);
	CHECK(lib.nrows == 2 && lib.rows[0].kind == LIB_RECENT);
	lib_pane_select(&lib, p + 1);
	CHECK(lib_set_index(&lib, idx) == 0 && lib.pane == p + 1 && lib.nrows == 3);   /* kept */
	CHECK(search(&lib, "second") == 0 && lib.rows[0].kind == LIB_GROUP && lib.npages == 2);
	CHECK(search(&lib, "") == 0 && lib.nrows == 3);
	CHECK(lib_set_recent(&lib, NULL) == 0 && lib.npanes == 4 && lib.pane == p);
	CHECK(lib_set_layout(&lib, LIB_OUTLINE) == 0 && lib.nrows == 7);
	lib_free(&lib);
	bm_free(&recent);
}

static void test_names(const ehv_index *idx)
{
	ehv_library lib;
	char t[120];
	const char *path, *node;
	unsigned short line;

	lib_init(&lib, idx);
	CHECK(search(&lib, "det") == 0);
	CHECK(lib.npages == 1 && lib.nguides == 0 && lib.nrows == 3);
	CHECK(lib.rows[0].kind == LIB_GROUP && lib.rows[1].kind == LIB_PAGE && lib.rows[2].kind == LIB_ACTION);
	CHECK(lib.sel == 1);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_OPEN);
	CHECK(ends_with(path, "other.hyp") && node && strcmp(node, "Detail") == 0);
	lib_row_detail(&lib, 1, t, sizeof(t));
	CHECK(strcmp(t, "EHypView other test") == 0);

	/* at once: the names starting with it (they follow each other in the
	   index); those containing it, or like it, come in the slices */
	CHECK(lib_set_query(&lib, "Secnd") == 0 && lib.npages == 0 && lib.scanning);
	CHECK(lib.nrows >= 2 && lib.rows[1].kind == LIB_WAIT);           /* "Searching the names..." */
	CHECK(lib_search_step(&lib, 1) >= 0);
	CHECK(search(&lib, "Secnd") == 0 && lib.npages >= 1 && !lib.scanning);
	CHECK(lib_set_query(&lib, "sec") == 0 && lib.npages >= 1 && lib.scanning);
	CHECK(lib_set_query(&lib, "") == 0 && !lib.scanning);       /* none: stopped */

	/* exact before prefix: Second, then the keyword secondkw */
	search(&lib, "second");
	CHECK(lib.npages == 2);
	row(&lib, 1, t);
	CHECK(strcmp(t, "  Second") == 0);
	row(&lib, 2, t);
	CHECK(strcmp(t, "  secondkw (in Second)") == 0);

	/* aliases lead to their page */
	search(&lib, "Zweite");
	row(&lib, 1, t);
	CHECK(strcmp(t, "  Zweite -> Second") == 0);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_OPEN && strcmp(node, "Second") == 0);

	/* titles */
	search(&lib, "image");
	CHECK(lib.npages == 0 && lib.nguides == 1);
	CHECK(lib.rows[0].kind == LIB_GROUP && strcmp(lib.rows[0].text, "Hypertexts") == 0);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_OPEN && ends_with(path, "images.hyp"));

	/* a typo still finds it */
	search(&lib, "Secnd");
	CHECK(lib.npages >= 1);
	row(&lib, 1, t);
	CHECK(strcmp(t, "  Second") == 0);

	/* the action row */
	search(&lib, "nothing-like-this");
	CHECK(lib.nrows == 1 && lib.rows[0].kind == LIB_ACTION && lib.sel == 0);
	row(&lib, 0, t);
	CHECK(strcmp(t, "  Search the full text for \"nothing-like-this\"") == 0);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_FULLTEXT);

	search(&lib, "");
	CHECK(lib.nrows == 7);
	lib_free(&lib);
}

/*
 * Subjects with parts, as ST-Guide's hypertexts give them: one subject
 * with its parts under it, in any case; a "/" at the start or the end
 * is no part.
 */
static void test_subjects(void)
{
	static const char *subj[] = { "Develop/Utilities", "Develop", "develop/shells",
				      "Develop/Editors", "Develop/Utilities", "COPS", "/odd", "Odd/",
				      NULL };
	static const char *title[] = { "u1", "doc", "sh", "ed", "u2", "cops", "slash", "end", "none" };
	ehv_guide g[9];
	ehv_index idx;
	ehv_library lib;
	char t[120];
	unsigned int i;
	long p;

	memset(&idx, 0, sizeof(idx));
	memset(g, 0, sizeof(g));
	for (i = 0; i < 9; i++)
	{
		g[i].path = title[i];
		g[i].key = title[i];
		g[i].database = title[i];
		g[i].subject = subj[i];
	}
	idx.guides = g;
	idx.nguides = 9;
	lib_init(&lib, &idx);
	lib_set_layout(&lib, LIB_PANES);
	/* All, /odd, COPS, Develop (Editors, shells, Utilities), Odd/, Other */
	CHECK(lib.npanes == 9);
	if (lib.npanes == 9)
	{
		CHECK(strcmp(lib.panes[1].text, "/odd") == 0 && lib.panes[1].level == 0);
		CHECK(strcmp(lib.panes[2].text, "COPS") == 0 && lib.panes[2].count == 1);
		CHECK(strcmp(lib.panes[3].text, "Develop") == 0 && lib.panes[3].count == 5);
		CHECK(lib.panes[3].level == 0 && lib.panes[3].tint == 3);
		CHECK(strcmp(lib.panes[4].text, "Editors") == 0 && lib.panes[4].level == 1 && lib.panes[4].tint == 3);
		CHECK(strcmp(lib.panes[5].text, "shells") == 0 && lib.panes[5].count == 1);
		CHECK(strcmp(lib.panes[6].text, "Utilities") == 0 && lib.panes[6].count == 2);
		CHECK(strcmp(lib.panes[7].text, "Odd/") == 0 && lib.panes[7].level == 0);
		CHECK(lib.panes[8].kind == PANE_OTHER && lib.panes[8].count == 1);
	}
	/* Develop: its own first, then each part under a heading */
	p = pane_of(&lib, "Develop");
	CHECK(lib_pane_select(&lib, p) == p && lib.nrows == 8 && lib.nguides == 5);
	if (lib.nrows == 8)
	{
		row(&lib, 0, t);
		CHECK(lib.rows[0].kind == LIB_GUIDE && strcmp(t, "  doc") == 0);
		CHECK(lib.rows[1].flags == LIB_SUB && strcmp(lib.rows[1].text, "Editors") == 0 && lib.rows[1].ref == 1);
		CHECK(lib.rows[3].flags == LIB_SUB && strcmp(lib.rows[3].text, "shells") == 0);
		CHECK(lib.rows[5].flags == LIB_SUB && strcmp(lib.rows[5].text, "Utilities") == 0 && lib.rows[5].ref == 2);
		row(&lib, 7, t);
		CHECK(strcmp(t, "  u2") == 0);
	}
	/* a part: only its own; kept by its name over a new index */
	CHECK(lib_pane_select(&lib, p + 3) == p + 3 && lib.nrows == 2 && lib.rows[0].kind == LIB_GUIDE);
	CHECK(lib_set_index(&lib, &idx) == 0 && lib.pane == p + 3 && lib.nrows == 2);
	/* the Outline: a heading that folds, each part's under it */
	lib_set_layout(&lib, LIB_OUTLINE);
	CHECK(lib.rows[4].kind == LIB_GROUP && strcmp(lib.rows[4].text, "Develop") == 0);
	CHECK(lib.rows[4].flags == LIB_FOLDS && lib.rows[4].ref == 5 && lib.rows[6].flags == LIB_SUB);
	lib_select(&lib, 4);
	CHECK(lib_fold(&lib, 0) == 1 && lib.rows[5].kind == LIB_GROUP && strcmp(lib.rows[5].text, "Odd/") == 0);
	lib_subject_text("Develop/Editors", t, sizeof(t));
	CHECK(strcmp(t, "Develop \xfa Editors") == 0);
	lib_subject_text("COPS", t, sizeof(t));
	CHECK(strcmp(t, "COPS") == 0);
	lib_free(&lib);
}

/*
 * "Documentation/Editors" and "Dokumentation/Editoren" are filed under
 * their part (Erez, 22 Oct 2026): no Documentation subject over them.
 */
static void test_documentation(void)
{
	static const char *subj[] = { "Documentation/Editors", "dokumentation/Editoren", "Editors",
				      "Documentation", "DOCUMENTATION/Editors/Vim" };
	ehv_guide g[5];
	ehv_index idx;
	ehv_library lib;
	unsigned int i;

	CHECK(strcmp(lib_subject("Documentation/Editors"), "Editors") == 0);
	CHECK(strcmp(lib_subject("Dokumentation/Editoren"), "Editoren") == 0);
	CHECK(strcmp(lib_subject("Documentation/Dokumentation/Shells"), "Shells") == 0);
	CHECK(strcmp(lib_subject("Documentation"), "Documentation") == 0);
	CHECK(strcmp(lib_subject("Documentation/"), "Documentation/") == 0);
	CHECK(strcmp(lib_subject("Docs/Editors"), "Docs/Editors") == 0);
	CHECK(lib_subject(NULL) == NULL);

	memset(&idx, 0, sizeof(idx));
	memset(g, 0, sizeof(g));
	for (i = 0; i < 5; i++)
	{
		g[i].path = subj[i];
		g[i].key = subj[i];
		g[i].database = subj[i];
		g[i].subject = subj[i];
	}
	idx.guides = g;
	idx.nguides = 5;
	lib_init(&lib, &idx);
	lib_set_layout(&lib, LIB_PANES);
	/* All, Documentation (the one without a part), Editoren, Editors (Vim) */
	CHECK(lib.npanes == 5);
	if (lib.npanes == 5)
	{
		CHECK(strcmp(lib.panes[1].text, "Documentation") == 0 && lib.panes[1].count == 1);
		CHECK(strcmp(lib.panes[2].text, "Editoren") == 0 && lib.panes[2].count == 1);
		CHECK(strcmp(lib.panes[3].text, "Editors") == 0 && lib.panes[3].count == 3 && lib.panes[3].level == 0);
		CHECK(strcmp(lib.panes[4].text, "Vim") == 0 && lib.panes[4].level == 1);
	}
	lib_set_layout(&lib, LIB_OUTLINE);
	for (i = 0; i < lib.nrows; i++)
		if (lib.rows[i].kind == LIB_GROUP)
			CHECK(strcmp(lib.rows[i].text, "Editors") == 0 || strcmp(lib.rows[i].text, "Editoren") == 0 ||
			      strcmp(lib.rows[i].text, "Documentation") == 0 || strcmp(lib.rows[i].text, "Vim") == 0);
	lib_free(&lib);
}

static int run_fulltext(ehv_library *lib)
{
	int r, steps = 0;

	CHECK(lib_fulltext_start(lib) == 0);
	while ((r = lib_fulltext_step(lib, 1)) == 0)
		steps++;
	CHECK(r == 1);
	return steps;
}

static void test_fulltext(const ehv_index *idx)
{
	ehv_library lib;
	char t[120];
	const char *path, *node;
	unsigned short line;

	lib_init(&lib, idx);
	search(&lib, "beside");
	CHECK(run_fulltext(&lib) > 3);
	CHECK(lib.rows[0].kind == LIB_GROUP && strcmp(lib.rows[0].text, "Full text") == 0);
	CHECK(lib.nrows == 3 && lib.rows[1].kind == LIB_TEXT && lib.rows[2].kind == LIB_NOTE);
	CHECK(lib.sel == 1);
	row(&lib, 1, t);
	CHECK(strcmp(t, "  Main: Text beside a limage.") == 0);
	row(&lib, 2, t);
	CHECK(strncmp(t, "  Found on 1 of ", 16) == 0);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_OPEN);
	CHECK(ends_with(path, "images.hyp") && strcmp(node, "Main") == 0 && line == 0);

	/* line numbers: "0899 ..." is line 900 of the big page */
	search(&lib, "0899");
	run_fulltext(&lib);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_OPEN);
	CHECK(ends_with(path, "big.hyp") && line == 900);

	/* with page matches the selection stays on them */
	search(&lib, "main");
	CHECK(lib.npages == 3 && lib.rows[lib.sel].kind == LIB_PAGE);   /* basic, images, big */
	run_fulltext(&lib);
	CHECK(lib.rows[lib.sel].kind == LIB_PAGE);
	{
		unsigned long i, texts = 0;

		for (i = 0; i < lib.nrows; i++)
			if (lib.rows[i].kind == LIB_TEXT)
				texts++;
		CHECK(texts >= 2);            /* "This is the main page.", "Back to Main" */
	}

	/* a new query drops the search; a new index too */
	search(&lib, "mai");
	CHECK(lib.fts == NULL && lib.rows[lib.nrows - 1].kind == LIB_ACTION);
	lib_set_index(&lib, NULL);
	CHECK(lib.nrows == 1 && lib.rows[0].kind == LIB_NOTE);
	lib_free(&lib);

	/* the search itself */
	{
		ehv_fts *s = ehv_fts_begin(idx, "PAGE");
		unsigned long n;
		const ehv_fts_hit *h;

		CHECK(s != NULL && strcmp(ehv_fts_query(s), "PAGE") == 0);
		while (ehv_fts_step(s, 3) == 0)
			;
		h = ehv_fts_hits(s, &n);
		CHECK(n >= 3 && ehv_fts_done(s));       /* ignoring case */
		CHECK(h[0].count >= 1);
		ehv_fts_free(s);
		CHECK(ehv_fts_begin(idx, "") == NULL);
		CHECK(ehv_fts_begin(NULL, "x") == NULL);
	}
}

int main(int argc, char **argv)
{
	const char *roots[1];
	ehv_index *idx;

	fixdir = argc > 1 ? argv[1] : "tests/fixtures";
	remove_tree();
	mkdir(TREE, 0755);
	copy("basic.hyp");
	copy("basic.ref");
	copy("other.hyp");
	copy("images.hyp");
	copy("big.hyp");
	roots[0] = TREE;
	idx = ehv_scan_all(roots, 1, NULL, NULL);
	CHECK(idx != NULL && idx->nguides == 4);
	if (idx != NULL)
	{
		test_all(idx);
		test_bookmarks(idx);
		test_layouts(idx);
		test_names(idx);
		test_subjects();
	test_documentation();
		test_fulltext(idx);
	}
	ehv_index_free(idx);
	remove_tree();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
