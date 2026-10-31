/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the tab strip's layout (tabs.h).
 */
#include "tabs.h"

short tabs_height(short ch)
{
	return (short) (ch + 5);
}

void tabs_layout(tabstrip *s, short x, short y, short w, short cw, short ch, int n, const short *len)
{
	long want = 0, share;
	short at;
	int i;

	if (n > TABS_MAX)
		n = TABS_MAX;
	if (n < 0)
		n = 0;
	s->x = x;
	s->y = y;
	s->w = w;
	s->h = tabs_height(ch);
	s->n = (short) n;
	/* a character's room on either side of the title */
	for (i = 0; i < n; i++)
		want += (long) ((len[i] < TABS_TITLE ? len[i] : TABS_TITLE) + 2) * cw;
	share = n > 0 ? (long) w / n : 0;
	at = x;
	for (i = 0; i < n; i++)
	{
		short room, l = len[i] < TABS_TITLE ? len[i] : TABS_TITLE;

		s->tx[i] = at;
		s->tw[i] = (short) (want <= w ? (l + 2) * cw : share);
		room = (short) (s->tw[i] / cw - 2);
		if (room < 0)
			room = 0;
		/* cut: "..." after what still fits, when the dots leave a
		   letter or more; else just what fits */
		if (len[i] <= room)
			s->shown[i] = len[i], s->dots[i] = 0;
		else if (room >= 4)
			s->shown[i] = (short) (room - 3), s->dots[i] = 1;
		else
			s->shown[i] = room, s->dots[i] = 2;
		at = (short) (at + s->tw[i]);
	}
}

int tabs_hit(const tabstrip *s, short mx, short my)
{
	int i;

	if (my < s->y || my >= s->y + s->h)
		return -1;
	for (i = 0; i < s->n; i++)
		if (mx >= s->tx[i] && mx < s->tx[i] + s->tw[i])
			return i;
	return -1;
}

int tabs_cut(const tabstrip *s, int i)
{
	return i >= 0 && i < s->n && s->dots[i] != 0;
}
