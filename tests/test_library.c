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

	/* grouped by subject, titles sorted, no subject last */
	CHECK(lib.nrows == 6 && lib.nguides == 4);
	if (lib.nrows == 6)
	{
		CHECK(lib.rows[0].kind == LIB_GROUP && strcmp(lib.rows[0].text, "Test/EHypView") == 0);
		row(&lib, 1, t);
		CHECK(strcmp(t, "  EHypView basic test") == 0);
		row(&lib, 2, t);
		CHECK(strcmp(t, "  EHypView image test") == 0);
		row(&lib, 3, t);
		CHECK(strcmp(t, "  EHypView other test") == 0);
		CHECK(lib.rows[4].kind == LIB_GROUP && strcmp(lib.rows[4].text, "Other hypertexts") == 0);
		row(&lib, 5, t);
		CHECK(strcmp(t, "  EHypView big test") == 0);
		lib_row_detail(&lib, 5, t, sizeof(t));
		CHECK(strcmp(t, "big") == 0);
	}
	CHECK(lib.sel == 1);
	CHECK(lib_move(&lib, 1) == 2);
	CHECK(lib_move(&lib, 10) == 5);           /* over the heading, stops at the end */
	CHECK(lib_move(&lib, -1) == 3);
	CHECK(lib_move(&lib, -10) == 1);
	CHECK(lib_select(&lib, 4) == 1);          /* headings are not selectable */
	CHECK(lib_select(&lib, 5) == 5);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_OPEN);
	CHECK(ends_with(path, "big.hyp") && node == NULL && line == 0);
	lib_free(&lib);
}

static void test_names(const ehv_index *idx)
{
	ehv_library lib;
	char t[120];
	const char *path, *node;
	unsigned short line;

	lib_init(&lib, idx);
	CHECK(lib_set_query(&lib, "det") == 0);
	CHECK(lib.npages == 1 && lib.nguides == 0 && lib.nrows == 3);
	CHECK(lib.rows[0].kind == LIB_GROUP && lib.rows[1].kind == LIB_PAGE && lib.rows[2].kind == LIB_ACTION);
	CHECK(lib.sel == 1);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_OPEN);
	CHECK(ends_with(path, "other.hyp") && node && strcmp(node, "Detail") == 0);
	lib_row_detail(&lib, 1, t, sizeof(t));
	CHECK(strcmp(t, "EHypView other test") == 0);

	/* exact before prefix: Second, then the keyword secondkw */
	lib_set_query(&lib, "second");
	CHECK(lib.npages == 2);
	row(&lib, 1, t);
	CHECK(strcmp(t, "  Second") == 0);
	row(&lib, 2, t);
	CHECK(strcmp(t, "  secondkw (in Second)") == 0);

	/* aliases lead to their page */
	lib_set_query(&lib, "Zweite");
	row(&lib, 1, t);
	CHECK(strcmp(t, "  Zweite -> Second") == 0);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_OPEN && strcmp(node, "Second") == 0);

	/* titles */
	lib_set_query(&lib, "image");
	CHECK(lib.npages == 0 && lib.nguides == 1);
	CHECK(lib.rows[0].kind == LIB_GROUP && strcmp(lib.rows[0].text, "Hypertexts") == 0);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_OPEN && ends_with(path, "images.hyp"));

	/* a typo still finds it */
	lib_set_query(&lib, "Secnd");
	CHECK(lib.npages >= 1);
	row(&lib, 1, t);
	CHECK(strcmp(t, "  Second") == 0);

	/* the action row */
	lib_set_query(&lib, "nothing-like-this");
	CHECK(lib.nrows == 1 && lib.rows[0].kind == LIB_ACTION && lib.sel == 0);
	row(&lib, 0, t);
	CHECK(strcmp(t, "  Search the full text for \"nothing-like-this\"") == 0);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_FULLTEXT);

	lib_set_query(&lib, "");
	CHECK(lib.nrows == 6);
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
	lib_set_query(&lib, "beside");
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
	lib_set_query(&lib, "0899");
	run_fulltext(&lib);
	CHECK(lib_activate(&lib, &path, &node, &line) == LIB_OPEN);
	CHECK(ends_with(path, "big.hyp") && line == 900);

	/* with page matches the selection stays on them */
	lib_set_query(&lib, "main");
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
	lib_set_query(&lib, "mai");
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
		test_names(idx);
		test_fulltext(idx);
	}
	ehv_index_free(idx);
	remove_tree();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
