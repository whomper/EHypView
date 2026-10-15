/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView core library unit tests.
 *
 *   test_core FIXTURE_DIR
 *
 * Build with -fsanitize=address,undefined to catch memory errors; the
 * mutation test feeds damaged files to every parser.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hypfile.h"
#include "page.h"
#include "lh5.h"
#include "ref.h"
#include "inf.h"
#include "charset.h"
#include "plat.h"

static int failures;
static int checks;
static const char *fixdir;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

static char *fixture(const char *name)
{
	static char buf[1024];

	snprintf(buf, sizeof(buf), "%s/%s", fixdir, name);
	return buf;
}

static hyp_doc *open_fixture(const char *name)
{
	int err;
	hyp_doc *doc = hyp_open(fixture(name), &err);

	if (doc == NULL)
	{
		printf("FAIL cannot open %s: %s\n", name, hyp_strerror(err));
		exit(1);
	}
	return doc;
}

/* text of a whole line, concatenated */
static void line_text(const hyp_page *pg, unsigned short l, char *buf, size_t size)
{
	unsigned short r;
	size_t n = 0;

	buf[0] = '\0';
	if (l >= pg->nlines)
		return;
	for (r = 0; r < pg->lines[l].nruns; r++)
	{
		const hyp_run *run = &pg->runs[pg->lines[l].first_run + r];
		size_t k = run->len < size - 1 - n ? run->len : size - 1 - n;

		memcpy(buf + n, run->text, k);
		n += k;
	}
	buf[n] = '\0';
}

/* find a run whose text equals s */
static const hyp_run *find_run(const hyp_page *pg, const char *s)
{
	unsigned long i;
	size_t len = strlen(s);

	for (i = 0; i < pg->nruns; i++)
		if (pg->runs[i].len == len && memcmp(pg->runs[i].text, s, len) == 0)
			return &pg->runs[i];
	return NULL;
}

static unsigned short find_line(const hyp_page *pg, const char *prefix)
{
	unsigned short l;
	char buf[512];

	for (l = 0; l < pg->nlines; l++)
	{
		line_text(pg, l, buf, sizeof(buf));
		if (strncmp(buf, prefix, strlen(prefix)) == 0)
			return l;
	}
	return 0xFFFF;
}

/* ---------------------------------------------------------------- */

static void test_lh5_garbage(void)
{
	unsigned char src[512], dst[4096];
	unsigned int i, seed = 1;
	int k;

	/* random input must never crash and must fail or fill exactly */
	for (k = 0; k < 500; k++)
	{
		for (i = 0; i < sizeof(src); i++)
		{
			seed = seed * 1103515245u + 12345u;
			src[i] = (unsigned char) (seed >> 16);
		}
		(void) lh5_decode(src, (unsigned long) (k % 64 + 1), dst, sizeof(dst));
	}
	CHECK(lh5_decode(src, 0, dst, 16) != 0);
	CHECK(lh5_decode(src, 0, dst, 0) == 0);
}

static void test_open_errors(void)
{
	int err;
	FILE *fp;
	const char *tmp = "test_core_bad.hyp";

	CHECK(hyp_open("does/not/exist.hyp", &err) == NULL && err == HYP_ERR_OPEN);

	fp = fopen(tmp, "wb");
	fputs("HREF this is not a hypertext", fp);
	fclose(fp);
	CHECK(hyp_open(tmp, &err) == NULL && err == HYP_ERR_FORMAT);

	/* header claims a huge index table */
	fp = fopen(tmp, "wb");
	fwrite("HDOC\x7f\xff\xff\xff\x00\x10\x03\x02", 1, 12, fp);
	fclose(fp);
	CHECK(hyp_open(tmp, &err) == NULL && err == HYP_ERR_FORMAT);
	remove(tmp);
}

/* an old-style file: no extended headers, data right after the index */
static void test_minimal_file(void)
{
	static const unsigned char file[] = {
		'H', 'D', 'O', 'C', 0, 0, 0, 36, 0, 2, 3, 2,
		/* entry 0: page "Main" at 48, uncompressed */
		20, 0, 0, 0, 0, 48, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 'M', 'a', 'i', 'n', 0, 0,
		/* end-of-data marker at 60 */
		16, 0xFF, 0, 0, 0, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		'H', 'e', 'l', 'l', 'o', 0, 'W', 'o', 'r', 'l', 'd', 0
	};
	const char *tmp = "test_core_min.hyp";
	FILE *fp = fopen(tmp, "wb");
	hyp_doc *doc;
	hyp_page *pg;
	int err;
	char buf[64];

	fwrite(file, 1, sizeof(file), fp);
	fclose(fp);
	doc = hyp_open(tmp, &err);
	CHECK(doc != NULL);
	if (doc != NULL)
	{
		CHECK(doc->num_entries == 1);
		CHECK(doc->database == NULL && doc->width == 0 && doc->stg_flags == 0);
		CHECK(doc->entries[0].size == 12 && doc->entries[0].packed == 12);
		pg = hyp_page_load(doc, hyp_default_node(doc), &err);
		CHECK(pg != NULL && pg->nlines == 2);
		if (pg != NULL)
		{
			line_text(pg, 1, buf, sizeof(buf));
			CHECK(strcmp(buf, "World") == 0);
			CHECK(strcmp(hyp_page_title(doc, pg), "Main") == 0);
		}
		hyp_page_free(pg);
		hyp_close(doc);
	}
	remove(tmp);
}

static void test_basic_doc(const char *name)
{
	hyp_doc *doc = open_fixture(name);
	unsigned short main_node, second, pop, help;

	CHECK(doc->os == HYP_OS_UNIX || doc->os == HYP_OS_ATARI);
	CHECK(doc->database && strcmp(doc->database, "EHypView basic test") == 0);
	CHECK(doc->subject && strcmp(doc->subject, "Test/EHypView") == 0);
	CHECK(doc->author && strcmp(doc->author, "EHypView project") == 0);
	CHECK(doc->help_node && strcmp(doc->help_node, "Help") == 0);
	CHECK(doc->version && strstr(doc->version, "basic.hyp 1.0") != NULL);
	CHECK(doc->width == 70);

	main_node = hyp_find_node(doc, "Main");
	second = hyp_find_node(doc, "Second");
	pop = hyp_find_node(doc, "Pop");
	help = hyp_find_node(doc, "Help");
	CHECK(main_node != HYP_NO_NODE && second != HYP_NO_NODE);
	CHECK(pop != HYP_NO_NODE && doc->entries[pop].type == HYP_NODE_POPUP);
	CHECK(help != HYP_NO_NODE);
	CHECK(hyp_find_node(doc, "main") == main_node);     /* case-insensitive */
	CHECK(hyp_find_node(doc, "nosuchpage") == HYP_NO_NODE);
	CHECK(hyp_default_node(doc) == main_node);
	CHECK(doc->entries[main_node].next == second || doc->entries[second].prev == main_node);

	hyp_close(doc);
}

static void test_basic_pages(const char *name)
{
	hyp_doc *doc = open_fixture(name);
	int err;
	hyp_page *pg = hyp_page_load(doc, hyp_find_node(doc, "Main"), &err);
	const hyp_run *r;
	char buf[512];
	unsigned short l;

	CHECK(pg != NULL);
	if (pg == NULL)
	{
		hyp_close(doc);
		return;
	}
	CHECK(strcmp(hyp_page_title(doc, pg), "Basic test - main page") == 0);
	line_text(pg, 0, buf, sizeof(buf));
	CHECK(strcmp(buf, "This is the main page.") == 0);

	/* see-also list, blanks trimmed */
	CHECK(pg->nxrefs == 2);
	if (pg->nxrefs == 2)
	{
		CHECK(strcmp(pg->xrefs[0].text, "The help page") == 0);
		CHECK(strcmp(doc->entries[pg->xrefs[0].target].name, "Help") == 0);
		CHECK(doc->entries[pg->xrefs[1].target].type == HYP_NODE_EXTERNAL);
	}

	r = find_run(pg, "to Second");
	CHECK(r && r->kind == HYP_RUN_LINK && r->link_kind == HYP_LINK_LINK);
	CHECK(r && strcmp(doc->entries[r->target].name, "Second") == 0 && r->target_line == 0);

	/* source line numbers are 1-based, stored (and reported) 0-based */
	r = find_run(pg, "line 3");
	CHECK(r && r->link_kind == HYP_LINK_ALINK && r->target_line == 2);

	r = find_run(pg, "popup here");
	CHECK(r && doc->entries[r->target].type == HYP_NODE_POPUP);

	r = find_run(pg, "other file");
	CHECK(r && doc->entries[r->target].type == HYP_NODE_EXTERNAL);
	CHECK(r && strcmp(doc->entries[r->target].name, "other.hyp/Start") == 0);

	/* attributes */
	r = find_run(pg, "bold");
	CHECK(r && r->attr == HYP_ATTR_BOLD && r->kind == HYP_RUN_TEXT);
	r = find_run(pg, "italic");
	CHECK(r && r->attr == HYP_ATTR_ITALIC);
	r = find_run(pg, "under");
	CHECK(r && r->attr == HYP_ATTR_UNDERLINE);
	r = find_run(pg, "ghost");
	CHECK(r && r->attr == HYP_ATTR_GHOSTED);
	r = find_run(pg, "outline");
	CHECK(r && r->attr == HYP_ATTR_OUTLINED);
	r = find_run(pg, "shadow");
	CHECK(r && r->attr == HYP_ATTR_SHADOWED);
	r = find_run(pg, "bold+under");
	CHECK(r && r->attr == (HYP_ATTR_BOLD | HYP_ATTR_UNDERLINE));
	r = find_run(pg, " plain");
	CHECK(r && r->attr == 0);

	/* Atari character set passes through unchanged */
	l = find_line(pg, "Umlauts: ");
	CHECK(l != 0xFFFF);
	line_text(pg, l, buf, sizeof(buf));
	CHECK(memcmp(buf + 9, "\x84\x94\x81\x8e\x99\x9a\x9e", 7) == 0);

	/* a link with no explicit text shows the node name */
	l = find_line(pg, "Default label: ");
	CHECK(l != 0xFFFF);
	line_text(pg, l, buf, sizeof(buf));
	CHECK(strcmp(buf, "Default label: Second") == 0);
	hyp_page_free(pg);

	/* graphics on the second page */
	pg = hyp_page_load(doc, hyp_find_node(doc, "Second"), &err);
	CHECK(pg != NULL);
	if (pg != NULL)
	{
		CHECK(pg->ngfx == 4);
		if (pg->ngfx == 4)
		{
			CHECK(pg->gfx[0].type == HYP_GFX_BOX && pg->gfx[0].x == 2 && pg->gfx[0].w == 10 &&
				pg->gfx[0].h == 3 && pg->gfx[0].pattern == 4 && pg->gfx[0].y == 3);
			CHECK(pg->gfx[1].type == HYP_GFX_LINE && pg->gfx[1].w == 10 && pg->gfx[1].h == 2 &&
				pg->gfx[1].arrows == HYP_ARROW_START && pg->gfx[1].style == 1);
			CHECK(pg->gfx[2].type == HYP_GFX_LINE && pg->gfx[2].w == -8 && pg->gfx[2].h == 0 &&
				pg->gfx[2].arrows == (HYP_ARROW_START | HYP_ARROW_END));
			CHECK(pg->gfx[3].type == HYP_GFX_RBOX && pg->gfx[3].pattern == 2);
		}
		hyp_page_free(pg);
	}

	/* non-text entries are refused */
	CHECK(hyp_page_load(doc, doc->num_entries, &err) == NULL && err == HYP_ERR_RANGE);
	hyp_close(doc);
}

static void test_images(void)
{
	hyp_doc *doc = open_fixture("images.hyp");
	unsigned short i, nimg = 0;
	int big_seen = 0;

	for (i = 0; i < doc->num_entries; i++)
	{
		const hyp_entry *e = &doc->entries[i];
		unsigned char *data;
		unsigned long size;

		if (e->type != HYP_NODE_IMAGE)
			continue;
		nimg++;
		CHECK(hyp_load(doc, i, &data, &size) == HYP_OK);
		if (data == NULL)
			continue;
		CHECK(size == e->size);
		/* image header: width, height, planes */
		if (size >= 8)
		{
			unsigned int w = (data[0] << 8) | data[1];
			unsigned int h = (data[2] << 8) | data[3];

			CHECK(data[4] == 4);
			if (w == 640)
			{
				big_seen = 1;
				CHECK(h == 400);
				CHECK(size > 65536UL);     /* needs the high size bits */
				CHECK(size >= 8 + 640UL / 8 * 400 * 4);
			}
		}
		free(data);
	}
	CHECK(nimg == 2);
	CHECK(big_seen);
	hyp_close(doc);
}

static void test_big(void)
{
	hyp_doc *doc = open_fixture("big.hyp");
	int err;
	hyp_page *pg = hyp_page_load(doc, hyp_find_node(doc, "Main"), &err);
	char buf[512];

	CHECK(pg != NULL);
	if (pg != NULL)
	{
		CHECK(pg->nlines == 901);
		line_text(pg, 1, buf, sizeof(buf));
		CHECK(strncmp(buf, "0000 ", 5) == 0);
		line_text(pg, 900, buf, sizeof(buf));
		CHECK(strncmp(buf, "0899 ", 5) == 0);
		CHECK(doc->entries[pg->num].packed < doc->entries[pg->num].size / 2);
		hyp_page_free(pg);
	}
	hyp_close(doc);
}

static void test_encrypted(void)
{
	hyp_doc *a = open_fixture("basic.hyp");
	hyp_doc *b = open_fixture("basic_enc.hyp");
	unsigned short i;

	CHECK(b->stg_flags & HYP_STG_ENCRYPTED);
	CHECK(a->num_entries == b->num_entries);
	for (i = 0; i < a->num_entries && i < b->num_entries; i++)
	{
		unsigned char *da, *db;
		unsigned long sa, sb;

		if (!HYP_IS_TEXT(a->entries[i].type))
			continue;
		CHECK(hyp_load(a, i, &da, &sa) == HYP_OK);
		CHECK(hyp_load(b, i, &db, &sb) == HYP_OK);
		CHECK(da && db && sa == sb && memcmp(da, db, sa) == 0);
		free(da);
		free(db);
	}
	hyp_close(a);
	hyp_close(b);
}

static void test_ref(void)
{
	int err;
	ref_db *db = ref_open(fixture("basic.ref"), &err);
	unsigned long hits[8], n;
	char name[64];

	CHECK(db != NULL);
	if (db == NULL)
		return;
	CHECK(db->nmodules == 1);
	CHECK(db->modules[0].file && strcmp(db->modules[0].file, "basic") == 0);
	CHECK(db->modules[0].database && strcmp(db->modules[0].database, "EHypView basic test") == 0);
	ref_module_filename(&db->modules[0], name, sizeof(name));
	CHECK(strcmp(name, "basic.hyp") == 0);

	n = ref_find(db, "Second", hits, 8);
	CHECK(n == 1 && db->syms[hits[0]].kind == REF_NODE);
	n = ref_find(db, "zweite", hits, 8);       /* alias, ignoring case */
	CHECK(n == 1 && db->syms[hits[0]].kind == REF_ALIAS);
	CHECK(n == 1 && db->syms[hits[0]].node && strcmp(db->syms[hits[0]].node, "Second") == 0);
	/* @keywords are stored as labels on line 0 of their page */
	n = ref_find(db, "secondkw", hits, 8);
	CHECK(n == 1 && db->syms[hits[0]].kind == REF_LABEL && db->syms[hits[0]].line == 0);
	CHECK(n == 1 && db->syms[hits[0]].node && strcmp(db->syms[hits[0]].node, "Second") == 0);
	CHECK(ref_find(db, "absent", hits, 8) == 0);
	ref_close(db);

	CHECK(ref_open(fixture("basic.hyp"), &err) == NULL && err == REF_ERR_FORMAT);
}

static void test_inf(void)
{
	int err;
	char **list;
	unsigned int n;
	inf_file *inf = inf_parse(
		"# ST-Guide settings\r\n"
		"PATHS=C:\\GUIDES\\;D:\\DOC\\ , ;E:\\\r\n"
		"ref = C:\\GUIDES\\ALL.REF\r\n"
		"; comment=ignored\n"
		"HYPFOLD=C:\\GUIDES\n"
		"noequals\n"
		"HYPFOLD=D:\\LATER\n", &err);

	CHECK(inf != NULL && err == INF_OK);
	if (inf == NULL)
		return;
	CHECK(inf_get(inf, "REF") && strcmp(inf_get(inf, "REF"), "C:\\GUIDES\\ALL.REF") == 0);
	CHECK(inf_get(inf, "hypfold") && strcmp(inf_get(inf, "HYPFOLD"), "D:\\LATER") == 0);
	CHECK(inf_get(inf, "comment") == NULL);
	CHECK(inf_get(inf, "noequals") == NULL);

	n = inf_split_list(inf_get(inf, "PATHS"), &list);
	CHECK(n == 3);
	if (n == 3)
	{
		CHECK(strcmp(list[0], "C:\\GUIDES\\") == 0);
		CHECK(strcmp(list[1], "D:\\DOC\\") == 0);
		CHECK(strcmp(list[2], "E:\\") == 0);
	}
	inf_free_list(list, n);
	CHECK(inf_split_list("", &list) == 0 && list == NULL);
	CHECK(inf_split_list(NULL, &list) == 0);
	inf_free(inf);
}

static void test_inf_put(void)
{
	static const char old[] =
		"; EHypView's settings\r\n"
		"\r\n"
		"; the text font\r\n"
		"font = Swiss\r\n"
		"; FONT=not this one\r\n"
		"MINE=kept\n"
		"POINT=10";
	char *t = inf_put(old, "FONT", "SYSTEM", "the text font"), *u;

	CHECK(t != NULL && strcmp(t, "; EHypView's settings\r\n\r\n; the text font\r\nFONT=SYSTEM\r\n"
				 "; FONT=not this one\r\nMINE=kept\r\nPOINT=10\r\n") == 0);
	u = inf_put(t, "FOLDER", "C:\\EHYPVIEW", "where its files are");
	CHECK(u != NULL && strstr(u, "POINT=10\r\n; where its files are\r\nFOLDER=C:\\EHYPVIEW\r\n\r\n") != NULL);
	free(t);
	t = inf_put(u, "folder", "D:\\", NULL);
	CHECK(t != NULL && strstr(t, "folder=D:\\\r\n\r\n") != NULL && strstr(t, "C:\\EHYPVIEW") == NULL);
	free(t);
	free(u);
	t = inf_put(NULL, "POINT", "12", "its size");
	CHECK(t != NULL && strcmp(t, "; its size\r\nPOINT=12\r\n\r\n") == 0);
	free(t);
}

static void test_save(void)
{
	unsigned long n;
	char *t;
	FILE *fp;

	remove("TSAVE.INF");
	CHECK(pf_save("TSAVE.INF", "one", 3) == 0 && pf_save("TSAVE.INF", "two!", 4) == 0);
	t = pf_load("TSAVE.INF", &n);
	CHECK(t != NULL && n == 4 && strcmp(t, "two!") == 0);
	free(t);
	fp = fopen("TSAVE.IN$", "rb");
	CHECK(fp == NULL);                 /* the 8.3 temporary name, gone */
	if (fp != NULL)
		fclose(fp);
	/* a reset between the old file going and the new one's renaming */
	remove("TSAVE.INF");
	fp = fopen("TSAVE.IN$", "wb");
	if (fp != NULL)
	{
		fputs("whole", fp);
		fclose(fp);
	}
	t = pf_load("TSAVE.INF", &n);
	CHECK(t != NULL && strcmp(t, "whole") == 0);
	free(t);
	remove("TSAVE.INF");
}

static void test_charset(void)
{
	char b[4];

	CHECK(atari_to_unicode('A') == 'A');
	CHECK(atari_to_unicode(0x84) == 0x00E4);       /* a umlaut */
	CHECK(atari_to_unicode(0x9E) == 0x00DF);       /* sharp s */
	CHECK(atari_to_unicode(0xC2) == 0x05D0);       /* alef */
	CHECK(atari_to_utf8(0x84, b) == 2 && (unsigned char) b[0] == 0xC3 && (unsigned char) b[1] == 0xA4);
	CHECK(atari_to_utf8(0x10, b) == 4);
}

/* damaged files must be rejected or parsed without memory errors */
static void test_mutations(const char *name)
{
	FILE *fp = fopen(fixture(name), "rb");
	unsigned char *orig, *buf;
	long size;
	unsigned int seed = 7;
	int k;
	const char *tmp = "test_core_mut.hyp";

	if (fp == NULL)
		return;
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	rewind(fp);
	orig = malloc((size_t) size);
	buf = malloc((size_t) size);
	if (fread(orig, 1, (size_t) size, fp) != (size_t) size)
		size = 0;
	fclose(fp);

	for (k = 0; k < 300 && size > 0; k++)
	{
		hyp_doc *doc;
		int i, nflip;
		long cut = size;

		memcpy(buf, orig, (size_t) size);
		seed = seed * 1103515245u + 12345u;
		nflip = 1 + (int) ((seed >> 16) % 8);
		for (i = 0; i < nflip; i++)
		{
			seed = seed * 1103515245u + 12345u;
			buf[(seed >> 8) % (unsigned long) size] ^= (unsigned char) (1u << ((seed >> 4) & 7));
		}
		if (k % 5 == 0)
		{
			seed = seed * 1103515245u + 12345u;
			cut = (long) ((seed >> 8) % (unsigned long) size);
		}
		fp = fopen(tmp, "wb");
		fwrite(buf, 1, (size_t) cut, fp);
		fclose(fp);

		if (strstr(name, ".ref") != NULL)
		{
			ref_db *db = ref_open(tmp, NULL);

			if (db != NULL)
			{
				unsigned long hits[4];

				(void) ref_find(db, "Second", hits, 4);
				ref_close(db);
			}
			continue;
		}
		doc = hyp_open(tmp, NULL);
		if (doc != NULL)
		{
			unsigned short n;

			for (n = 0; n < doc->num_entries; n++)
			{
				if (HYP_IS_TEXT(doc->entries[n].type))
				{
					hyp_page *pg = hyp_page_load(doc, n, NULL);

					hyp_page_free(pg);
				} else if (doc->entries[n].type == HYP_NODE_IMAGE)
				{
					unsigned char *d;
					unsigned long s;

					if (hyp_load(doc, n, &d, &s) == HYP_OK)
						free(d);
				}
			}
			hyp_close(doc);
		}
	}
	remove(tmp);
	free(orig);
	free(buf);
	checks++;
}

int main(int argc, char **argv)
{
	fixdir = argc > 1 ? argv[1] : "tests/fixtures";

	test_lh5_garbage();
	test_open_errors();
	test_minimal_file();
	test_basic_doc("basic.hyp");
	test_basic_doc("basic_raw.hyp");
	test_basic_doc("basic_enc.hyp");
	test_basic_pages("basic.hyp");
	test_basic_pages("basic_raw.hyp");
	test_basic_pages("basic_enc.hyp");
	test_images();
	test_big();
	test_encrypted();
	test_ref();
	test_inf();
	test_inf_put();
	test_save();
	test_charset();
	test_mutations("basic.hyp");
	test_mutations("images.hyp");
	test_mutations("basic.ref");

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
