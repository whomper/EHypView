/*
 * EHypView - the menu bar: its titles, items and shortcuts, built by
 * the AERA programs' shared builder (common/ui/mbar.c), which started
 * here.
 *
 * Copyright (C) the EHypView authors.  Free software under the GNU
 * General Public License, version 2 or (at your option) any later
 * version; see COPYING.
 */
#include <stddef.h>
#include "menu.h"

/* the separator and the accessories' slots are added by the builder */
static const MBAR_ITEM desk_items[] = {
	{ "About EHypView...", NULL, CMD_ABOUT },
	{ NULL, NULL, CMD_NONE }
};

static const MBAR_ITEM file_items[] = {
	{ "Open hypertext...", "^O", CMD_OPEN },
	{ "Library and search", "^L", CMD_LIBRARY },
	{ "-", NULL, CMD_NONE },
	{ "Close window", "^W", CMD_CLOSE },
	{ "-", NULL, CMD_NONE },
	{ "Quit", "^Q", CMD_QUIT },
	{ NULL, NULL, CMD_NONE }
};

static const MBAR_ITEM nav_items[] = {
	{ "Back", "Undo", CMD_BACK },
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

/* \x01 is the Shift arrow in the Atari character set */
static const MBAR_ITEM view_items[] = {
	{ "Font...", "\x01^F", CMD_FONT },
	{ "-", NULL, CMD_NONE },
	{ "Larger text", "^+", CMD_LARGER },
	{ "Smaller text", "^-", CMD_SMALLER },
	{ NULL, NULL, CMD_NONE }
};

static const MBAR_TITLE titles[] = {
	{ " Desk ", desk_items },
	{ " File ", file_items },
	{ " Navigate ", nav_items },
	{ " View ", view_items },
	{ NULL, NULL }
};

int menu_build(ehv_menu *m, short cw, short ch, short screen_w, short screen_h)
{
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
	static const char *const names[CMD_COUNT] = {
		"none", "about", "open", "library", "close", "quit",
		"back", "home", "index", "toc", "help", "prev", "next",
		"font", "larger", "smaller"
	};

	return cmd > CMD_NONE && cmd < CMD_COUNT ? names[cmd] : "none";
}
