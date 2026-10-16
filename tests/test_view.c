/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView tests for the viewer model (src/view): opening, links,
 * history, page navigation and keyboard link selection.
 *
 *   test_view FIXTURE_DIR
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "view.h"
#include "scan.h"

static int failures, checks;
static const char *fixdir;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

static char *fx(const char *name)
{
	static char buf[2][1024];
	static int k;

	k ^= 1;
	snprintf(buf[k], sizeof(buf[k]), "%s/%s", fixdir, name);
	return buf[k];
}

static const char *page_name(const ehv_view *v)
{
	return v->page ? v->doc->entries[v->page->num].name : "(none)";
}

static int ends_with(const char *s, const char *tail)
{
	size_t ls = strlen(s), lt = strlen(tail);

	return ls >= lt && strcmp(s + ls - lt, tail) == 0;
}

/* the run whose text is s */
static long run_named(const ehv_view *v, const char *s)
{
	unsigned long i;
	size_t n = strlen(s);

	for (i = 0; v->page && i < v->page->nruns; i++)
		if (v->page->runs[i].len == n && memcmp(v->page->runs[i].text, s, n) == 0)
			return (long) i;
	return -1;
}

static void test_open_and_links(void)
{
	ehv_view v;
	char title[100];
	long r;

	view_init(&v, NULL);
	CHECK(!view_has_page(&v));
	view_title(&v, title, sizeof(title));
	CHECK(strcmp(title, "EHypView") == 0);

	CHECK(view_open(&v, fx("basic.hyp"), NULL, 0) == VIEW_OK);
	CHECK(strcmp(page_name(&v), "Main") == 0 && v.nhist == 0);
	view_title(&v, title, sizeof(title));
	CHECK(strcmp(title, " EHypView basic test - Basic test - main page ") == 0);

	/* a plain link */
	r = run_named(&v, "to Second");
	CHECK(r >= 0 && view_follow(&v, &v.page->runs[r]) == VIEW_OK);
	CHECK(strcmp(page_name(&v), "Second") == 0 && v.nhist == 1 && v.top == 0);
	CHECK(view_back(&v) == VIEW_OK && strcmp(page_name(&v), "Main") == 0 && v.nhist == 0);
	CHECK(view_back(&v) == VIEW_ERROR && v.status[0] != '\0');

	/* a link to a line (0-based line 2) */
	r = run_named(&v, "line 3");
	CHECK(r >= 0 && view_follow(&v, &v.page->runs[r]) == VIEW_OK);
	CHECK(strcmp(page_name(&v), "Second") == 0 && v.top == 2);
	view_back(&v);

	/* an external link: other.hyp is found next to basic.hyp */
	r = run_named(&v, "other file");
	CHECK(r >= 0 && view_follow(&v, &v.page->runs[r]) == VIEW_OK);
	CHECK(ends_with(v.doc->path, "other.hyp") && strcmp(page_name(&v), "Start") == 0);
	/* ... and back across files */
	r = run_named(&v, "basic second");
	CHECK(r >= 0 && view_follow(&v, &v.page->runs[r]) == VIEW_OK);
	CHECK(ends_with(v.doc->path, "basic.hyp") && strcmp(page_name(&v), "Second") == 0 && v.nhist == 2);
	CHECK(view_back(&v) == VIEW_OK && ends_with(v.doc->path, "other.hyp"));
	CHECK(view_back(&v) == VIEW_OK && ends_with(v.doc->path, "basic.hyp") && strcmp(page_name(&v), "Main") == 0);

	/* a quit link closes the window */
	CHECK(view_relative(&v, VIEW_HELP) == VIEW_OK && strcmp(page_name(&v), "Help") == 0);
	r = run_named(&v, "Quit");
	CHECK(r >= 0 && view_follow(&v, &v.page->runs[r]) == VIEW_CLOSE);

	/* non-links are not followed */
	CHECK(view_follow(&v, NULL) == VIEW_ERROR);
	view_free(&v);
}

/* a run of the pop-up whose text starts with `text`, or -1 */
static long popup_run(const ehv_view *v, const char *text)
{
	unsigned long i;

	for (i = 0; v->popup != NULL && i < v->popup->nruns; i++)
		if (v->popup->runs[i].kind == HYP_RUN_LINK && strncmp(v->popup->runs[i].text, text, strlen(text)) == 0)
			return (long) i;
	return -1;
}

static void test_popup(void)
{
	ehv_view v;
	long r, top;

	view_init(&v, NULL);
	CHECK(view_open(&v, fx("basic.hyp"), NULL, 0) == VIEW_OK);
	CHECK(v.popup == NULL && view_popup_cols(&v) == 0 && view_popup_link_at(&v, 0, 0) == -1);
	CHECK(view_popup_follow(&v, 0) == VIEW_ERROR);
	view_popup_close(&v);                                   /* none open: nothing happens */

	/* a link to a @pnode opens it over the page, which stays */
	r = run_named(&v, "popup here");
	v.top = top = 1;
	CHECK(r >= 0 && view_follow(&v, &v.page->runs[r]) == VIEW_POPUP);
	CHECK(v.popup != NULL && strcmp(v.doc->entries[v.popup_node].name, "Pop") == 0);
	CHECK(strcmp(page_name(&v), "Main") == 0 && v.nhist == 0 && v.top == top);
	CHECK(v.popup->nlines >= 2);
	CHECK(view_popup_cols(&v) == (long) strlen("It links to Second."));

	/* its links: "Second" is at column 12 of its second line */
	CHECK(view_popup_link_at(&v, 0, 0) == -1);               /* plain text */
	CHECK(view_popup_link_at(&v, 1, 11) == -1);
	r = view_popup_link_at(&v, 1, 12);
	CHECK(r >= 0 && r == popup_run(&v, "Second"));
	CHECK(view_popup_link_at(&v, 1, 18) == -1);              /* the full stop */
	CHECK(view_popup_link_at(&v, 9, 0) == -1 && view_popup_link_at(&v, 1, -1) == -1);
	CHECK(view_popup_follow(&v, 999) == VIEW_ERROR && v.popup != NULL);

	/* following one closes the pop-up and goes there */
	CHECK(view_popup_follow(&v, r) == VIEW_OK);
	CHECK(v.popup == NULL && strcmp(page_name(&v), "Second") == 0 && v.nhist == 1);
	CHECK(view_back(&v) == VIEW_OK && strcmp(page_name(&v), "Main") == 0);

	/* closing it leaves the page as it was */
	r = run_named(&v, "popup here");
	CHECK(view_follow(&v, &v.page->runs[r]) == VIEW_POPUP);
	view_popup_close(&v);
	CHECK(v.popup == NULL && strcmp(page_name(&v), "Main") == 0);

	/* any page change closes it: following, back, next, another file */
	CHECK(view_follow(&v, &v.page->runs[r]) == VIEW_POPUP);
	CHECK(view_follow(&v, &v.page->runs[run_named(&v, "to Second")]) == VIEW_OK && v.popup == NULL);
	view_back(&v);
	r = run_named(&v, "popup here");
	CHECK(view_follow(&v, &v.page->runs[r]) == VIEW_POPUP);
	CHECK(v.nhist == 0 && view_back(&v) == VIEW_ERROR && v.popup != NULL);  /* no page change */
	CHECK(view_open(&v, fx("basic.hyp"), NULL, 0) == VIEW_OK && v.popup == NULL && v.nhist == 1);
	r = run_named(&v, "popup here");
	CHECK(view_follow(&v, &v.page->runs[r]) == VIEW_POPUP);
	CHECK(view_back(&v) == VIEW_OK && v.popup == NULL);
	r = run_named(&v, "popup here");
	CHECK(view_follow(&v, &v.page->runs[r]) == VIEW_POPUP);
	CHECK(view_relative(&v, VIEW_HELP) == VIEW_OK && v.popup == NULL);
	view_back(&v);
	r = run_named(&v, "popup here");
	CHECK(view_follow(&v, &v.page->runs[r]) == VIEW_POPUP);
	CHECK(view_open(&v, fx("other.hyp"), NULL, 0) == VIEW_OK && v.popup == NULL);

	/* and view_free lets an open one go (ASan checks) */
	view_back(&v);
	r = run_named(&v, "popup here");
	CHECK(view_follow(&v, &v.page->runs[r]) == VIEW_POPUP);
	view_free(&v);
	CHECK(v.popup == NULL);
}

static void test_relative(void)
{
	ehv_view v;

	view_init(&v, NULL);
	CHECK(!view_can(&v, VIEW_NEXT) && !view_can(&v, VIEW_HOME) && !view_can(&v, VIEW_BACK));
	CHECK(view_open(&v, fx("other.hyp"), NULL, 0) == VIEW_OK);
	CHECK(strcmp(page_name(&v), "Start") == 0);               /* @default */
	/* what the Navigate menu enables */
	CHECK(view_can(&v, VIEW_NEXT) && view_can(&v, VIEW_INDEX) && view_can(&v, VIEW_HOME));
	CHECK(!view_can(&v, VIEW_PREV) && !view_can(&v, VIEW_HELP) && !view_can(&v, VIEW_BACK));
	CHECK(view_relative(&v, VIEW_NEXT) == VIEW_OK && strcmp(page_name(&v), "Detail") == 0);
	CHECK(view_relative(&v, VIEW_PREV) == VIEW_OK && strcmp(page_name(&v), "Start") == 0);
	CHECK(view_relative(&v, VIEW_INDEX) == VIEW_OK && strcmp(page_name(&v), "Index") == 0);
	CHECK(view_relative(&v, VIEW_HOME) == VIEW_OK && strcmp(page_name(&v), "Start") == 0);
	CHECK(view_relative(&v, VIEW_HELP) == VIEW_ERROR && v.status[0] != '\0');
	CHECK(view_relative(&v, VIEW_PREV) == VIEW_ERROR);
	CHECK(v.nhist == 4 && view_can(&v, VIEW_BACK));
	view_free(&v);
}

/* back and forward, as a web browser: forward is gone after any other move */
static void test_forward(void)
{
	ehv_view v;

	view_init(&v, NULL);
	CHECK(!view_can(&v, VIEW_FORWARD) && view_forward(&v) == VIEW_ERROR);
	CHECK(view_open(&v, fx("other.hyp"), NULL, 0) == VIEW_OK);         /* Start */
	CHECK(view_relative(&v, VIEW_NEXT) == VIEW_OK);                     /* Detail */
	CHECK(view_relative(&v, VIEW_INDEX) == VIEW_OK);                    /* Index */
	CHECK(v.nhist == 2 && v.nfwd == 0);
	CHECK(view_jump_title(&v, -1) != NULL && view_jump_title(&v, -3) == NULL && view_jump_title(&v, 1) == NULL);
	CHECK(view_back(&v) == VIEW_OK && strcmp(page_name(&v), "Detail") == 0);
	CHECK(view_can(&v, VIEW_FORWARD) && v.nhist == 1 && v.nfwd == 1);
	CHECK(view_jump_title(&v, 1) != NULL && view_jump_title(&v, 1)[0] != '\0');
	CHECK(view_forward(&v) == VIEW_OK && strcmp(page_name(&v), "Index") == 0);
	CHECK(!view_can(&v, VIEW_FORWARD) && view_forward(&v) == VIEW_ERROR && v.status[0] != '\0');
	CHECK(v.nhist == 2);

	/* two steps back at once: the page between goes forward too */
	CHECK(view_jump(&v, -2) == VIEW_OK && strcmp(page_name(&v), "Start") == 0);
	CHECK(v.nhist == 0 && v.nfwd == 2);
	CHECK(view_jump(&v, -1) == VIEW_ERROR && strcmp(page_name(&v), "Start") == 0 && v.nfwd == 2);
	CHECK(view_jump(&v, 3) == VIEW_ERROR && v.nfwd == 2);               /* not that far */
	CHECK(view_jump(&v, 1) == VIEW_OK && strcmp(page_name(&v), "Detail") == 0);
	CHECK(view_jump(&v, 1) == VIEW_OK && strcmp(page_name(&v), "Index") == 0 && v.nfwd == 0);
	CHECK(view_jump(&v, -2) == VIEW_OK && view_jump(&v, 2) == VIEW_OK && strcmp(page_name(&v), "Index") == 0);
	CHECK(view_jump(&v, 0) == VIEW_OK && strcmp(page_name(&v), "Index") == 0);

	/* another move forgets the way forward */
	CHECK(view_back(&v) == VIEW_OK && v.nfwd == 1);
	CHECK(view_relative(&v, VIEW_HOME) == VIEW_OK && v.nfwd == 0 && !view_can(&v, VIEW_FORWARD));

	/* across hypertexts, and where it scrolled to */
	CHECK(view_open(&v, fx("basic.hyp"), "Second", 2) == VIEW_OK && v.nfwd == 0);
	CHECK(view_back(&v) == VIEW_OK && ends_with(v.doc->path, "other.hyp"));
	CHECK(view_forward(&v) == VIEW_OK && ends_with(v.doc->path, "basic.hyp"));
	CHECK(strcmp(page_name(&v), "Second") == 0 && v.top == 2);
	CHECK(view_jump_title(&v, -1) != NULL && view_jump_title(&v, -1)[0] != '\0');
	view_free(&v);
	CHECK(v.nfwd == 0 && v.fwd == NULL);
}

/* "See also": the page's @xref entries */
static void test_xref(void)
{
	ehv_view v;
	char label[80];

	view_init(&v, NULL);
	CHECK(view_xref_count(&v) == 0 && !view_can(&v, VIEW_SEEALSO));
	CHECK(view_open(&v, fx("basic.hyp"), NULL, 0) == VIEW_OK);
	CHECK(view_xref_count(&v) == 2 && view_can(&v, VIEW_SEEALSO));
	view_xref_label(&v, 0, label, sizeof(label));
	CHECK(strcmp(label, "The help page") == 0);
	view_xref_label(&v, 1, label, sizeof(label));
	CHECK(strstr(label, "other.hyp") != NULL);                          /* no text: the target */
	view_xref_label(&v, 9, label, sizeof(label));
	CHECK(label[0] == '\0');
	CHECK(view_xref_follow(&v, 9) == VIEW_ERROR && strcmp(page_name(&v), "Main") == 0);
	CHECK(view_xref_follow(&v, 0) == VIEW_OK && strcmp(page_name(&v), "Help") == 0);
	CHECK(v.nhist == 1 && !view_can(&v, VIEW_SEEALSO));
	CHECK(view_back(&v) == VIEW_OK && view_xref_follow(&v, 1) == VIEW_OK);
	CHECK(ends_with(v.doc->path, "other.hyp"));
	view_free(&v);
}

static void test_open_errors(void)
{
	ehv_view v;

	view_init(&v, NULL);
	CHECK(view_open(&v, fx("no-such.hyp"), NULL, 0) == VIEW_ERROR && v.status[0] != '\0');
	CHECK(!view_has_page(&v));
	CHECK(view_open(&v, fx("basic.ref"), NULL, 0) == VIEW_ERROR);
	CHECK(view_open(&v, fx("basic.hyp"), "NoSuchPage", 0) == VIEW_OK);
	CHECK(strcmp(page_name(&v), "Main") == 0 && strstr(v.status, "NoSuchPage") != NULL);
	CHECK(view_open(&v, fx("basic.hyp"), "second", 99) == VIEW_OK);    /* ignoring case */
	CHECK(strcmp(page_name(&v), "Second") == 0 && v.top == 3);          /* clamped */
	view_free(&v);
}

static void test_selection(void)
{
	ehv_view v;
	long line, first;

	view_init(&v, NULL);
	view_open(&v, fx("basic.hyp"), NULL, 0);
	/* links on Main: lines 2 (two), 3, 4, 8 */
	line = view_select_link(&v, 1, 0, 8);
	CHECK(line == 2 && strncmp(v.page->runs[v.sel].text, "to Second", 9) == 0);
	first = v.sel;
	CHECK(view_select_link(&v, 1, 0, 8) == 2);
	CHECK(view_select_link(&v, 1, 0, 8) == 3);
	CHECK(view_select_link(&v, 1, 0, 8) == 4);
	CHECK(view_select_link(&v, 1, 0, 8) == 8);
	CHECK(view_select_link(&v, 1, 0, 8) == -1 && v.page->runs[v.sel].len == 6);  /* stays on last */
	CHECK(view_select_link(&v, -1, 0, 8) == 4);
	/* a selection scrolled out of view: start from the visible part */
	CHECK(view_select_link(&v, 1, 6, 8) == 8);
	CHECK(view_select_link(&v, -1, 0, 1) == -1);
	v.sel = -1;
	CHECK(view_select_link(&v, -1, 0, 8) == 8);
	v.sel = first;
	CHECK(view_follow_selected(&v) == VIEW_OK && strcmp(page_name(&v), "Second") == 0 && v.sel == -1);
	CHECK(view_follow_selected(&v) == VIEW_ERROR);

	/* hit testing: "Links: " is 7 columns, then "to Second" */
	view_back(&v);
	CHECK(view_link_at(&v, 2, 6) == -1);
	CHECK(view_link_at(&v, 2, 7) == first && view_link_at(&v, 2, 15) == first);
	CHECK(view_link_at(&v, 2, 16) == -1);
	CHECK(view_link_at(&v, 0, 0) == -1 && view_link_at(&v, 99, 0) == -1);
	CHECK(view_run_line(&v, first) == 2);
	{
		/* the page as text (^C): a line each, the link as its text */
		char text[2000], small[6];
		unsigned long n = view_page_text(&v, text, sizeof(text)), lines = 0, i;

		for (i = 0; i < n; i++)
			lines += text[i] == '\n';
		CHECK(n == strlen(text) && lines == v.page->nlines);
		CHECK(strstr(text, "Links: to Second") != NULL);
		CHECK(view_page_text(&v, small, sizeof(small)) == 5 && strlen(small) == 5);
	}
	view_free(&v);
}

static void test_request(void)
{
	const char *roots[1];
	ehv_index *idx;
	ehv_view v;

	roots[0] = fixdir;
	idx = ehv_scan_all(roots, 1, NULL, NULL);
	CHECK(idx != NULL);
	view_init(&v, idx);
	CHECK(view_request(&v, "*:\\OTHER.HYP Detail") == VIEW_OK);
	CHECK(ends_with(v.doc->path, "other.hyp") && strcmp(page_name(&v), "Detail") == 0);
	CHECK(view_request(&v, "Zweite") == VIEW_OK && strcmp(page_name(&v), "Second") == 0);
	CHECK(view_request(&v, "Secnd") == VIEW_ERROR);
	CHECK(strstr(v.status, "did you mean") != NULL && strstr(v.status, "Second") != NULL);
	CHECK(strcmp(page_name(&v), "Second") == 0);          /* unchanged */
	CHECK(view_request(&v, "*:\\OTHER.HYP Strat") == VIEW_OK);
	CHECK(strcmp(page_name(&v), "Start") == 0 && strstr(v.status, "Strat") != NULL);
	view_free(&v);
	ehv_index_free(idx);
}

int main(int argc, char **argv)
{
	fixdir = argc > 1 ? argv[1] : "tests/fixtures";
	test_open_and_links();
	test_popup();
	test_relative();
	test_forward();
	test_xref();
	test_open_errors();
	test_selection();
	test_request();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
