/*
 * EHypView tests for the menu tree (src/gem/menu.c): the AES walks it
 * by ob_next/ob_head/ob_tail, so a broken link hangs the machine.
 *
 *   test_menu
 */
#include <stdio.h>
#include <string.h>
#include "menu.h"

static int failures, checks;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

static int visits[200];

/* walk the children of `parent` the way the AES does */
static int walk(const OBJECT *t, short n, short parent, int depth)
{
	short c;
	int count = 0;

	if (depth > 4)
		return -1000;
	if (t[parent].ob_head == -1)
		return t[parent].ob_tail == -1 ? 0 : -1000;
	for (c = t[parent].ob_head; c != parent; c = t[c].ob_next)
	{
		int sub;

		if (c < 0 || c >= n || count > n)
			return -1000;
		visits[c]++;
		sub = walk(t, n, c, depth + 1);
		if (sub < 0)
			return sub;
		count += 1 + sub;
		if (t[c].ob_next == parent && t[parent].ob_tail != c)
			return -1000;
	}
	return count;
}

static const char *text(const OBJECT *o)
{
	return (const char *) o->ob_spec;
}

static int ends_with(const char *s, const char *end)
{
	size_t n = strlen(s), m = strlen(end);

	return n >= m && strcmp(s + n - m, end) == 0;
}

static void test_tree(short cw, short ch, short sw, short sh)
{
	ehv_menu m;
	const OBJECT *t;
	short i, screen, title, drop;
	int lastobs = 0;

	CHECK(menu_build(&m, cw, ch, sw, sh) == 0);
	t = m.tree;
	CHECK(m.ntitles == 4);

	/* every object reached exactly once from the root */
	memset(visits, 0, sizeof(visits));
	CHECK(walk(t, m.nobjects, 0, 0) == m.nobjects - 1);
	for (i = 1; i < m.nobjects; i++)
		CHECK(visits[i] == 1);
	for (i = 0; i < m.nobjects; i++)
		if (t[i].ob_flags & LASTOB)
			lastobs++;
	CHECK(lastobs == 1 && (t[m.nobjects - 1].ob_flags & LASTOB));

	/* root: the bar, then the screen */
	CHECK(t[0].ob_next == -1 && t[0].ob_head == 1);
	screen = t[0].ob_tail;
	CHECK(t[1].ob_next == screen && t[1].ob_type == G_BOX && t[1].ob_head == 2);
	CHECK(t[screen].ob_type == G_IBOX && t[screen].ob_y == t[1].ob_height);
	CHECK(t[0].ob_width == sw && t[0].ob_height == sh);

	/* the titles, one drop-down each, in the same order */
	title = t[2].ob_head;
	drop = t[screen].ob_head;
	for (i = 0; i < m.ntitles; i++)
	{
		short it, n = 0, x = (short) (t[2].ob_x + t[title].ob_x);

		CHECK(t[title].ob_type == G_TITLE && t[title].ob_height == t[1].ob_height);
		CHECK(t[drop].ob_type == G_BOX);
		/* below its title when there is room, always on the screen */
		CHECK(t[drop].ob_x >= 0 && t[drop].ob_x + t[drop].ob_width <= sw);
		if (x + t[drop].ob_width <= sw)
			CHECK(t[drop].ob_x == x);
		for (it = t[drop].ob_head; it != drop; it = t[it].ob_next, n++)
		{
			CHECK(t[it].ob_type == G_STRING);
			CHECK(t[it].ob_y == n * ch && t[it].ob_width == t[drop].ob_width);
			CHECK((short) strlen(text(&t[it])) * cw <= t[drop].ob_width);
			if (text(&t[it])[0] == '-')
				CHECK(t[it].ob_state & DISABLED);
		}
		CHECK(t[drop].ob_height == n * ch);
		if (i == 0)
			CHECK(n == 8);                      /* About, separator, six accessories */
		title = t[title].ob_next;
		drop = t[drop].ob_next;
	}
	CHECK(title == 2 && drop == screen);
	CHECK(strcmp(text(&t[t[2].ob_head]), " Desk ") == 0);

	/* commands */
	for (i = CMD_ABOUT; i < CMD_COUNT; i++)
	{
		short o = menu_object(&m, i);

		CHECK(o > 0 && menu_command(&m, o) == i && t[o].ob_type == G_STRING);
		CHECK(!(t[o].ob_state & DISABLED));
	}
	CHECK(menu_object(&m, CMD_NONE) == -1);
	CHECK(strcmp(menu_command_name(CMD_OPEN), "open") == 0 && strcmp(menu_command_name(CMD_NEXT), "next") == 0);
	CHECK(strcmp(menu_command_name(CMD_COUNT), "none") == 0 && strcmp(menu_command_name(-1), "none") == 0);
	CHECK(menu_command(&m, 0) == CMD_NONE && menu_command(&m, 1) == CMD_NONE);
	CHECK(menu_command(&m, -1) == CMD_NONE && menu_command(&m, m.nobjects) == CMD_NONE);
	CHECK(strncmp(text(&t[menu_object(&m, CMD_QUIT)]), "  Quit", 6) == 0);
	CHECK(ends_with(text(&t[menu_object(&m, CMD_QUIT)]), "^Q "));
	CHECK(ends_with(text(&t[menu_object(&m, CMD_NEXT)]), "^Right "));
	CHECK(ends_with(text(&t[menu_object(&m, CMD_LARGER)]), "^+ "));
	CHECK(strcmp(menu_command_name(CMD_SMALLER), "smaller") == 0);
	CHECK(strncmp(text(&t[menu_object(&m, CMD_ABOUT)]), "  About EHypView", 16) == 0);
	menu_free(&m);
	CHECK(m.tree == NULL);
}

int main(void)
{
	test_tree(8, 16, 640, 480);                 /* Falcon VGA, TT medium */
	test_tree(8, 8, 320, 200);                  /* ST low: the menus move left */
	test_tree(8, 16, 1280, 960);                /* TT high */
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
