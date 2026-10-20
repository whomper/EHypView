/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView tests for bookmarks (src/view/bookmark.c).
 *
 *   test_bookmark
 */
#include <stdio.h>
#include <string.h>
#include "bookmark.h"

static int failures, checks;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

static void test_add_find_remove(void)
{
	ehv_bookmarks bm;
	char name[32];
	unsigned int i;

	bm_init(&bm);
	CHECK(bm.n == 0 && bm_find(&bm, "C:\\A.HYP", "Main") == -1);
	CHECK(bm_add(&bm, "C:\\A.HYP", "Main", "A - Main") == 0);
	CHECK(bm_add(&bm, "C:\\B.HYP", "Start", "") == 1);          /* no title: the page name */
	CHECK(bm.n == 2 && strcmp(bm.b[1].title, "Start") == 0);
	/* the same page again, path in any case: the one there, untouched */
	CHECK(bm_add(&bm, "c:\\a.hyp", "Main", "other title") == 0 && bm.n == 2);
	CHECK(strcmp(bm.b[0].title, "A - Main") == 0);
	/* page names are exact */
	CHECK(bm_find(&bm, "C:\\A.HYP", "main") == -1 && bm_find(&bm, "C:\\a.HYP", "Main") == 0);
	/* titles are cut, and a '|' or a line end in one becomes a blank */
	CHECK(bm_add(&bm, "C:\\C.HYP", "X", "a|b\nc 0123456789012345678901234567890123456789012345678901234") == 2);
	CHECK(strlen(bm.b[2].title) == BM_TITLE_MAX && strncmp(bm.b[2].title, "a b c ", 6) == 0);
	/* paths and page names may not hold one */
	CHECK(bm_add(&bm, "C:\\D|.HYP", "X", "t") == -1 && bm_add(&bm, "C:\\D.HYP", "X|Y", "t") == -1);
	CHECK(bm_add(&bm, "", "X", "t") == -1 && bm.n == 3);

	/* removing one moves the rest up */
	CHECK(bm_remove(&bm, 0) == 0 && bm.n == 2);
	CHECK(strcmp(bm.b[0].path, "C:\\B.HYP") == 0 && strcmp(bm.b[1].path, "C:\\C.HYP") == 0);
	CHECK(bm.b[2].path == NULL);
	CHECK(bm_remove(&bm, 2) == -1 && bm_remove(&bm, 5) == -1);

	/* at most BM_MAX */
	for (i = bm.n; i < BM_MAX; i++)
	{
		sprintf(name, "P%u", i);
		CHECK(bm_add(&bm, "C:\\E.HYP", name, name) == (int) i);
	}
	CHECK(bm.n == BM_MAX && bm_add(&bm, "C:\\F.HYP", "Main", "full") == -1);
	CHECK(bm_add(&bm, "C:\\E.HYP", "P5", "already there") == 5);   /* still found */
	bm_free(&bm);
	CHECK(bm.n == 0);
}

static void test_inf(void)
{
	ehv_bookmarks bm, again;
	inf_file *inf;
	char key[16], val[200], text[2000];
	unsigned int i;
	int err;

	bm_key(0, key, sizeof(key));
	CHECK(strcmp(key, "BOOKMARK1") == 0);
	bm_key(9, key, sizeof(key));
	CHECK(strcmp(key, "BOOKMARK10") == 0);

	/* read: empty and broken ones are left out, the order kept */
	inf = inf_parse("; EHYPVIEW.INF\r\nFOLDER=C:\\\r\n"
		"BOOKMARK1=C:\\GUIDES\\TOS.HYP|Fsfirst|TOS - Fsfirst\r\n"
		"BOOKMARK2=\r\n"
		"BOOKMARK3=no separators\r\n"
		"BOOKMARK4=C:\\A.HYP|Main|A - Main | more\r\n"
		"BOOKMARK11=C:\\TOO.HYP|Far|beyond the last\r\n", &err);
	CHECK(inf != NULL);
	bm_init(&bm);
	bm_read(&bm, inf);
	CHECK(bm.n == 2);
	CHECK(strcmp(bm.b[0].path, "C:\\GUIDES\\TOS.HYP") == 0 && strcmp(bm.b[0].node, "Fsfirst") == 0);
	CHECK(strcmp(bm.b[0].title, "TOS - Fsfirst") == 0);
	CHECK(strcmp(bm.b[1].title, "A - Main   more") == 0);   /* a '|' in a title */
	inf_free(inf);

	/* values, and a round trip through the INF text */
	CHECK(bm_value(&bm, 0, val, sizeof(val)) == strlen("C:\\GUIDES\\TOS.HYP|Fsfirst|TOS - Fsfirst"));
	CHECK(strcmp(val, "C:\\GUIDES\\TOS.HYP|Fsfirst|TOS - Fsfirst") == 0);
	CHECK(bm_value(&bm, 2, val, sizeof(val)) == 0 && val[0] == '\0');   /* past the last: empty */
	CHECK(bm_value(&bm, 0, val, 10) == 0);                             /* doesn't fit */
	text[0] = '\0';
	for (i = 0; i < BM_MAX; i++)
	{
		bm_key(i, key, sizeof(key));
		bm_value(&bm, i, val, sizeof(val));
		sprintf(text + strlen(text), "%s=%s\r\n", key, val);
	}
	inf = inf_parse(text, &err);
	bm_init(&again);
	bm_read(&again, inf);
	CHECK(again.n == bm.n);
	for (i = 0; i < bm.n; i++)
		CHECK(strcmp(again.b[i].path, bm.b[i].path) == 0 && strcmp(again.b[i].node, bm.b[i].node) == 0 &&
			strcmp(again.b[i].title, bm.b[i].title) == 0);
	inf_free(inf);
	bm_read(&again, NULL);                                             /* none */
	CHECK(again.n == 0);
	bm_free(&again);
	bm_free(&bm);
}

static void test_recent(void)
{
	ehv_bookmarks r, again;
	inf_file *inf;
	char key[16];

	bm_init(&r);
	CHECK(bm_touch(&r, "C:\\A.HYP", "Main", "A - Main", 3) == 1 && r.n == 1);
	CHECK(bm_touch(&r, "C:\\A.HYP", "Main", "A - Main", 3) == 0);        /* first already */
	CHECK(bm_touch(&r, "C:\\B.HYP", "Start", "B - Start", 3) == 1);
	CHECK(r.n == 2 && strcmp(r.b[0].path, "C:\\B.HYP") == 0 && strcmp(r.b[1].path, "C:\\A.HYP") == 0);
	/* the same hypertext, another page: that one, first */
	CHECK(bm_touch(&r, "c:\\a.hyp", "Index", "A - Index", 3) == 1);
	CHECK(r.n == 2 && strcmp(r.b[0].node, "Index") == 0 && strcmp(r.b[1].path, "C:\\B.HYP") == 0);
	CHECK(bm_touch(&r, "C:\\C.HYP", "Main", "C", 3) == 1 && bm_touch(&r, "C:\\D.HYP", "Main", "D", 3) == 1);
	CHECK(r.n == 3 && strcmp(r.b[0].path, "C:\\D.HYP") == 0 && strcmp(r.b[2].node, "Index") == 0);
	CHECK(bm_touch(&r, "C:\\E|.HYP", "Main", "E", 3) == -1);
	bm_key_as("RECENT", 4, key, sizeof(key));
	CHECK(strcmp(key, "RECENT5") == 0);

	inf = inf_parse("RECENT1=C:\\X.HYP|Main|X\r\nRECENT2=\r\nRECENT3=C:\\Y.HYP|P|Y\r\n"
			"RECENT4=C:\\Z.HYP|P|Z\r\nBOOKMARK1=C:\\B.HYP|P|B\r\n", NULL);
	bm_init(&again);
	bm_read_as(&again, inf, "RECENT", 3);       /* RECENT4 is past the 3 */
	CHECK(again.n == 2 && strcmp(again.b[1].path, "C:\\Y.HYP") == 0);
	inf_free(inf);
	bm_free(&again);
	bm_free(&r);
}

int main(void)
{
	test_add_find_remove();
	test_inf();
	test_recent();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
