/*
 * EHypView - the menu bar, built in code (no resource file).
 *
 * The tree has the layout the AES expects:
 *
 *   0 root (IBOX, whole screen)
 *   1   the bar (BOX)
 *   2     the active area (IBOX) with the titles (TITLE)
 *   n   the screen (IBOX) with one drop-down (BOX) per title,
 *         each holding its items (STRING)
 *
 * The first drop-down is the Desk menu: an About item, a separator and
 * six slots the AES fills with the accessories' names.
 *
 * Copyright (C) the EHypView authors.  Free software under the GNU
 * General Public License, version 2 or (at your option) any later
 * version; see COPYING.
 */
#include <stdlib.h>
#include <string.h>
#include "menu.h"

#define G_IBOX_SPEC 0x00000000L
#define BAR_SPEC    0x00001100L     /* no frame, black on white */
#define DROP_SPEC   0x00FF1100L     /* one pixel frame */
#define ITEM_MAX    40              /* longest item, in characters */

typedef struct {
	const char *text;               /* NULL ends a menu; "-" a separator */
	const char *key;                /* shortcut shown on the right */
	int cmd;
} item_def;

typedef struct {
	const char *title;
	const item_def *items;
} title_def;

static const item_def desk_items[] = {
	{ "About EHypView...", NULL, CMD_ABOUT },
	{ "-", NULL, CMD_NONE },
	{ "", NULL, CMD_NONE }, { "", NULL, CMD_NONE }, { "", NULL, CMD_NONE },
	{ "", NULL, CMD_NONE }, { "", NULL, CMD_NONE }, { "", NULL, CMD_NONE },
	{ NULL, NULL, CMD_NONE }
};

static const item_def file_items[] = {
	{ "Open hypertext...", "^O", CMD_OPEN },
	{ "Library and search", "^L", CMD_LIBRARY },
	{ "-", NULL, CMD_NONE },
	{ "Close window", "^W", CMD_CLOSE },
	{ "-", NULL, CMD_NONE },
	{ "Quit", "^Q", CMD_QUIT },
	{ NULL, NULL, CMD_NONE }
};

static const item_def nav_items[] = {
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

static const title_def titles[] = {
	{ " Desk ", desk_items },
	{ " File ", file_items },
	{ " Navigate ", nav_items },
	{ NULL, NULL }
};

/* the accessory slots must be writable: the AES fills them in */
static char acc_slot[6][24];

static short count_items(const item_def *it)
{
	short n = 0;

	while (it[n].text != NULL)
		n++;
	return n;
}

static void set(OBJECT *o, short next, short head, short tail, unsigned short type,
                long spec, short x, short y, short w, short h)
{
	o->ob_next = next;
	o->ob_head = head;
	o->ob_tail = tail;
	o->ob_type = type;
	o->ob_flags = 0;
	o->ob_state = 0;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
}

/* item texts: "  Text      Key " padded to the drop-down's width */
static char *item_text(char *pool, const item_def *it, short width)
{
	short n, tl, kl;

	if (strcmp(it->text, "-") == 0)
	{
		for (n = 0; n < width; n++)
			pool[n] = '-';
		pool[width] = '\0';
		return pool;
	}
	memset(pool, ' ', (size_t) width);
	pool[width] = '\0';
	tl = (short) strlen(it->text);
	kl = it->key ? (short) strlen(it->key) : 0;
	memcpy(pool + 2, it->text, (size_t) tl);
	if (kl > 0)
		memcpy(pool + width - kl - 1, it->key, (size_t) kl);
	return pool;
}

int menu_build(ehv_menu *m, short cw, short ch, short screen_w, short screen_h)
{
	short nt = 0, total_items = 0, t, i, obj, bar_h = (short) (ch + 3);
	short first_title, screen_obj, first_drop, x;
	size_t pool_size = 0;
	char *pool;
	OBJECT *tree;

	memset(m, 0, sizeof(*m));
	while (titles[nt].title != NULL)
	{
		total_items = (short) (total_items + count_items(titles[nt].items));
		nt++;
	}
	m->ntitles = nt;
	m->nobjects = (short) (3 + nt + 1 + nt + total_items);
	if (m->nobjects > (short) (sizeof(m->cmd_of) / sizeof(m->cmd_of[0])))
		return -1;

	/* room for every padded item text */
	for (t = 0; t < nt; t++)
		pool_size += (size_t) count_items(titles[t].items) * (ITEM_MAX + 1);
	tree = calloc((size_t) m->nobjects, sizeof(OBJECT));
	pool = calloc(1, pool_size + 1);
	if (tree == NULL || pool == NULL)
	{
		free(tree);
		free(pool);
		return -1;
	}
	m->tree = tree;
	m->pool = pool;

	first_title = 3;
	screen_obj = (short) (first_title + nt);
	first_drop = (short) (screen_obj + 1);

	set(&tree[0], -1, 1, screen_obj, G_IBOX, G_IBOX_SPEC, 0, 0, screen_w, screen_h);
	set(&tree[1], screen_obj, 2, 2, G_BOX, BAR_SPEC, 0, 0, screen_w, bar_h);

	/* titles */
	x = 0;
	for (t = 0; t < nt; t++)
	{
		short w = (short) (strlen(titles[t].title) * cw);

		set(&tree[first_title + t], (short) (t + 1 < nt ? first_title + t + 1 : 2), -1, -1, G_TITLE,
			(long) titles[t].title, x, 0, w, bar_h);
		x = (short) (x + w);
	}
	set(&tree[2], 1, first_title, (short) (first_title + nt - 1), G_IBOX, G_IBOX_SPEC,
		(short) (2 * cw), 0, x, (short) (bar_h - 1));

	/* drop-downs */
	obj = first_drop;
	{
		short last_drop = -1;
		char *p = pool;

		for (t = 0; t < nt; t++)
		{
			const item_def *it = titles[t].items;
			short n = count_items(it), width = 0, drop = obj, dx, dw;

			for (i = 0; i < n; i++)
			{
				short need = (short) (strlen(it[i].text) + 4 + (it[i].key ? strlen(it[i].key) + 2 : 0));

				if (need > width)
					width = need;
			}
			if (t == 0 && width < 22)
				width = 22;             /* room for accessory names */
			dw = (short) (width * cw);
			dx = (short) (tree[2].ob_x + tree[first_title + t].ob_x);
			if (dx + dw > screen_w)
				dx = (short) (screen_w - dw);
			if (dx < 0)
				dx = 0;
			set(&tree[drop], -1, (short) (drop + 1), (short) (drop + n), G_BOX, DROP_SPEC,
				dx, 0, dw, (short) (n * ch));
			if (last_drop >= 0)
				tree[last_drop].ob_next = drop;
			last_drop = drop;
			for (i = 0; i < n; i++)
			{
				short o = (short) (drop + 1 + i);
				long spec;

				if (t == 0 && i >= 2)
				{
					/* accessory slot */
					short len = width < (short) sizeof(acc_slot[0]) ? width : (short) (sizeof(acc_slot[0]) - 1);

					memset(acc_slot[i - 2], ' ', (size_t) len);
					acc_slot[i - 2][len] = '\0';
					spec = (long) acc_slot[i - 2];
				} else
				{
					spec = (long) item_text(p, &it[i], width);
					p += width + 1;
				}
				set(&tree[o], (short) (i + 1 < n ? o + 1 : drop), -1, -1, G_STRING, spec,
					0, (short) (i * ch), dw, ch);
				if (strcmp(it[i].text, "-") == 0)
					tree[o].ob_state = DISABLED;
				m->cmd_of[o] = (short) it[i].cmd;
			}
			obj = (short) (drop + 1 + n);
		}
		tree[last_drop].ob_next = screen_obj;
		set(&tree[screen_obj], 0, first_drop, last_drop, G_IBOX, G_IBOX_SPEC,
			0, bar_h, screen_w, (short) (screen_h - bar_h));
	}
	tree[m->nobjects - 1].ob_flags |= LASTOB;
	return 0;
}

void menu_free(ehv_menu *m)
{
	free(m->pool);
	free(m->tree);
	memset(m, 0, sizeof(*m));
}

short menu_object(const ehv_menu *m, int cmd)
{
	short i;

	if (cmd == CMD_NONE)
		return -1;
	for (i = 0; i < m->nobjects; i++)
		if (m->cmd_of[i] == cmd)
			return i;
	return -1;
}

int menu_command(const ehv_menu *m, short item)
{
	if (item < 0 || item >= m->nobjects)
		return CMD_NONE;
	return m->cmd_of[item];
}

const char *menu_command_name(int cmd)
{
	static const char *const names[CMD_COUNT] = {
		"none", "about", "open", "library", "close", "quit",
		"back", "home", "index", "toc", "help", "prev", "next"
	};

	return cmd > CMD_NONE && cmd < CMD_COUNT ? names[cmd] : "none";
}
