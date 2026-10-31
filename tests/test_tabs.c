/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView tests for the tab strip's layout (src/gem/tabs.c).
 */
#include <stdio.h>
#include "tabs.h"

static int failures, checks;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

int main(void)
{
	tabstrip s;
	short len[TABS_MAX] = { 4, 30, 10, 10, 10, 10, 10, 10 };
	int i;

	/* two that fit: each its title and a character either side, the
	   long one at TABS_TITLE, cut */
	tabs_layout(&s, 10, 50, 600, 8, 16, 2, len);
	CHECK(s.n == 2 && s.h == tabs_height(16) && s.y == 50);
	CHECK(s.tx[0] == 10 && s.tw[0] == 6 * 8 && s.shown[0] == 4 && !tabs_cut(&s, 0));
	CHECK(s.tx[1] == 10 + 48 && s.tw[1] == (TABS_TITLE + 2) * 8);
	CHECK(tabs_cut(&s, 1) && s.dots[1] == 1 && s.shown[1] == TABS_TITLE - 3);
	CHECK(tabs_hit(&s, 10, 50) == 0 && tabs_hit(&s, 57, 60) == 0 && tabs_hit(&s, 58, 60) == 1);
	CHECK(tabs_hit(&s, 58, 49) == -1 && tabs_hit(&s, 58, 50 + s.h) == -1);
	CHECK(tabs_hit(&s, 10 + 48 + 208, 55) == -1);       /* right of the last */

	/* eight on a 320-wide strip: an equal share each, the titles cut */
	tabs_layout(&s, 0, 0, 320, 8, 8, 8, len);
	for (i = 0; i < 8; i++)
		CHECK(s.tw[i] == 40 && s.tx[i] == i * 40);
	CHECK(s.shown[0] == 3 && s.dots[0] == 2);           /* 3 characters' room: no dots */
	CHECK(tabs_cut(&s, 0) && tabs_cut(&s, 2));
	CHECK(tabs_hit(&s, 319, 4) == 7);

	/* more than TABS_MAX: TABS_MAX; none: none */
	tabs_layout(&s, 0, 0, 640, 8, 16, 12, len);
	CHECK(s.n == TABS_MAX);
	tabs_layout(&s, 0, 0, 640, 8, 16, 0, len);
	CHECK(s.n == 0 && tabs_hit(&s, 5, 5) == -1 && !tabs_cut(&s, 0));

	/* four in 400: the share (100) leaves 10 characters: 7 and dots */
	len[0] = 30;
	tabs_layout(&s, 0, 0, 400, 8, 16, 4, len);
	CHECK(s.tw[0] == 100 && s.shown[0] == 7 && s.dots[0] == 1);
	CHECK(s.shown[1] == 7 && s.shown[2] == 10 && !tabs_cut(&s, 2));
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
