/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the text font setting.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fontpref.h"
#include "inf.h"

void fontpref_default(font_pref *p)
{
	strcpy(p->name, FONT_SYSTEM_NAME);
	p->point = 0;
}

int fontpref_is_system(const font_pref *p)
{
	return strcmp(p->name, FONT_SYSTEM_NAME) == 0;
}

/* a point size as the INF writes it: 0 (the usual one) to 200; -1 bad */
static int points_of(const char *v)
{
	char *end;
	long pt;

	if (v == NULL || *v < '0' || *v > '9')
		return -1;
	pt = strtol(v, &end, 10);
	return *end == '\0' && pt >= 0 && pt <= 200 ? (int) pt : -1;
}

/* FONT=name,points: 1 with the name in p and the points (when good) */
static int new_form(font_pref *p, const char *v)
{
	const char *comma = strrchr(v, ',');
	size_t n;
	int pt;

	if (comma == NULL || (pt = points_of(comma + 1)) < 0)
		return 0;
	n = (size_t) (comma - v);
	if (n == 0 || n >= sizeof(p->name))
		return 1;                      /* the new form, a bad name: the default */
	memcpy(p->name, v, n);
	p->name[n] = '\0';
	p->point = (short) pt;
	return 1;
}

void fontpref_parse(font_pref *p, const char *text)
{
	inf_file *inf;
	const char *v;
	int err;

	fontpref_default(p);
	if (text == NULL || (inf = inf_parse(text, &err)) == NULL)
		return;
	v = inf_get(inf, "FONT");
	if (v != NULL && v[0] != '\0' && !new_form(p, v) && strlen(v) < sizeof(p->name))
	{
		/* the old form, FONT=name and POINT=pt, read once (D44) */
		int pt = points_of(inf_get(inf, "POINT"));

		strcpy(p->name, v);
		if (pt > 0)
			p->point = (short) pt;
	}
	inf_free(inf);
}

unsigned long fontpref_value(const font_pref *p, char *buf, unsigned long size)
{
	int n = snprintf(buf, size, "%s,%d", p->name, p->point);

	return n < 0 || (unsigned long) n >= size ? 0 : (unsigned long) n;
}

/* text with t's key changed (value) or dropped (value 0); t is let go */
static char *change(char *t, const char *key, const char *value, const char *note)
{
	char *u;

	if (t == NULL)
		return NULL;
	u = value != NULL ? inf_put(t, key, value, note) : inf_drop(t, key);
	free(t);
	return u;
}

char *fontpref_upgrade(const char *text)
{
	inf_file *inf;
	const char *font, *point, *cols;
	char *t;
	int err;

	if (text == NULL || (inf = inf_parse(text, &err)) == NULL)
		return NULL;
	font = inf_get(inf, "FONT");
	point = inf_get(inf, "POINT");
	cols = inf_get(inf, "PANECOLS");
	if (point == NULL && cols == NULL)
	{
		inf_free(inf);
		return NULL;
	}
	t = malloc(strlen(text) + 1);
	if (t != NULL)
		strcpy(t, text);
	if (point != NULL)
	{
		font_pref p;

		if (font != NULL && font[0] != '\0' && !new_form(&p, font))
		{
			char v[FONT_NAME_MAX + 8];
			int pt = points_of(point);

			snprintf(v, sizeof(v), "%s,%d", font, pt > 0 ? pt : 0);
			t = change(t, "FONT", v, FONT_NOTE);
		}
		t = change(t, "POINT", NULL, NULL);
	}
	if (cols != NULL)
	{
		if (inf_get(inf, "PANES") == NULL)
			t = change(t, "PANES", cols, PANES_NOTE);
		t = change(t, "PANECOLS", NULL, NULL);
	}
	inf_free(inf);
	return t;
}

int fontpref_pick(const short *sizes, int n, short want)
{
	int i, best = 0;

	if (n <= 0)
		return -1;
	for (i = 0; i < n; i++)
		if (sizes[i] <= want)
			best = i;
	return best;
}

int fontpref_step(const short *sizes, int n, short cur, int dir)
{
	int i;

	if (dir > 0)
	{
		for (i = 0; i < n; i++)
			if (sizes[i] > cur)
				return i;
	} else
	{
		for (i = n - 1; i >= 0; i--)
			if (sizes[i] < cur)
				return i;
	}
	return -1;
}
