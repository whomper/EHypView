/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the Settings dialog (^,, the AERA guidelines 6.1): what
 * EHYPVIEW.INF holds, on the family's Settings dialog (common/ui/settings.c:
 * the band, the pages on the left, the keys of 5.10, Cancel and OK). This
 * file gives its two pages and what OK writes:
 *
 *   General   START=: when the accessory looks through the library;
 *             FOLDER=, shown: where the index is kept;
 *             LOG=: Write a log (EHYPVIEW.LOG), the Options tick too (6.5)
 *   Library   LIBRARY=: its layout, Outline or Two panes (radios, 5.6);
 *             PATHS=: folders to look in as well as ST-GUIDE.INF's
 *
 * The font is Options > Font... (no key, D6), as in the other apps, and the
 * bookmarks are kept from the Bookmarks menu and the library.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gem.h"
#include "gemapp.h"
#include "ehvset.h"
#include "settings.h"
#include "dialog.h"
#include "alert.h"
#include "inf.h"
#include "plat.h"
#include "bgwork.h"
#include "charset.h"

#define H 18                       /* the dialog's rows (57 by 18: the page is 42) */
#define NPATH 3                    /* folder fields on the Library page */
#define PLEN 40                    /* characters a folder field shows */
#define PCAP 128                   /* and holds (5.4): a GEMDOS path */

static char paths[NPATH][PCAP + 1];
static short o_path[NPATH];

/* ---------------------------------------------------------------- */
/* START=, four radios (D24; Erez, 19 Nov 2026: "Radios, as proposed"). */
/* Another number of seconds in the file: none chosen, a line says it, */
/* and the file keeps it until one is clicked                         */

static const char *const start_val[] = { "opened", "now", "60", "300" };
#define NSTART 4
static const char *const start_name[NSTART] = { "Opened", "At start", "1 minute", "5 minutes" };
static char start_other_line[40];
static short start_sel, start_was;     /* NSTART: another number of seconds */

static void start_from(const char *v)
{
	long s = bg_parse(v != NULL ? v : "opened");

	start_sel = s == BG_OPENED ? 0 : s == BG_NOW ? 1 : s == 60 ? 2 : s == 300 ? 3 : NSTART;
	sprintf(start_other_line, "Now after %ld s (EHYPVIEW.INF).", s);
}

/* LIBRARY=, two radios */
static const char *const layout_val[] = { "outline", "panes" };
static const char *const layout_name[] = { "Outline", "Two panes" };
static short layout_sel, layout_was;

/* LOG=: Write a log */
static short log_on, log_was;

static char folder_text[PLEN + 1];

/* ---------------------------------------------------------------- */
/* the pages: the rows as they were, a half character between groups */

static void page_general(SETTINGS *s, short x, short y, void *user)
{
	short gap = (short) (sys_ch / 2);

	(void) y;
	(void) user;
	set_text(s, x, 4, "The accessory looks through the library:");
	set_radio(s, x, (short) (5 | (gap << 8)), 9, start_name, NSTART, &start_sel);
	if (start_was == NSTART)
		set_text(s, x, (short) (6 | (gap << 8)), start_other_line);
	set_text(s, x, (short) (7 | (gap << 8)), "The program looks as it starts.");
	set_text(s, x, (short) (9 | (gap << 8)), "EHypView's folder (FOLDER=):");
	set_text(s, (short) (x + 2), (short) (10 | (gap << 8)), folder_text);
	set_text(s, x, (short) (11 | (gap << 8)), "keeps the library's index, EHYPVIEW.IDX,");
	set_text(s, x, (short) (12 | (gap << 8)), "and the fonts measured, EHYPVIEW.FNT.");
	set_tick(s, x, (short) (14 | (gap << 8)), "Write a log (EHYPVIEW.LOG)", &log_on);
}

static void page_library(SETTINGS *s, short x, short y, void *user)
{
	short gap = (short) (sys_ch / 2), k;

	(void) y;
	(void) user;
	set_text(s, x, 4, "Layout:");
	set_radio(s, (short) (x + 9), 4, 11, layout_name, 2, &layout_sel);
	set_text(s, x, (short) (6 | (gap << 8)), "Also look for hypertexts in (PATHS=):");
	for (k = 0; k < NPATH; k++)
		o_path[k] = set_long(s, x, (short) ((7 + k) | ((gap + 2 + k * (sys_ch / 4 + 2)) << 8)),
				     paths[k], PLEN, PCAP, 0);
	set_text(s, x, (short) (11 | (gap << 8)), "Each with its subfolders, as well as");
	set_text(s, x, (short) (12 | (gap << 8)), "PATHS, HYPFOLD and REF in ST-GUIDE.INF,");
	set_text(s, x, (short) (13 | (gap << 8)), "and \\GUIDES and \\GEMSYS\\GUIDES on every");
	set_text(s, x, (short) (14 | (gap << 8)), "drive.");
}

/* ---------------------------------------------------------------- */
/* what the user did, as "EHV ..." lines for the gui rig             */

static short seen_page, seen_start, seen_layout, seen_log;

static void changed(SETTINGS *s, short obj)
{
	(void) obj;
	if (set_page_now(s) != seen_page)
	{
		seen_page = set_page_now(s);
		log_event("settings-page", seen_page == 0 ? "general" : "library", NULL);
	}
	if (start_sel != seen_start)
	{
		seen_start = start_sel;
		log_event("settings-start", start_sel < NSTART ? start_val[start_sel] : "other", NULL);
	}
	if (layout_sel != seen_layout)
	{
		seen_layout = layout_sel;
		log_event("settings-layout", layout_val[layout_sel], NULL);
	}
	if (log_on != seen_log)
	{
		seen_log = log_on;
		log_event("settings-log", log_on ? "on" : "off", NULL);
	}
}

/* the dialog drawn: where it is (the rig's clicks are measured from it)
   and its look */
static void drawn(OBJECT *tree, short band)
{
	char at[16];
	short d = (short) ((tree[0].ob_state & OUTLINED) ? 3 : 0);

	(void) band;
	sprintf(at, "%d %d", tree[0].ob_x - d, tree[0].ob_y - d);
	log_event("dialog", "settings", at);
	log_event("settings-look", dlg_3d_ok() ? "3d" : "no-3d", NULL);
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
   isn't in yet), else back to the Library page, the cursor in it. 1 when
   all may stay */
static int paths_check(SETTINGS *s, void *user)
{
	short k;

	(void) user;
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
		if (alert(1, "[2][%s isn't a folder on this Atari.|Look there anyway?][Keep|Change]", shown) != 1)
		{
			log_event("alert-closed", "no-folder", "change");
			set_show(s, 1);
			set_focus(s, o_path[k]);
			return 0;
		}
		log_event("alert-closed", "no-folder", "keep");
	}
	return 1;
}

int settings_log_on(const inf_file *inf)
{
	const char *v = inf != NULL ? inf_get(inf, "LOG") : NULL;

	return v != NULL && atoi(v) == 1;
}

int ehv_settings(const char *inf_path, const char *folder)
{
	static const SET_PAGE pages[] = { { "General", page_general }, { "Library", page_library } };
	inf_file *inf = inf_load(inf_path, NULL);
	char was[NPATH * (PCAP + 1)], now[NPATH * (PCAP + 1)];
	SET_DEF d;
	short k;
	int changed_ = 0, ok;

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
	log_on = (short) settings_log_on(inf);
	inf_free(inf);
	log_was = log_on;
	start_was = start_sel;
	layout_was = layout_sel;
	paths_join(was, sizeof(was));
	snprintf(folder_text, sizeof(folder_text), "%s", folder);

	seen_page = 0;
	seen_start = start_sel;
	seen_layout = layout_sel;
	seen_log = log_on;
	memset(&d, 0, sizeof(d));
	d.title = "Settings";
	d.pages = pages;
	d.npages = 2;
	d.height = H;
	d.check = paths_check;
	settings_changed = changed;
	settings_drawn = drawn;
	ok = settings_dialog(&d);
	settings_changed = NULL;
	settings_drawn = NULL;
	log_event("dialog-closed", "settings", ok ? "ok" : "cancel");
	if (!ok)
		return 0;

	if (start_sel != start_was && start_sel < NSTART)
	{
		settings_put("START", start_val[start_sel], "when the accessory looks through the library: opened "
			     "(when first opened or asked for a page), now (at boot), or seconds after boot");
		changed_ |= SETTINGS_START;
	}
	log_event("settings", "START", start_sel < NSTART ? start_val[start_sel] : "other");
	if (layout_sel != layout_was)
	{
		settings_put("LIBRARY", layout_val[layout_sel], "the library's layout: outline (the groups, each "
			     "folding, under the pages read last) or panes (the subjects on the left, the hypertexts "
			     "of the one chosen on the right)");
		changed_ |= SETTINGS_LAYOUT;
	}
	log_event("settings", "LIBRARY", layout_val[layout_sel]);
	if (log_on != log_was)
	{
		settings_put("LOG", log_on ? "1" : "0", SETTINGS_LOG_NOTE);
		changed_ |= SETTINGS_LOG;
	}
	log_event("settings", "LOG", log_on ? "1" : "0");
	paths_join(now, sizeof(now));
	if (strcmp(now, was) != 0)
	{
		settings_put("PATHS", now, "folders to look for hypertexts in (with their subfolders), between ';', "
			     "as well as ST-GUIDE.INF's PATHS, HYPFOLD and REF, and \\GUIDES and \\GEMSYS\\GUIDES "
			     "on every drive");
		changed_ |= SETTINGS_PATHS;
	}
	log_event("settings", "PATHS", now);
	return changed_;
}
