/*
 * EHypView - the text font setting.
 *
 * Copyright (C) the EHypView authors.  Free software under the GNU
 * General Public License, version 2 or (at your option) any later
 * version; see COPYING.
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

void fontpref_parse(font_pref *p, const char *text)
{
	inf_file *inf;
	const char *v;
	int err;

	fontpref_default(p);
	if (text == NULL || (inf = inf_parse(text, &err)) == NULL)
		return;
	v = inf_get(inf, "FONT");
	if (v != NULL && v[0] != '\0' && strlen(v) < sizeof(p->name))
	{
		strcpy(p->name, v);
		v = inf_get(inf, "POINT");
		if (v != NULL)
		{
			char *end;
			long pt = strtol(v, &end, 10);

			if (end != v && *end == '\0' && pt > 0 && pt <= 200)
				p->point = (short) pt;
		}
	}
	inf_free(inf);
}

unsigned long fontpref_format(const font_pref *p, char *buf, unsigned long size)
{
	int n = snprintf(buf, size, "FONT=%s\r\nPOINT=%d\r\n", p->name, p->point);

	return n < 0 || (unsigned long) n >= size ? 0 : (unsigned long) n;
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
