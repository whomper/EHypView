/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the menu bar: its titles, items and shortcuts, built by
 * the AERA programs' shared builder (common/ui/mbar.c), which started
 * here.
 */
#include <stdio.h>
#include <string.h>
#include "menu.h"

/* the separator and the accessories' slots are added by the builder */
static const MBAR_ITEM desk_items[] = {
	{ "About EHypView...", NULL, CMD_ABOUT },
	{ NULL, NULL, CMD_NONE }
};

static const MBAR_ITEM file_items[] = {
	{ "New window", "^N", CMD_NEWWIN },
	{ "Open hypertext...", "^O", CMD_OPEN },
	{ "Library and search", "^L", CMD_LIBRARY },
	{ "-", NULL, CMD_NONE },
	{ "Close window", "^W", CMD_CLOSE },
	{ "-", NULL, CMD_NONE },
	{ "Quit", "^Q", CMD_QUIT },
	{ NULL, NULL, CMD_NONE }
};

static const MBAR_ITEM edit_items[] = {
	{ "Cut", "^X", CMD_CUT },
	{ "Copy", "^C", CMD_COPY },
	{ "Paste", "^V", CMD_PASTE },
	{ NULL, NULL, CMD_NONE }
};

/* \x01 is the Shift arrow in the Atari character set */
static const MBAR_ITEM view_items[] = {
	{ "Font...", "\x01^F", CMD_FONT },
	{ "-", NULL, CMD_NONE },
	{ "Larger text", "^+", CMD_LARGER },
	{ "Smaller text", "^-", CMD_SMALLER },
	{ NULL, NULL, CMD_NONE }
};

static const MBAR_ITEM nav_items[] = {
	{ "Back", "Undo", CMD_BACK },
	{ "Link in new window", "\x01Return", CMD_LINKWIN },
	{ "-", NULL, CMD_NONE },
	{ "Main page", "^Home", CMD_HOME },
	{ "Contents", "^T", CMD_TOC },
	{ "Index", "^I", CMD_INDEX },
	{ "Help page", "Help", CMD_HELP },
	{ "-", NULL, CMD_NONE },
	{ "Previous page", "^Left", CMD_PREV },
	{ "Next page", "^Right", CMD_NEXT },
	{ NULL, NULL, CMD_NONE }
};

/* the bookmarks themselves follow these, a line each */
#define BM_FIXED 2
#define BM_LABEL 32                /* a title shown in the menu is cut to this */
static MBAR_ITEM bm_items[BM_FIXED + 1 + BM_MAX + 1] = {
	{ "Add bookmark", "^D", CMD_BM_ADD },
	{ "Remove bookmark", NULL, CMD_BM_REMOVE }
};
static char bm_label[BM_MAX][BM_LABEL + 4];

/* the guidelines' order (3.1): Desk, File, Edit, View, then the app's */
static const MBAR_TITLE titles[] = {
	{ " Desk ", desk_items },
	{ " File ", file_items },
	{ " Edit ", edit_items },
	{ " View ", view_items },
	{ " Navigate ", nav_items },
	{ " Bookmarks ", bm_items },
	{ NULL, NULL }
};

int menu_build(ehv_menu *m, const ehv_bookmarks *bm, short cw, short ch, short screen_w, short screen_h)
{
	unsigned int i, n = BM_FIXED;

	if (bm != NULL && bm->n > 0)
	{
		bm_items[n].text = "-";
		bm_items[n].key = NULL;
		bm_items[n++].cmd = CMD_NONE;
		for (i = 0; i < bm->n && i < BM_MAX; i++)
		{
			size_t len = strlen(bm->b[i].title);

			if (len > BM_LABEL)
				sprintf(bm_label[i], "%.*s...", BM_LABEL - 3, bm->b[i].title);
			else
				strcpy(bm_label[i], bm->b[i].title);
			bm_items[n].text = bm_label[i];
			bm_items[n].key = NULL;
			bm_items[n++].cmd = CMD_BM0 + (int) i;
		}
	}
	bm_items[n].text = NULL;
	return mbar_build(m, titles, cw, ch, screen_w, screen_h);
}

void menu_free(ehv_menu *m)
{
	mbar_free(m);
}

short menu_object(const ehv_menu *m, int cmd)
{
	return mbar_object(m, cmd);
}

int menu_command(const ehv_menu *m, short item)
{
	return mbar_command(m, item);
}

const char *menu_command_name(int cmd)
{
	static const char *const names[CMD_BM0] = {
		"none", "about", "open", "library", "close", "quit",
		"back", "home", "index", "toc", "help", "prev", "next",
		"font", "larger", "smaller", "cut", "copy", "paste",
		"new-window", "link-window", "bookmark-add", "bookmark-remove"
	};

	if (cmd >= CMD_BM0 && cmd < CMD_COUNT)
		return "bookmark";
	return cmd > CMD_NONE && cmd < CMD_BM0 ? names[cmd] : "none";
}
