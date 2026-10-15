/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView tests for the font setting (src/view/fontpref.c).
 *
 *   test_fonts
 */
#include <stdio.h>
#include <string.h>
#include "fontpref.h"

static int failures, checks;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

static void test_parse(void)
{
	font_pref p;
	char buf[80];

	fontpref_default(&p);
	CHECK(fontpref_is_system(&p) && p.point == 0);

	fontpref_parse(&p, "FONT=Courier 10 Pitch\r\nPOINT=12\r\n");
	CHECK(strcmp(p.name, "Courier 10 Pitch") == 0 && p.point == 12 && !fontpref_is_system(&p));
	/* keys in any case, blanks, other settings and comments */
	fontpref_parse(&p, "; EHypView\nOTHER=1\n point = 9 \n font = Monaco \n");
	CHECK(strcmp(p.name, "Monaco") == 0 && p.point == 9);
	/* bad sizes keep the font, at the default size */
	fontpref_parse(&p, "FONT=Monaco\nPOINT=x9\n");
	CHECK(strcmp(p.name, "Monaco") == 0 && p.point == 0);
	fontpref_parse(&p, "FONT=Monaco\nPOINT=0\n");
	CHECK(p.point == 0);
	fontpref_parse(&p, "FONT=Monaco\nPOINT=900\n");
	CHECK(p.point == 0);
	/* no font, an empty one or a name too long: the system font */
	fontpref_parse(&p, "POINT=12\n");
	CHECK(fontpref_is_system(&p) && p.point == 0);
	fontpref_parse(&p, "FONT=\nPOINT=12\n");
	CHECK(fontpref_is_system(&p));
	fontpref_parse(&p, "FONT=A font name that is far too long for VDI\n");
	CHECK(fontpref_is_system(&p));
	fontpref_parse(&p, NULL);
	CHECK(fontpref_is_system(&p));

	/* a round trip */
	strcpy(p.name, "Courier");
	p.point = 10;
	CHECK(fontpref_format(&p, buf, sizeof(buf)) == strlen("FONT=Courier\r\nPOINT=10\r\n"));
	CHECK(strcmp(buf, "FONT=Courier\r\nPOINT=10\r\n") == 0);
	fontpref_parse(&p, buf);
	CHECK(strcmp(p.name, "Courier") == 0 && p.point == 10);
	CHECK(fontpref_format(&p, buf, 10) == 0);
}

static void test_sizes(void)
{
	static const short sys[] = { 8, 16 };
	static const short gdos[] = { 8, 9, 10, 12, 14, 18 };

	CHECK(fontpref_pick(sys, 2, 0) == 0);           /* below all: the smallest */
	CHECK(fontpref_pick(sys, 2, 8) == 0);
	CHECK(fontpref_pick(sys, 2, 12) == 0);
	CHECK(fontpref_pick(sys, 2, 16) == 1);
	CHECK(fontpref_pick(gdos, 6, 11) == 2);         /* 10 pt */
	CHECK(fontpref_pick(gdos, 6, 40) == 5);
	CHECK(fontpref_pick(gdos, 0, 10) == -1);

	CHECK(fontpref_step(sys, 2, 8, 1) == 1);
	CHECK(fontpref_step(sys, 2, 16, 1) == -1);
	CHECK(fontpref_step(sys, 2, 16, -1) == 0);
	CHECK(fontpref_step(sys, 2, 8, -1) == -1);
	CHECK(fontpref_step(gdos, 6, 11, 1) == 3);      /* between sizes */
	CHECK(fontpref_step(gdos, 6, 11, -1) == 2);
	CHECK(fontpref_step(gdos, 0, 11, 1) == -1);
}

int main(void)
{
	test_parse();
	test_sizes();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
