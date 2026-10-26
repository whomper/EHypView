/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the font for the text (View > Font...); menus, dialogs and
 * alerts keep the system font.
 *
 * The Font dialog is the family's (common/ui/fontsel.c, the guidelines
 * 5.3), as in ECal and EMail: the system font and the fixed-width GDOS
 * fonts with letters, each measured once and the answer kept in
 * EHYPVIEW.FNT. What is EHypView's is here: the font set on the
 * screen's workstation, FONT= and POINT= in EHYPVIEW.INF (by name),
 * Control+Plus and Control+Minus, and the font protocol (FONT_CHANGED).
 *
 * The accessory loads GDOS fonts only at start-up, before its first
 * event call (guidelines 2.2): loaded later, their memory would belong
 * to the program in front and go when it ends.  Under NVDI it always
 * loads them, so its Font dialog lists the program's fonts and a font
 * chosen is used at once.  Under FONTGDOS or SpeedoGDOS only when
 * EHYPVIEW.INF names one, to spare a 1 MB ST their memory; without them,
 * the dialog lists the fonts EHYPVIEW.FNT knows "from the next start",
 * and one chosen is taken at the next boot.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "plat.h"
#include "fontpref.h"
#include "gemapp.h"
#include "alert.h"
#include "fontsel.h"

static short gdos_fonts = -1;             /* loaded on vdi_h: vst_load_fonts' count */
static font_pref pref;
static short cur_id = 1, cur_pt;
static char inf_path[200], fnt_path[200];
static unsigned long inf_seen;            /* EHYPVIEW.INF as we last read or wrote it */
static int acc, may_load = 1;             /* the accessory: GDOS fonts at start-up only */

static unsigned long stamp_of(const pf_info *st)
{
	return st->mtime ^ (st->size << 16) ^ st->size;
}

static unsigned long stamp(const char *path)
{
	pf_info st;

	return pf_stat(path, &st) == 0 ? stamp_of(&st) : 0;
}

static short sys_point(void)
{
	return (short) (sys_ch >= 16 ? 16 : 8);
}

/* set the font; cw/ch become its cell size.  0 if it isn't there */
static int set_font(short id, short pt)
{
	short w, h;

	if (id == 1)
	{
		if (pt != 8 && pt != 16)
			pt = sys_point();
		vst_font(vdi_h, 1);
		vst_height(vdi_h, (short) (pt == 16 ? 13 : 6), &w, &h);
	} else
	{
		if (vst_font(vdi_h, id) != id)
			return 0;
		pt = vst_point(vdi_h, pt, &w, &h);
	}
	if (w < 4 || h < 6)
		return 0;
	cw = w;
	ch = h;
	vst_alignment(vdi_h, 0, 5);
	fast_fx = id == 1 && get_cookie(0x4E564449L) == 0;	/* the system font, not NVDI */
	cur_id = id;
	cur_pt = pt;
	return 1;
}

/* the system font at the AES's size, for the dialogs' own text */
static void use_sys(void)
{
	set_font(1, sys_point());
	cw = sys_cw;
	ch = sys_ch;
}

/* dialogs and the About box draw in the system font; the text font
   comes back afterwards */
static short sys_depth, saved_id, saved_pt;

void font_sys_push(void)
{
	if (sys_depth++ == 0)
	{
		saved_id = cur_id;
		saved_pt = cur_pt;
		use_sys();
	}
}

void font_sys_pop(void)
{
	if (sys_depth > 0 && --sys_depth == 0)
		set_font(saved_id, saved_pt);
}

/* ---------------- the fonts ---------------- */

static void load_fonts(void)
{
	if (gdos_fonts >= 0 || !may_load)
		return;
	graf_mouse(BUSYBEE, NULL);
	gdos_fonts = vq_gdos() ? vst_load_fonts(vdi_h, 0) : 0;
	graf_mouse(ARROW, NULL);
}

/* a loaded GDOS font's id and name (trimmed), by its place (2...) */
static short font_at(short i, char *name)
{
	short id = vqt_name(vdi_h, i, name), n;

	for (n = 31; n >= 0 && (name[n] == ' ' || name[n] == '\0'); n--)
		name[n] = '\0';
	return id;
}

/* the id of the loaded font of this name, or 0 */
static short id_of(const char *want)
{
	short i;
	char name[34];

	for (i = 2; i <= gdos_fonts + 1; i++)
	{
		short id = font_at(i, name);

		if (id > 1 && strcmp(name, want) == 0)
			return id;
	}
	return 0;
}

/* ---------------- the setting ---------------- */

static void save(void)
{
	char pt[12];

	sprintf(pt, "%d", pref.point);
	settings_put("FONT", pref.name, "the text font: System font, or the name of a GDOS font (View > Font...)");
	settings_put("POINT", pt, "its size in points; 0: the usual one");
	inf_seen = stamp(inf_path);
}

/* pref -> the VDI font, cw and ch */
static void apply(void)
{
	short id;

	if (fontpref_is_system(&pref))
	{
		set_font(1, pref.point);
		return;
	}
	load_fonts();
	id = id_of(pref.name);
	if (id == 0 || !set_font(id, pref.point))
	{
		/* the accessory keeps the choice for the next boot */
		if (!acc)
			fontpref_default(&pref);
		set_font(1, 0);
	}
}

/* a new font is in force: tell the windows */
static void changed(void)
{
	save();
	app_font_changed();
}

static void join(char *out, size_t size, const char *dir, const char *name)
{
	size_t n = strlen(dir);

	snprintf(out, size, "%s%s%s", dir, n > 0 && dir[n - 1] != '\\' ? "\\" : "", name);
}

/* the accessory: before its first event call, so GDOS fonts load now
   or not at all (guidelines 2.2): always under NVDI, else only when
   EHYPVIEW.INF names one */
void font_init(const char *dir, const char *inf, const char *text, const pf_info *info, int is_acc)
{
	snprintf(inf_path, sizeof(inf_path), "%s", inf);
	join(fnt_path, sizeof(fnt_path), dir, "EHYPVIEW.FNT");
	inf_seen = text != NULL ? stamp_of(info) : 0;
	fontpref_parse(&pref, text);
	acc = is_acc;
	if (acc && (get_cookie(0x4E564449L) != 0 || !fontpref_is_system(&pref)))	/* NVDI */
		load_fonts();                  /* vst_load_fonts */
	may_load = !acc;
	apply();
}

/* EHYPVIEW.PRG chose another font while the accessory waited: the
   accessory takes it too */
void font_check(void)
{
	char *text;
	pf_info info;
	font_pref was = pref;

	if (stamp(inf_path) == inf_seen)
		return;
	text = pf_load_info(inf_path, NULL, &info);
	inf_seen = text != NULL ? stamp_of(&info) : 0;
	fontpref_parse(&pref, text);
	free(text);
	if (strcmp(was.name, pref.name) != 0 || was.point != pref.point)
	{
		apply();
		app_font_changed();
	}
}

void font_exit(void)
{
	if (gdos_fonts > 0)
		vst_unload_fonts(vdi_h, 0);
	gdos_fonts = -1;
}

/* the font in use: in the accessory, the one chosen may wait for the
   next boot */
const char *font_name(void)
{
	static char name[34];
	short i;

	if (cur_id == 1)
		return FONT_SYSTEM_NAME;
	for (i = 2; i <= gdos_fonts + 1; i++)
		if (font_at(i, name) == cur_id)
			return name;
	return pref.name;
}

short font_point(void)
{
	return cur_pt;
}

/* ^+ and ^-: the next size the font has */
int font_step(int dir)
{
	static const short try_pt[] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32, 36 };
	short sizes[sizeof(try_pt) / sizeof(try_pt[0])], nsizes = 0, i, w, h, id = cur_id, pt = cur_pt;
	int k;

	if (id == 1)
	{
		sizes[0] = 8;
		sizes[1] = 16;
		nsizes = 2;
	} else
	{
		graf_mouse(BUSYBEE, NULL);
		for (i = 0; i < (short) (sizeof(try_pt) / sizeof(try_pt[0])); i++)
		{
			short got = vst_point(vdi_h, try_pt[i], &w, &h);

			if (w < 4 || h < 6 || (nsizes && sizes[nsizes - 1] >= got))
				continue;
			sizes[nsizes++] = got;
		}
		graf_mouse(ARROW, NULL);
	}
	k = fontpref_step(sizes, nsizes, pt, dir);
	set_font(id, pt);
	if (k < 0)
		return 0;
	pref.point = sizes[k];
	apply();
	changed();
	return 1;
}

/* the fontsel for this program, as it is now */
static void fill_in(fontsel *fs)
{
	memset(fs, 0, sizeof(*fs));
	fs->app = "EHypView";
	fs->fnt = fnt_path;
	/* the accessory's font of the next start: by its name */
	fs->id = cur_id == 1 && !fontpref_is_system(&pref) ? 0 : cur_id;
	fs->pt = cur_pt;
	fs->name = pref.name;
	/* the accessory without them (FONTGDOS, SpeedoGDOS, none named at
	   boot): EHYPVIEW.FNT's for the next start */
	fs->gdos = gdos_fonts < 0 || (acc && gdos_fonts == 0) ? FS_UNLOADED : gdos_fonts;
	fs->acc = acc;
}

/* a font dropped on a window by a font selector (the font protocol):
   taken when the Font dialog would list it */
int font_protocol(short id, short point)
{
	fontsel fs;
	char name[34];
	short i;

	fill_in(&fs);
	if (id != 1 && (gdos_fonts <= 0 || !fontsel_usable(&fs, id)))
		return 0;
	if (id == 1)
		strcpy(pref.name, FONT_SYSTEM_NAME);
	else
	{
		for (i = 2; i <= gdos_fonts + 1; i++)
			if (font_at(i, name) == id)
				break;
		if (i > gdos_fonts + 1)
			return 0;
		snprintf(pref.name, sizeof(pref.name), "%s", name);
	}
	pref.point = id == 1 ? (short) (point >= 13 ? 16 : 8) : point;
	apply();
	changed();
	return 1;
}

/* ---------------- View > Font... (common/ui/fontsel.c) ---------------- */

/* OK: the font for the text, kept in EHYPVIEW.INF.  The accessory's font
   of the next start is kept until then */
static void take(fontsel *s)
{
	log_event("dialog-closed", "font", "ok");
	log_event("font-chosen", s->font, s->later ? "later" : "now");
	if (!acc && s->gdos >= 0)
		gdos_fonts = s->gdos;          /* loaded by the dialog: not again in apply() */
	snprintf(pref.name, sizeof(pref.name), "%s", s->font);
	pref.point = s->pt;
	if (s->later)
	{
		save();
		log_event("alert", "font-next-boot", pref.name);
		alert(1, "[1][EHypView takes %s when the Atari next starts: the accessory loads GDOS fonts at "
		      "start-up only.][ OK ]", pref.name);
		return;
	}
	apply();
	changed();
}

void font_dialog(void)
{
	static fontsel fs;

	fill_in(&fs);
	fs.apply = take;
	log_event("dialog", "font", NULL);
	if (!fontsel_run(&fs))
		log_event("dialog-closed", "font", "cancel");
	if (!acc && fs.gdos >= 0)
		gdos_fonts = fs.gdos;          /* the program loads them when first wanted */
}
