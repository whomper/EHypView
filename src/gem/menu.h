/*
 * EHypView - the menu bar, built in code (no resource file).
 *
 * menu_build() only fills in an OBJECT tree; it makes no AES calls, so
 * tests/test_menu.c checks the tree on the host.
 *
 * Copyright (C) the EHypView authors.  Free software under the GNU
 * General Public License, version 2 or (at your option) any later
 * version; see COPYING.
 */
#ifndef EHV_MENU_H
#define EHV_MENU_H

#include "gem.h"

/* commands */
enum {
	CMD_NONE = 0,
	CMD_ABOUT,
	CMD_OPEN,
	CMD_LIBRARY,
	CMD_CLOSE,
	CMD_QUIT,
	CMD_BACK,
	CMD_HOME,
	CMD_INDEX,
	CMD_TOC,
	CMD_HELP,
	CMD_PREV,
	CMD_NEXT,
	CMD_FONT,
	CMD_LARGER,
	CMD_SMALLER,
	CMD_COUNT
};

typedef struct {
	OBJECT *tree;
	char *pool;                /* the item texts */
	short nobjects;
	short ntitles;
	short cmd_of[96];          /* command of each object, CMD_NONE */
} ehv_menu;

/*
 * Build the menu tree for character cells cw x ch on a screen of
 * screen_w x screen_h pixels.  Returns 0, or -1 when out of memory.
 */
int menu_build(ehv_menu *m, short cw, short ch, short screen_w, short screen_h);
void menu_free(ehv_menu *m);

/* the object of a command, or -1 */
short menu_object(const ehv_menu *m, int cmd);

/* the command of a selected item */
int menu_command(const ehv_menu *m, short item);

/* "open", "next", ... for the log */
const char *menu_command_name(int cmd);

#endif
