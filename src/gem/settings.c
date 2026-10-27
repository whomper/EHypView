/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the Settings dialog (^,, the AERA guidelines 6.1): what
 * EHYPVIEW.INF holds, as EMail's Settings lays it out: a black title band,
 * the pages on the left (the chosen one in a black bar) and that page's
 * settings on the right. Built in code, run by the shared dialog loop
 * (common/ui/dialog.c), so Undo is Cancel, and Esc too where no field
 * has the cursor; in a folder field Esc empties it (3.5). The dialogs
 * are 3D wherever the AES draws 3D: there is no setting for it (5.6).
 *
 *   General   START=: when the accessory looks through the library;
 *             FOLDER=, shown: where the index is kept
 *   Library   LIBRARY=: its layout, Outline or Two panes;
 *             PATHS=: folders to look in as well as ST-GUIDE.INF's
 *
 * The font is View > Font... (⇧^F), as in ECal and EMail, and the
 * bookmarks are kept from the Bookmarks menu and the library.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gem.h"
#include "gemapp.h"
#include "settings.h"
#include "grect.h"
#include "dialog.h"
#include "popup.h"
#include "alert.h"
#include "inf.h"
#include "plat.h"
#include "bgwork.h"
#include "charset.h"
#include "field.h"

#define DMAX 32
#define TMAX 8
#define NPATH 3                    /* folder fields on the Library page */
#define PLEN 40                    /* characters a folder field shows */
#define PCAP 128                   /* and holds (5.4): a GEMDOS path */

#define W 57                       /* the dialog, in characters */
#define H 18
#define SW 13                      /* the list of pages */
#define SX (SW + 2)                /* where a page's settings start */

static OBJECT tree[DMAX];
static TEDINFO ted[TMAX];
static short nobj, nted, last_child;
static short page_of[DMAX], page_tab[3], cur_page;

static short d_add(short type, short flags, short state, long spec, short x, short y, short w, short h)
{
	short i = nobj++;
	OBJECT *o = &tree[i];

	o->ob_next = 0;
	o->ob_head = o->ob_tail = -1;
	o->ob_type = (unsigned short) type;
	o->ob_flags = (unsigned short) flags;
	o->ob_state = (unsigned short) state;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	if (last_child)
		tree[last_child].ob_next = i;
	else
		tree[0].ob_head = i;
	tree[0].ob_tail = i;
	last_child = i;
	return i;
}

static short d_text(short x, short y, const char *s)
{
	return d_add(G_STRING, 0, 0, (long) s, x, y, (short) strlen(s), 1);
}

static TEDINFO *d_ted(char *text, const char *tmpl, const char *valid, short color)
{
	TEDINFO *t = &ted[nted++];

	t->te_ptext = text;
	t->te_ptmplt = (char *) tmpl;
	t->te_pvalid = (char *) valid;
	t->te_font = 3;
	t->te_just = 0;
	t->te_color = color;
	t->te_thickness = 0;
	t->te_txtlen = (short) strlen(text) + 1;
	t->te_tmplen = (short) strlen(tmpl) + 1;
	return t;
}

/* a folder field in a thin frame, holding more than it shows (field.c) */
static char tmpl[PLEN + 1], valid[PLEN + 1];
static char paths[NPATH][PCAP + 1];
static LFIELD path_field[NPATH];
static short o_path[NPATH];

static short d_field(short x, short y, char *buf)
{
	TEDINFO *t;

	memset(tmpl, '_', PLEN);
	memset(valid, 'X', PLEN);
	tmpl[PLEN] = valid[PLEN] = '\0';
	t = d_ted(buf, tmpl, valid, 0x1180);
	t->te_txtlen = PLEN + 1;
	t->te_thickness = -1;
	return d_add(G_FBOXTEXT, EDITABLE, 0, (long) t, x, y, PLEN, 1);
}

/* ---------------------------------------------------------------- */
/* START=, as the popup shows it (ECal's choices)                    */

static const char *const start_val[] = { "opened", "now", "60", "300" };
static const char *const start_name[] = {
	"When first opened", "At start", "After a minute", "After 5 minutes"
};
#define NSTART 4
static short start_sel;                /* NSTART: another number of seconds */
static long start_other;
static short start_was;                /* as the file had it */
static char start_label[30];

static void start_from(const char *v)
{
	long s = bg_parse(v != NULL ? v : "opened");

	start_sel = s == BG_OPENED ? 0 : s == BG_NOW ? 1 : s == 60 ? 2 : s == 300 ? 3 : NSTART;
	start_other = s;
}

/* the button's text: the choice, padded, and the system font's down arrow */
static void start_show(void)
{
	char what[24];

	if (start_sel < NSTART)
		strcpy(what, start_name[start_sel]);
	else
		sprintf(what, "After %ld s", start_other);
	sprintf(start_label, " %-21s\x02", what);
}

/* ---------------------------------------------------------------- */
/* LIBRARY=, the same way                                            */

static const char *const layout_val[] = { "outline", "panes" };
static const char *const layout_name[] = { "Outline", "Two panes" };
#define NLAYOUT 2
static short layout_sel, layout_was;
static char layout_label[30];

static void layout_show(void)
{
	sprintf(layout_label, " %-13s\x02", layout_name[layout_sel]);
}

/* ---------------------------------------------------------------- */
/* the pages                                                         */

static void show_page(short p, int draw)
{
	short i;

	cur_page = p;
	for (i = 1; i < nobj; i++)
	{
		if (!page_of[i])
			continue;
		if (page_of[i] == p)
		{
			tree[i].ob_flags &= (unsigned short) ~HIDETREE;
			if (tree[i].ob_type == G_FBOXTEXT)
				tree[i].ob_flags |= EDITABLE;
		} else
		{
			tree[i].ob_flags |= HIDETREE;
			tree[i].ob_flags &= (unsigned short) ~EDITABLE;
		}
	}
	/* the chosen page in a black bar, as the band (never SELECTED: in
	   3D the AES only inverts the grey, guidelines 5.6) */
	for (i = 1; i <= 2; i++)
		((TEDINFO *) tree[page_tab[i]].ob_spec)->te_color = i == p ? 0x1071 : 0x1180;
	if (draw)
		dlg_draw(tree, 0);
}

static short o_band, o_start, o_layout, o_ok, o_cancel;
static char folder_text[PLEN + 1];

static void build(const char *folder)
{
	static char band[] = " Settings", tab1[] = "  General", tab2[] = "  Library";
	short i, k, from, gap = (short) (sys_ch / 2);

	memset(tree, 0, sizeof(tree));
	memset(page_of, 0, sizeof(page_of));
	nobj = 1;
	nted = 0;
	last_child = 0;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = 0x00021100L;
	tree[0].ob_width = W;
	tree[0].ob_height = H;

	/* the band, the list of pages, a line between them and the page */
	o_band = d_add(G_BOXTEXT, 0, 0, (long) d_ted(band, "", "", 0x1071), 0, 0, W, 1);
	d_add(G_BOX, 0, 0, 0x00001171L, SW, 1, (short) (0 | (1 << 8)), H - 1);
	page_tab[1] = d_add(G_BOXTEXT, TOUCHEXIT, 0, (long) d_ted(tab1, "", "", 0x1180),
			    (short) (0 | (2 << 8)), 2, (short) ((SW - 1) | (6 << 8)), 1);
	page_tab[2] = d_add(G_BOXTEXT, TOUCHEXIT, 0, (long) d_ted(tab2, "", "", 0x1180),
			    (short) (0 | (2 << 8)), (short) (4 | (gap << 8)), (short) ((SW - 1) | (6 << 8)), 1);

	/* General */
	from = nobj;
	d_text(SX, 2, "General");
	d_add(G_BOX, 0, 0, 0x00001171L, SX, (short) (3 | (2 << 8)), 40, (short) (0 | (1 << 8)));
	d_text(SX, 4, "The accessory looks through the library:");
	start_show();
	o_start = d_add(G_BOXTEXT, TOUCHEXIT, 0, (long) d_ted(start_label, "", "", 0x1180),
			SX + 2, (short) (5 | (gap << 8)), 23, 1);
	((TEDINFO *) tree[o_start].ob_spec)->te_thickness = -1;
	d_text(SX, (short) (7 | (gap << 8)), "The program looks as it starts.");
	d_text(SX, (short) (9 | (gap << 8)), "EHypView's folder (FOLDER=):");
	snprintf(folder_text, sizeof(folder_text), "%s", folder);
	d_text(SX + 2, (short) (10 | (gap << 8)), folder_text);
	d_text(SX, (short) (11 | (gap << 8)), "keeps the library's index, EHYPVIEW.IDX,");
	d_text(SX, (short) (12 | (gap << 8)), "and the fonts measured, EHYPVIEW.FNT.");
	for (i = from; i < nobj; i++)
		page_of[i] = 1;

	/* Library */
	from = nobj;
	d_text(SX, 2, "Library");
	d_add(G_BOX, 0, 0, 0x00001171L, SX, (short) (3 | (2 << 8)), 40, (short) (0 | (1 << 8)));
	d_text(SX, 4, "Layout:");
	layout_show();
	o_layout = d_add(G_BOXTEXT, TOUCHEXIT, 0, (long) d_ted(layout_label, "", "", 0x1180),
			 SX + 9, 4, 15, 1);
	((TEDINFO *) tree[o_layout].ob_spec)->te_thickness = -1;
	d_text(SX, (short) (6 | (gap << 8)), "Also look for hypertexts in (PATHS=):");
	for (k = 0; k < NPATH; k++)
		o_path[k] = d_field(SX, (short) ((7 + k) | ((gap + 2 + k * (sys_ch / 4 + 2)) << 8)), paths[k]);
	d_text(SX, (short) (11 | (gap << 8)), "Each with its subfolders, as well as");
	d_text(SX, (short) (12 | (gap << 8)), "PATHS, HYPFOLD and REF in ST-GUIDE.INF,");
	d_text(SX, (short) (13 | (gap << 8)), "and \\GUIDES and \\GEMSYS\\GUIDES on every");
	d_text(SX, (short) (14 | (gap << 8)), "drive.");
	for (i = from; i < nobj; i++)
		page_of[i] = 2;

	o_cancel = d_add(G_BUTTON, SELECTABLE | EXIT, 0, (long) "Cancel", W - 24, 16, 10, 1);
	o_ok = d_add(G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, (long) "OK", W - 12, 16, 10, 1);
	tree[last_child].ob_next = 0;
	tree[nobj - 1].ob_flags |= LASTOB;
	for (i = 0; i < nobj; i++)
		rsrc_obfix(tree, i);
	dlg_3d(tree);
	for (k = 0; k < NPATH; k++)
		lf_init(&path_field[k], tree, o_path[k], paths[k], PCAP, 0);
	show_page(1, 0);
}

/* the dialog under a popup drawn again: in a window through the AES's
   rectangles (it may have been moved, guidelines 3.5) */
static void under_popup(void)
{
	if (dlg_in_window())
		dlg_draw(tree, 0);
	else if (!popup_put_back)          /* else the screen under it is back */
		objc_draw(tree, 0, 8, popup_area.x, popup_area.y, popup_area.w, popup_area.h);
}

/* where a popup goes to have its row sel over the button at y, as the
   AES's own pop-ups do (the guidelines, 5.7): popup.c's rows are a line
   and 2 pixels high, from 2 pixels down, their text a pixel in */
static short over_button(short y, short sel)
{
	return (short) (y - 3 - sel * (sys_ch + 2));
}

/* the popup of START= choices, over its button with the choice ticked;
   the dialog under it drawn again */
static void start_pick(void)
{
	static char lab[NSTART][28];
	const char *l[NSTART];
	short x, y, k, r;

	for (k = 0; k < NSTART; k++)
	{
		sprintf(lab[k], "%c %s", k == start_sel ? '\x08' : ' ', start_name[k]);
		l[k] = lab[k];
	}
	objc_offset(tree, o_start, &x, &y);
	log_event("menu", "settings-start", NULL);
	r = popup(x, over_button(y, start_sel < NSTART ? start_sel : 0), l, NSTART);
	under_popup();
	if (r < 0)
		return;
	start_sel = r;
	start_show();
	dlg_draw(tree, o_start);
	log_event("settings-start", start_val[r], NULL);
}

/* the popup of layouts, the same way */
static void layout_pick(void)
{
	static char lab[NLAYOUT][20];
	const char *l[NLAYOUT];
	short x, y, k, r;

	for (k = 0; k < NLAYOUT; k++)
	{
		sprintf(lab[k], "%c %s", k == layout_sel ? '\x08' : ' ', layout_name[k]);
		l[k] = lab[k];
	}
	objc_offset(tree, o_layout, &x, &y);
	log_event("menu", "settings-layout", NULL);
	r = popup(x, over_button(y, layout_sel), l, NLAYOUT);
	under_popup();
	if (r < 0)
		return;
	layout_sel = r;
	layout_show();
	dlg_draw(tree, o_layout);
	log_event("settings-layout", layout_val[r], NULL);
}

/* the folders typed, as one PATHS= value: blanks trimmed, empty ones
   left out */
static void paths_join(char *out, size_t size)
{
	short k;

	out[0] = '\0';
	for (k = 0; k < NPATH; k++)
	{
		char *p = paths[k], *e;

		while (*p == ' ')
			p++;
		e = p + strlen(p);
		while (e > p && e[-1] == ' ')
			*--e = '\0';
		if (*p == '\0')
			continue;
		if (out[0] != '\0' && strlen(out) + 1 < size)
			strcat(out, ";");
		if (strlen(out) + strlen(p) < size)
			strcat(out, p);
	}
}

/* a folder that isn't there: kept when the user says so (a drive that
   isn't in yet), else back to the Library page. 1 when all may stay */
static int paths_check(void)
{
	short k;

	for (k = 0; k < NPATH; k++)
	{
		pf_info st;
		char *p = paths[k], shown[52];

		while (*p == ' ')
			p++;
		if (*p == '\0' || (pf_stat(p, &st) == 0 && st.is_dir))
			continue;
		log_event("alert", "no-folder", p);
		/* a long one by its end: the alert has five lines of 30 */
		if (strlen(p) < sizeof(shown))
			strcpy(shown, p);
		else
			sprintf(shown, "...%s", p + strlen(p) - (sizeof(shown) - 4));
		if (alert(1, "[2][%s isn't a folder on this Atari.|Look there anyway?][ Keep |Change]", shown) != 1)
		{
			log_event("alert-closed", "no-folder", "change");
			show_page(2, 1);
			return 0;
		}
		log_event("alert-closed", "no-folder", "keep");
	}
	return 1;
}

int settings_dialog(const char *inf_path, const char *folder)
{
	inf_file *inf = inf_load(inf_path, NULL);
	char was[NPATH * (PCAP + 1)], now[NPATH * (PCAP + 1)];
	short x, y, w, h, r, k;
	int changed = 0;

	/* what the file says now: the program may have changed it */
	start_from(inf != NULL ? inf_get(inf, "START") : NULL);
	{
		const char *v = inf != NULL ? inf_get(inf, "LIBRARY") : NULL;

		layout_sel = v != NULL && atari_casecmp(v, "panes") == 0 ? 1 : 0;
	}
	{
		char **list;
		unsigned int n = inf_split_list(inf != NULL ? inf_get(inf, "PATHS") : NULL, &list);

		for (k = 0; k < NPATH; k++)
			snprintf(paths[k], sizeof(paths[k]), "%s", (unsigned int) k < n ? list[k] : "");
		inf_free_list(list, n);
	}
	inf_free(inf);
	start_was = start_sel;
	layout_was = layout_sel;
	paths_join(was, sizeof(was));
	build(folder);

	/* over the screen on TOS and EmuTOS; on MiNT and MagiC a window of
	   the AES's that can be moved (guidelines 3.5) */
	dlg_name("Settings");
	dlg_band(o_band);                  /* in a window its title bar names it */
	dlg_open(tree, &x, &y, &w, &h);
	{
		char at[16];

		sprintf(at, "%d %d", x, y);
		log_event("dialog", "settings", at);
	}
	log_event("settings-look", dlg_3d_ok() ? "3d" : "no-3d", NULL);
	for (;;)
	{
		r = (short) (dlg_do(tree, 0, o_cancel) & 0x7fff);
		if (r == page_tab[1] || r == page_tab[2])
		{
			if (cur_page != (r == page_tab[1] ? 1 : 2))
			{
				show_page(r == page_tab[1] ? 1 : 2, 1);
				log_event("settings-page", cur_page == 1 ? "general" : "library", NULL);
			}
			continue;
		}
		if (r == o_start)
		{
			start_pick();
			continue;
		}
		if (r == o_layout)
		{
			layout_pick();
			continue;
		}
		if (r == o_ok && !paths_check())
		{
			tree[o_ok].ob_state &= (unsigned short) ~SELECTED;
			dlg_draw(tree, o_ok);
			continue;
		}
		break;
	}
	dlg_close();
	lf_forget(tree);
	tree[r].ob_state &= (unsigned short) ~SELECTED;
	log_event("dialog-closed", "settings", r == o_ok ? "ok" : "cancel");
	if (r != o_ok)
		return 0;

	if (start_sel != start_was && start_sel < NSTART)
	{
		settings_put("START", start_val[start_sel], "when the accessory looks through the library: opened "
			     "(when first opened or asked for a page), now (at boot), or seconds after boot");
		changed |= SETTINGS_START;
	}
	log_event("settings", "START", start_sel < NSTART ? start_val[start_sel] : "other");
	if (layout_sel != layout_was)
	{
		settings_put("LIBRARY", layout_val[layout_sel], "the library's layout: outline (the groups, each "
			     "folding, under the pages read last) or panes (the subjects on the left, the hypertexts "
			     "of the one chosen on the right)");
		changed |= SETTINGS_LAYOUT;
	}
	log_event("settings", "LIBRARY", layout_val[layout_sel]);
	paths_join(now, sizeof(now));
	if (strcmp(now, was) != 0)
	{
		settings_put("PATHS", now, "folders to look for hypertexts in (with their subfolders), between ';', "
			     "as well as ST-GUIDE.INF's PATHS, HYPFOLD and REF, and \\GUIDES and \\GEMSYS\\GUIDES "
			     "on every drive");
		changed |= SETTINGS_PATHS;
	}
	log_event("settings", "PATHS", now);
	return changed;
}
