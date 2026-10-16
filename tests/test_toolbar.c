/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView tests for the toolbar's layout (src/gem/toolbar.c): every
 * button on a wide bar, in order and apart; the least needed going first
 * on a narrow one; clicks finding their button.
 *
 *   test_toolbar
 */
#include <stdio.h>
#include <string.h>
#include "toolbar.h"

static int failures, checks;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

/* the buttons shown don't overlap, keep left to right, and stay on the bar */
static void check_sane(const toolbar *t)
{
	int b, last = -1;

	for (b = 0; b < TB_COUNT; b++)
	{
		if (t->bw[b] == 0)
			continue;
		CHECK(t->bx[b] >= t->x && t->bx[b] + t->bw[b] <= t->x + t->w);
		if (last >= 0)
			CHECK(t->bx[b] >= t->bx[last] + t->bw[last]);
		CHECK(tb_hit(t, t->bx[b], t->by) == b);
		CHECK(tb_hit(t, (short) (t->bx[b] + t->bw[b] - 1), (short) (t->by + t->bh - 1)) == b);
		last = b;
	}
	/* Back, Forward and the menu, whatever the width */
	CHECK(t->bw[TB_BACK] > 0 && t->bw[TB_FORWARD] > 0 && t->bw[TB_MENU] > 0);
	CHECK(tb_hit(t, t->bx[TB_BACK], (short) (t->by - 1)) == -1);
	CHECK(tb_hit(t, t->bx[TB_BACK], (short) (t->by + t->bh)) == -1);
}

static int shown(const toolbar *t)
{
	int b, n = 0;

	for (b = 0; b < TB_COUNT; b++)
		n += t->bw[b] > 0;
	return n;
}

int main(void)
{
	toolbar t;
	int b, n, prev;
	short w;

	/* a viewer window on a 640-pixel screen: everything */
	tb_layout(&t, 20, 50, 608, 8, 16);
	check_sane(&t);
	CHECK(shown(&t) == TB_COUNT);
	CHECK(t.h == tb_height(16) && t.h == 24 && t.by == 53 && t.bh == 18);
	CHECK(t.bx[TB_BACK] == 24);
	CHECK(t.bx[TB_MENU] + t.bw[TB_MENU] == 20 + 608 - 4);              /* on the right */
	/* the groups: two pixels inside one, a character less two between them */
	CHECK(t.bx[TB_FORWARD] == t.bx[TB_BACK] + t.bw[TB_BACK] + 2);
	CHECK(t.bx[TB_PREV] == t.bx[TB_HOME] + t.bw[TB_HOME] + 6);
	CHECK(t.bw[TB_TOC] == 10 * 8 && t.bw[TB_BACK] == 3 * 8);
	CHECK(tb_hit(&t, (short) (t.bx[TB_FORWARD] - 1), t.by) == -1);    /* between two */
	CHECK(tb_hit(&t, 0, t.by) == -1);

	/* just wide enough for everything, and a pixel less */
	tb_layout(&t, 0, 0, tb_full_width(8), 8, 16);
	CHECK(shown(&t) == TB_COUNT);
	tb_layout(&t, 0, 0, (short) (tb_full_width(8) - 1), 8, 16);
	CHECK(shown(&t) == TB_COUNT - 1 && t.bw[TB_SEEALSO] == 0);
	CHECK(tb_full_width(8) == 566);

	/* narrower and narrower: buttons only go, in the order given */
	prev = TB_COUNT;
	for (w = 608; w >= 96; w -= 8)              /* 96: the narrowest window, 12 characters */
	{
		tb_layout(&t, 0, 0, w, 8, 16);
		check_sane(&t);
		n = shown(&t);
		CHECK(n <= prev);
		prev = n;
		if (t.bw[TB_SEEALSO] > 0)
			CHECK(t.bw[TB_INDEX] > 0 && t.bw[TB_LIBRARY] > 0 && t.bw[TB_HOME] > 0);
		if (t.bw[TB_HOME] > 0)
			CHECK(t.bw[TB_TOC] > 0 && t.bw[TB_PREV] > 0);
	}
	CHECK(prev == 3);                                                   /* back, forward, menu */
	tb_layout(&t, 0, 0, 400, 8, 16);
	CHECK(t.bw[TB_SEEALSO] == 0 && t.bw[TB_INDEX] == 0 && t.bw[TB_TOC] > 0);

	/* the ST's 8x8 font: the same, in a smaller bar */
	tb_layout(&t, 0, 0, 608, 8, 8);
	check_sane(&t);
	CHECK(t.h == 16 && t.bh == 10);

	/* names, labels, tips */
	for (b = 0; b < TB_COUNT; b++)
	{
		CHECK(tb_tip(b)[0] != '\0' && strcmp(tb_name(b), "none") != 0);
		CHECK((tb_glyph(b) != 0) == (tb_label(b)[0] == '\0'));
	}
	CHECK(strcmp(tb_name(TB_SEEALSO), "see-also") == 0 && strcmp(tb_label(TB_TOC), "Contents") == 0);
	CHECK(strcmp(tb_name(-1), "none") == 0 && strcmp(tb_name(TB_COUNT), "none") == 0);
	CHECK(tb_label(TB_COUNT)[0] == '\0' && tb_glyph(TB_INDEX) == 0 && tb_glyph(TB_HOME) == TB_HOUSE);

	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
