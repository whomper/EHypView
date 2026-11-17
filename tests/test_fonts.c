/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView tests for the font setting (src/view/fontpref.c).
 *
 *   test_fonts
 */
#include <stdio.h>
#include <stdlib.h>
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

	/* the new form, FONT=name,points (D43); a POINT= beside it is not read */
	fontpref_parse(&p, "FONT=Monaco,9\nPOINT=12\n");
	CHECK(strcmp(p.name, "Monaco") == 0 && p.point == 9);
	fontpref_parse(&p, "FONT=System font,0\n");
	CHECK(fontpref_is_system(&p) && p.point == 0);
	fontpref_parse(&p, "FONT=Swiss, bold,12\n");     /* a comma in the name: the last one splits */
	CHECK(strcmp(p.name, "Swiss, bold") == 0 && p.point == 12);
	fontpref_parse(&p, "FONT=Monaco,900\n");          /* no size after the comma: all a name */
	CHECK(strcmp(p.name, "Monaco,900") == 0 && p.point == 0);
	fontpref_parse(&p, "FONT=,12\n");
	CHECK(fontpref_is_system(&p));

	/* a round trip */
	strcpy(p.name, "Courier");
	p.point = 10;
	CHECK(fontpref_value(&p, buf, sizeof(buf)) == strlen("Courier,10"));
	CHECK(strcmp(buf, "Courier,10") == 0);
	{
		char line[96];

		snprintf(line, sizeof(line), "FONT=%s\r\n", buf);
		fontpref_parse(&p, line);
	}
	CHECK(strcmp(p.name, "Courier") == 0 && p.point == 10);
	CHECK(fontpref_value(&p, buf, 5) == 0);
}

/* the old keys read once and written in the new forms (6.1, D44) */
static void test_upgrade(void)
{
	char *t;
	font_pref p;

	/* nothing old: nothing to write */
	CHECK(fontpref_upgrade("FONT=Monaco,9\r\nPANES=20\r\n") == NULL);
	CHECK(fontpref_upgrade(NULL) == NULL);
	/* FONT= and POINT=: one key; PANECOLS=: PANES=; the rest kept */
	t = fontpref_upgrade("; the font\r\nFONT=Monaco\r\n; its size\r\nPOINT=9\r\n"
			     "START=now\r\nPANECOLS=22\r\n");
	CHECK(t != NULL && strstr(t, "FONT=Monaco,9\r\n") != NULL && strstr(t, "POINT=") == NULL &&
	      strstr(t, "PANES=22\r\n") != NULL && strstr(t, "PANECOLS=") == NULL &&
	      strstr(t, "START=now\r\n") != NULL && strstr(t, "its size") == NULL);
	fontpref_parse(&p, t);
	CHECK(strcmp(p.name, "Monaco") == 0 && p.point == 9);
	CHECK(t != NULL && fontpref_upgrade(t) == NULL);  /* once */
	free(t);
	/* the new key there already wins; the old one only goes */
	t = fontpref_upgrade("FONT=Courier,10\r\nPOINT=9\r\nPANES=30\r\nPANECOLS=22\r\n");
	CHECK(t != NULL && strstr(t, "FONT=Courier,10\r\n") != NULL && strstr(t, "POINT=") == NULL &&
	      strstr(t, "PANES=30\r\n") != NULL && strstr(t, "PANECOLS=") == NULL);
	free(t);
	/* a bad size: the usual one; POINT= with no FONT=: dropped */
	t = fontpref_upgrade("FONT=Monaco\r\nPOINT=x\r\n");
	CHECK(t != NULL && strstr(t, "FONT=Monaco,0\r\n") != NULL);
	free(t);
	t = fontpref_upgrade("POINT=12\r\n");
	CHECK(t != NULL && strstr(t, "POINT") == NULL && strstr(t, "FONT") == NULL);
	free(t);
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
	test_upgrade();
	test_sizes();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
