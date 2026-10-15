/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the viewer window's toolbar: the layout (toolbar.h).
 */
#include <string.h>
#include "toolbar.h"

/* \xF0 is the Atari character set's three bars (ECal's menu button) */
static const char *const labels[TB_COUNT] = {
	"", "", "", "<", "Contents", ">", "Index", "See also", "Library", "Bookmarks", "\xF0"
};

static const char *const tips[TB_COUNT] = {
	"Back (Undo). Right button: the history",
	"Forward (Shift+Undo). Right button: the history",
	"The main page (Control+Home)",
	"The page before (Control+Left)",
	"The contents (Control+T)",
	"The next page (Control+Right)",
	"The index (Control+I)",
	"See also: pages about this one",
	"The library: every hypertext, and search (Control+L)",
	"Bookmarks: add this page (Control+D), or go to one",
	"Menu"
};

static const char *const names[TB_COUNT] = {
	"back", "forward", "home", "prev", "toc", "next", "index", "see-also",
	"library", "bookmarks", "menu"
};

/* which go first when the bar is narrow */
static const int drop_order[] = {
	TB_SEEALSO, TB_INDEX, TB_BOOKMARKS, TB_LIBRARY, TB_HOME, TB_TOC, TB_PREV, TB_NEXT
};

/* the last button of each group: a wider gap follows */
static int group_end(int b)
{
	return b == TB_HOME || b == TB_NEXT || b == TB_SEEALSO || b == TB_BOOKMARKS;
}

short tb_height(short ch)
{
	return (short) (ch + 8);
}

const char *tb_label(int b)
{
	return b >= 0 && b < TB_COUNT ? labels[b] : "";
}

int tb_glyph(int b)
{
	switch (b)
	{
	case TB_BACK: return TB_ARROW_LEFT;
	case TB_FORWARD: return TB_ARROW_RIGHT;
	case TB_HOME: return TB_HOUSE;
	}
	return 0;
}

const char *tb_tip(int b)
{
	return b >= 0 && b < TB_COUNT ? tips[b] : "";
}

const char *tb_name(int b)
{
	return b >= 0 && b < TB_COUNT ? names[b] : "none";
}

static short button_w(int b, short cw)
{
	return (short) (tb_glyph(b) ? 3 * cw : ((short) strlen(labels[b]) + 2) * cw);
}

/* place the shown buttons from the left, the menu on the right; the
   width they take, the menu included */
static short place(toolbar *t, const int *shown, short cw)
{
	short x = (short) (t->x + 4), gap = 2;
	int b, last = -1;

	for (b = 0; b < TB_MENU; b++)
	{
		if (!shown[b])
		{
			t->bw[b] = 0;
			continue;
		}
		if (last >= 0)
			x = (short) (x + (group_end(last) ? cw - 2 : gap));
		t->bx[b] = x;
		t->bw[b] = button_w(b, cw);
		x = (short) (x + t->bw[b]);
		last = b;
	}
	t->bw[TB_MENU] = button_w(TB_MENU, cw);
	t->bx[TB_MENU] = (short) (t->x + t->w - 4 - t->bw[TB_MENU]);
	return (short) (x - t->x + cw + t->bw[TB_MENU] + 4);
}

void tb_layout(toolbar *t, short x, short y, short w, short cw, short ch)
{
	int shown[TB_COUNT], b;
	unsigned int k;

	t->x = x;
	t->y = y;
	t->w = w;
	t->h = tb_height(ch);
	t->by = (short) (y + 3);
	t->bh = (short) (ch + 2);
	for (b = 0; b < TB_COUNT; b++)
		shown[b] = 1;
	for (k = 0; place(t, shown, cw) > w && k < sizeof(drop_order) / sizeof(drop_order[0]); k++)
		shown[drop_order[k]] = 0;
}

short tb_full_width(short cw)
{
	toolbar t;
	int shown[TB_COUNT], b;

	for (b = 0; b < TB_COUNT; b++)
		shown[b] = 1;
	t.x = 0;
	t.w = 0;
	return place(&t, shown, cw);
}

int tb_hit(const toolbar *t, short mx, short my)
{
	int b;

	if (my < t->by || my >= t->by + t->bh)
		return -1;
	for (b = 0; b < TB_COUNT; b++)
		if (t->bw[b] > 0 && mx >= t->bx[b] && mx < t->bx[b] + t->bw[b])
			return b;
	return -1;
}
