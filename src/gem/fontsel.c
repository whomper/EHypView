/*
 * EHypView - the font for the text (View > Font...), the same selector
 * as ECal's and EMail's; menus, dialogs and alerts keep the system font.
 *
 * The font families are in a list on the left, the sizes of the chosen
 * one next to it, what kind of font it is below, and a sample.  The
 * system font comes in two sizes (8x8 and 8x16); with a GDOS (NVDI,
 * SpeedoGDOS, FontGDOS...) its fonts are listed too.  Hypertexts are laid
 * out in character cells, so only monospaced fonts can be chosen.  Each
 * font is measured once and the answer kept in EHYPVIEW.FNT, so the
 * selector opens quickly the next time even with hundreds of fonts.
 *
 * Control+Plus and Control+Minus step through the sizes of the font, and
 * a font dropped on a window by a font selector (the font protocol,
 * FONT_CHANGED) is taken too.  The choice is kept in EHYPVIEW.INF.
 *
 * Adapted from ECal's ui/font.c (github.com/whomper/ECal).
 * Copyright (C) the EHypView and ECal authors.  Free software under the
 * GNU General Public License, version 2 or (at your option) any later
 * version; see COPYING.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gem.h"
#include "plat.h"
#include "fontpref.h"
#include "gemapp.h"

#define MAXFAM  256
#define MAXSIZE 20

typedef struct {
	short id, format;         /* VDI font id; 1 bitmap, 2 Speedo, 4 TrueType, 8 Type 1 */
	signed char mono;         /* -1: not measured yet */
	char name[FONT_NAME_MAX];
} FAM;

static FAM fam[MAXFAM];
static short nfam, gdos_fonts = -1, measured;
static short sizes[MAXSIZE], nsizes;
static char size_label[MAXSIZE][12];
static font_pref pref;
static short cur_id = 1, cur_pt;
static char inf_path[200], fnt_path[200];
static unsigned long inf_seen;		/* EHYPVIEW.INF as we last read or wrote it */

static unsigned long stamp(const char *path)
{
	pf_info st;

	return pf_stat(path, &st) == 0 ? st.mtime ^ (st.size << 16) ^ st.size : 0;
}

static void busy(int on)
{
	graf_mouse(on ? BUSYBEE : ARROW, NULL);
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
	cur_id = id;
	cur_pt = pt;
	return 1;
}

/* the system font at the AES's size, for the dialog's own text */
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

/* names only: quick, whatever the number of fonts */
static void list_names(void)
{
	short i, k;

	if (nfam)
		return;
	fam[0].id = 1;
	fam[0].format = 1;
	fam[0].mono = 1;
	strcpy(fam[0].name, FONT_SYSTEM_NAME);
	nfam = 1;
	if (gdos_fonts < 0)
	{
		busy(1);
		gdos_fonts = vq_gdos() ? vst_load_fonts(vdi_h, 0) : 0;
		busy(0);
	}
	for (i = 2; i <= gdos_fonts + 1 && nfam < MAXFAM; i++)
	{
		FAM *f = &fam[nfam];
		char name[34];
		short n;

		f->id = vqt_name(vdi_h, i, name);
		if (f->id <= 1)
			continue;
		for (k = 1; k < nfam; k++)
			if (fam[k].id == f->id)
				break;
		if (k < nfam)
			continue;
		for (n = 31; n >= 0 && (name[n] == ' ' || name[n] == '\0'); n--)
			name[n] = '\0';
		memcpy(f->name, name, sizeof(f->name) - 1);
		f->name[sizeof(f->name) - 1] = '\0';
		f->format = vqt_font_format(vdi_h, i);
		f->mono = -1;
		nfam++;
	}
}

/* whether a font is monospaced: 'i', 'W' and 'm' as wide */
static int measure_mono(const FAM *f)
{
	short w, h;

	if (vst_font(vdi_h, f->id) != f->id)
		return 0;
	vst_point(vdi_h, 12, &w, &h);
	if (w < 1)
		return 0;
	return vqt_width(vdi_h, 'i') == vqt_width(vdi_h, 'W') &&
		vqt_width(vdi_h, 'i') == vqt_width(vdi_h, 'm');
}

/*
 * Only monospaced fonts can be chosen.  Measuring means loading each
 * font, slow with hundreds of vector fonts: the answers are kept in
 * EHYPVIEW.FNT ("1 Courier 10 Pitch", "0 Swiss 721") and only new fonts
 * are measured.
 */
static void measure_all(void)
{
	char *known, *line, *out;
	short i, todo = 0;
	unsigned long len = 0;

	if (measured)
		return;
	measured = 1;
	list_names();
	known = pf_load(fnt_path, NULL);
	for (i = 1; i < nfam; i++)
	{
		for (line = known; line != NULL && *line != '\0';
			line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL)
		{
			size_t n = strcspn(line + 2, "\r\n");

			if ((line[0] == '0' || line[0] == '1') && line[1] == ' ' &&
				n == strlen(fam[i].name) && strncmp(line + 2, fam[i].name, n) == 0)
			{
				fam[i].mono = (signed char) (line[0] == '1');
				break;
			}
		}
		if (fam[i].mono < 0)
			todo++;
	}
	free(known);
	if (todo == 0)
		return;
	busy(1);
	for (i = 1; i < nfam; i++)
		if (fam[i].mono < 0)
			fam[i].mono = (signed char) measure_mono(&fam[i]);
	busy(0);
	out = malloc((size_t) nfam * (FONT_NAME_MAX + 4) + 1);
	if (out != NULL)
	{
		for (i = 1; i < nfam; i++)
			len += (unsigned long) sprintf(out + len, "%d %s\r\n", fam[i].mono, fam[i].name);
		pf_save(fnt_path, out, len);
		free(out);
	}
	set_font(cur_id, cur_pt);
}

static FAM *family_of_id(short id)
{
	short i;

	for (i = 0; i < nfam; i++)
		if (fam[i].id == id)
			return &fam[i];
	return NULL;
}

static FAM *family_of_name(const char *name)
{
	short i;

	for (i = 0; i < nfam; i++)
		if (strcmp(fam[i].name, name) == 0)
			return &fam[i];
	return NULL;
}

/* the sizes of a family, and whether it is monospaced */
static void look_at(FAM *f)
{
	static const short try_pt[] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32, 36 };
	short i, w, h;

	nsizes = 0;
	if (f->id == 1)
	{
		sizes[0] = 8;
		strcpy(size_label[0], "8 x 8");
		sizes[1] = 16;
		strcpy(size_label[1], "8 x 16");
		nsizes = 2;
		return;
	}
	busy(1);
	if (vst_font(vdi_h, f->id) != f->id)
	{
		f->mono = 0;
		busy(0);
		return;
	}
	for (i = 0; i < (short) (sizeof(try_pt) / sizeof(try_pt[0])) && nsizes < MAXSIZE; i++)
	{
		short got = vst_point(vdi_h, try_pt[i], &w, &h);

		if (w < 4 || h < 6 || (nsizes && sizes[nsizes - 1] >= got))
			continue;
		if (f->mono < 0)
			f->mono = (signed char) (vqt_width(vdi_h, 'i') == vqt_width(vdi_h, 'W') &&
				vqt_width(vdi_h, 'i') == vqt_width(vdi_h, 'm'));
		sizes[nsizes] = got;
		sprintf(size_label[nsizes], "%d pt", got);
		nsizes++;
	}
	if (f->mono < 0)
		f->mono = 0;
	if (!f->mono)
		nsizes = 0;
	busy(0);
}

static const char *format_name(const FAM *f)
{
	if (f->id == 1)
		return "Bitmap (built in)";
	switch (f->format)
	{
	case 1: return "Bitmap (GDOS)";
	case 2: return "Speedo";
	case 4: return "TrueType";
	case 8: return "Type 1";
	}
	return "GDOS font";
}

/* ---------------- the setting ---------------- */

static void save(void)
{
	char pt[12];

	sprintf(pt, "%d", pref.point);
	settings_put("FONT", pref.name, "the text font: SYSTEM, or the name of a GDOS font (View > Font...)");
	settings_put("POINT", pt, "its size in points; 0: the usual one");
	inf_seen = stamp(inf_path);
}

/* pref -> the VDI font, cw and ch */
static void apply(void)
{
	FAM *f = NULL;

	if (fontpref_is_system(&pref))
	{
		set_font(1, pref.point);
		return;
	}
	list_names();
	f = family_of_name(pref.name);
	if (f == NULL || !set_font(f->id, pref.point))
	{
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

void font_init(const char *dir, const char *inf)
{
	char *text;

	snprintf(inf_path, sizeof(inf_path), "%s", inf);
	join(fnt_path, sizeof(fnt_path), dir, "EHYPVIEW.FNT");
	text = pf_load(inf_path, NULL);
	inf_seen = stamp(inf_path);
	fontpref_parse(&pref, text);
	free(text);
	apply();
}

/* EHYPVIEW.PRG chose another font while the accessory waited: the
   accessory takes it too */
void font_check(void)
{
	char *text;
	font_pref was = pref;

	if (stamp(inf_path) == inf_seen)
		return;
	text = pf_load(inf_path, NULL);
	inf_seen = stamp(inf_path);
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

const char *font_name(void)
{
	return pref.name;
}

short font_point(void)
{
	return cur_pt;
}

int font_step(int dir)
{
	FAM *f;
	int k;

	list_names();
	f = family_of_id(cur_id);
	if (f == NULL)
		return 0;
	look_at(f);
	k = fontpref_step(sizes, nsizes, cur_pt, dir);
	set_font(cur_id, cur_pt);
	if (k < 0)
		return 0;
	pref.point = sizes[k];
	apply();
	changed();
	return 1;
}

int font_protocol(short id, short point)
{
	FAM *f;

	measure_all();
	f = family_of_id(id);
	if (f == NULL || !f->mono)
		return 0;
	strcpy(pref.name, f->name);
	pref.point = id == 1 ? (short) (point >= 13 ? 16 : 8) : point;
	apply();
	changed();
	return 1;
}

/* ---------------- the dialog's tree, built in code ---------------- */

#define DMAX 16
static OBJECT tree[DMAX];
static short nobj, last_child;

static void d_begin(short w, short h)
{
	memset(tree, 0, sizeof(tree));
	nobj = 1;
	last_child = 0;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = 0x00021100L;
	tree[0].ob_width = w;
	tree[0].ob_height = h;
}

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

static short d_button(short x, short y, short w, const char *s, short flags)
{
	return d_add(G_BUTTON, SELECTABLE | flags, 0, (long) s, x, y, w, 1);
}

static void d_end(void)
{
	short i;

	tree[last_child].ob_next = 0;
	tree[nobj - 1].ob_flags |= LASTOB;
	for (i = 0; i < nobj; i++)
		rsrc_obfix(tree, i);
}

/* ---------------- drawing ---------------- */

typedef struct {
	short x, y, w, h;
} RECT;

static void clip_on(const RECT *r)
{
	short pxy[4];

	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = (short) (r->x + r->w - 1);
	pxy[3] = (short) (r->y + r->h - 1);
	vs_clip(vdi_h, 1, pxy);
}

static void clip_off(void)
{
	static const short none[4] = { 0, 0, 0, 0 };

	vs_clip(vdi_h, 0, none);
}

static void fill_rect(const RECT *r, short color)
{
	fill(color, r->x, r->y, (short) (r->x + r->w - 1), (short) (r->y + r->h - 1));
}

static void outline(short x, short y, short w, short h)
{
	short pxy[10];

	pxy[0] = pxy[6] = pxy[8] = x;
	pxy[1] = pxy[3] = pxy[9] = y;
	pxy[2] = pxy[4] = (short) (x + w - 1);
	pxy[5] = pxy[7] = (short) (y + h - 1);
	vswr_mode(vdi_h, MD_REPLACE);
	vsl_color(vdi_h, 1);
	vsl_type(vdi_h, 1);
	v_pline(vdi_h, 5, pxy);
}

static void line(short x1, short y1, short x2, short y2)
{
	short pxy[4];

	pxy[0] = x1;
	pxy[1] = y1;
	pxy[2] = x2;
	pxy[3] = y2;
	v_pline(vdi_h, 2, pxy);
}

/* dialog text in the system font: inverse, light, at most cols */
static void dtext(short x, short y, const char *s, short cols, int inverse, int light)
{
	short n = (short) strlen(s);

	if (n > cols)
		n = cols;
	vswr_mode(vdi_h, MD_TRANS);
	vst_color(vdi_h, (short) (inverse ? 0 : 1));
	vst_effects(vdi_h, (short) (light ? 2 : 0));
	v_gtext_n(vdi_h, x, y, s, n);
	vst_effects(vdi_h, 0);
	vst_color(vdi_h, 1);
}

/* ---------------- a list box ---------------- */

typedef struct {
	short obj;                /* its G_BOX */
	RECT r;                   /* inside the border */
	short n, top, sel, rows;
	int list_of_sizes;
} LIST;

static LIST lf, ls;

static short sbar(void)
{
	return (short) (2 * sys_cw);
}

static void list_place(LIST *l)
{
	short x, y;

	objc_offset(tree, l->obj, &x, &y);
	l->r.x = (short) (x + 1);
	l->r.y = (short) (y + 1);
	l->r.w = (short) (tree[l->obj].ob_width - 2);
	l->r.h = (short) (tree[l->obj].ob_height - 2);
	l->rows = (short) (l->r.h / sys_ch);
}

static void arrow(short x, short y, short w, short h, int up)
{
	short cx = (short) (x + w / 2), cy = (short) (y + h / 2), k = (short) (h >= 14 ? 3 : 2);
	short y1 = (short) (up ? cy + k / 2 + 1 : cy - k / 2 - 1);
	short y2 = (short) (up ? cy - k / 2 - 1 : cy + k / 2 + 1);

	outline(x, y, w, h);
	line((short) (cx - k), y1, cx, y2);
	line(cx, y2, (short) (cx + k), y1);
}

static void list_draw(LIST *l)
{
	RECT r = l->r, t;
	short i, sw = sbar(), cols = (short) ((r.w - sw - 4) / sys_cw), ah = sys_ch;

	use_sys();
	graf_mouse(M_OFF, NULL);
	clip_on(&r);
	fill_rect(&r, 0);
	for (i = 0; i < l->rows && l->top + i < l->n; i++)
	{
		short k = (short) (l->top + i), y = (short) (r.y + i * sys_ch);
		const char *s = l->list_of_sizes ? size_label[k] : fam[k].name;
		int light = !l->list_of_sizes && !fam[k].mono;

		if (k == l->sel)
		{
			t.x = r.x;
			t.y = y;
			t.w = (short) (r.w - sw);
			t.h = sys_ch;
			fill_rect(&t, 1);
		}
		dtext((short) (r.x + 2), y, s, cols, k == l->sel, light);
	}
	/* the scroll bar */
	t.x = (short) (r.x + r.w - sw);
	t.y = r.y;
	t.w = sw;
	t.h = r.h;
	fill_rect(&t, 0);
	arrow(t.x, t.y, sw, ah, 1);
	arrow(t.x, (short) (t.y + t.h - ah), sw, ah, 0);
	{
		short ty = (short) (t.y + ah), th = (short) (t.h - 2 * ah), pxy[4];

		pxy[0] = (short) (t.x + 1);
		pxy[1] = ty;
		pxy[2] = (short) (t.x + sw - 2);
		pxy[3] = (short) (ty + th - 1);
		vswr_mode(vdi_h, MD_REPLACE);
		vsf_interior(vdi_h, FIS_PATTERN);
		vsf_style(vdi_h, 1);
		vsf_color(vdi_h, 1);
		vsf_perimeter(vdi_h, 0);
		vr_recfl(vdi_h, pxy);
		if (l->n > l->rows)
		{
			short h = (short) ((long) th * l->rows / l->n), y;

			if (h < 6)
				h = 6;
			y = (short) (ty + (long) (th - h) * l->top / (l->n - l->rows));
			t.y = y;
			t.h = h;
			fill_rect(&t, 0);
			outline(t.x, y, sw, h);
		} else
		{
			t.y = ty;
			t.h = th;
			fill_rect(&t, 0);
		}
		outline((short) (r.x + r.w - sw), r.y, sw, r.h);
	}
	clip_off();
	graf_mouse(M_ON, NULL);
}

static void list_show(LIST *l, short k)
{
	if (k < l->top)
		l->top = k;
	if (k >= l->top + l->rows)
		l->top = (short) (k - l->rows + 1);
	if (l->top > l->n - l->rows)
		l->top = (short) (l->n - l->rows);
	if (l->top < 0)
		l->top = 0;
}

/* ---------------- the dialog ---------------- */

static short o_ok, o_type, o_prev;

static void draw_obj(short obj)
{
	short x, y;

	objc_offset(tree, obj, &x, &y);
	objc_draw(tree, 0, 8, (short) (x - 2), (short) (y - 2),
		(short) (tree[obj].ob_width + 4), (short) (tree[obj].ob_height + 4));
}

static short chosen_pt(void)
{
	return ls.sel >= 0 && ls.sel < nsizes ? sizes[ls.sel] : 0;
}

static void draw_preview(void)
{
	/* with umlauts in the Atari character set */
	static const char l1[] = "AaBbCc 0123 \x84\x94\x81\x8e\x99\x9a\x9e The quick brown fox";
	static const char l2[] = "jumps over the lazy dog. {[(<|>)]} @#$%&*";
	RECT r;
	short x, y;

	objc_offset(tree, o_prev, &x, &y);
	r.x = (short) (x + 1);
	r.y = (short) (y + 1);
	r.w = (short) (tree[o_prev].ob_width - 2);
	r.h = (short) (tree[o_prev].ob_height - 2);
	graf_mouse(M_OFF, NULL);
	clip_on(&r);
	fill_rect(&r, 0);
	if (fam[lf.sel].mono && set_font(fam[lf.sel].id, chosen_pt()))
	{
		short cols = (short) ((r.w - 2 * cw) / cw);

		dtext((short) (r.x + cw), (short) (r.y + 2), l1, cols, 0, 0);
		dtext((short) (r.x + cw), (short) (r.y + 4 + ch), l2, cols, 0, 0);
	} else
	{
		use_sys();
		dtext((short) (r.x + sys_cw), (short) (r.y + 2), "Proportional: hypertexts need a fixed-width font.",
			(short) ((r.w - 2 * sys_cw) / sys_cw), 0, 0);
	}
	use_sys();
	clip_off();
	graf_mouse(M_ON, NULL);
}

static void draw_type(void)
{
	short x, y;
	RECT r;

	objc_offset(tree, o_type, &x, &y);
	r.x = x;
	r.y = y;
	r.w = tree[o_type].ob_width;
	r.h = tree[o_type].ob_height;
	graf_mouse(M_OFF, NULL);
	use_sys();
	clip_on(&r);
	fill_rect(&r, 0);
	dtext(x, y, format_name(&fam[lf.sel]), (short) (r.w / sys_cw), 0, 0);
	clip_off();
	graf_mouse(M_ON, NULL);
}

static void set_ok(void)
{
	short dis = (short) (!fam[lf.sel].mono || !nsizes);
	short was = (short) ((tree[o_ok].ob_state & DISABLED) != 0);

	if (dis == was)
		return;
	if (dis)
		tree[o_ok].ob_state |= DISABLED;
	else
		tree[o_ok].ob_state &= (unsigned short) ~DISABLED;
	draw_obj(o_ok);
}

/* a family was picked: its sizes, keeping the size if it has it */
static void family_changed(int draw)
{
	short want = chosen_pt();
	int k;

	if (want == 0)
		want = cur_pt;
	look_at(&fam[lf.sel]);
	use_sys();
	ls.n = nsizes;
	k = fontpref_pick(sizes, nsizes, want);
	ls.sel = (short) k;
	ls.top = 0;
	list_show(&ls, (short) (ls.sel < 0 ? 0 : ls.sel));
	if (draw)
	{
		list_draw(&ls);
		draw_type();
		draw_preview();
		set_ok();
	}
}

static short button_down(short *mx, short *my)
{
	short mb, ks;

	graf_mkstate(mx, my, &mb, &ks);
	return (short) (mb & 3);
}

static void wait_up(void)
{
	short mx, my;

	while (button_down(&mx, &my))
		evnt_timer_(10);
}

/* a click in a list: 1 = a double click on an entry */
static int list_click(LIST *l, short mx, short my, short clicks)
{
	short sw = sbar(), ah = sys_ch, x, y;

	if (mx >= l->r.x + l->r.w - sw)
	{
		if (my < l->r.y + ah || my >= l->r.y + l->r.h - ah)
		{
			short d = (short) (my < l->r.y + ah ? -1 : 1), first = 1;

			do
			{
				short old = l->top;

				l->top = (short) (l->top + d);
				list_show(l, l->top < old ? l->top : (short) (l->top + l->rows - 1));
				if (l->top != old)
					list_draw(l);
				evnt_timer_(first ? 250 : 50);
				first = 0;
			} while (button_down(&x, &y));
		} else
		{
			short track_h = (short) (l->r.h - 2 * ah), mid = (short) (l->r.y + ah + track_h / 2);

			l->top = (short) (l->top + (my < mid ? -l->rows : l->rows));
			if (l->top > l->n - l->rows)
				l->top = (short) (l->n - l->rows);
			if (l->top < 0)
				l->top = 0;
			list_draw(l);
		}
		return 0;
	}
	{
		short k = (short) (l->top + (my - l->r.y) / sys_ch);

		if (k < 0 || k >= l->n)
			return 0;
		if (k != l->sel)
		{
			l->sel = k;
			list_draw(l);
			if (l == &lf)
				family_changed(1);
			else
				draw_preview();
		}
		return clicks >= 2;
	}
}

void font_dialog(void)
{
	short i, x, y, w, h, o_cancel, o_fam, o_size, done = 0, ok = 0, m[8];
	short keep_id = cur_id, keep_pt = cur_pt;
	AEVENT e;

	measure_all();
	lf.sel = 0;
	for (i = 0; i < nfam; i++)
		if (fam[i].id == cur_id)
			lf.sel = i;
	lf.n = nfam;
	lf.top = 0;
	lf.list_of_sizes = 0;
	ls.list_of_sizes = 1;
	ls.sel = -1;
	family_changed(0);

	/* fits ST medium too: 64 columns, 20 rows of 8 pixels */
	d_begin(64, 20);
	d_text(2, 1, "Font for the text");
	d_text(2, 3, "Family");
	d_text(39, 3, "Size");
	o_fam = d_add(G_BOX, TOUCHEXIT, 0, 0x00FF1100L, 2, 4, 35, 9);
	o_size = d_add(G_BOX, TOUCHEXIT, 0, 0x00FF1100L, 39, 4, 12, 9);
	o_ok = d_button(53, 4, 9, "OK", EXIT | DEFAULT);
	o_cancel = d_button(53, 6, 9, "Cancel", EXIT);
	d_text(2, 14, "Type:");
	o_type = d_add(G_IBOX, 0, 0, 0L, 8, 14, 54, 1);
	o_prev = d_add(G_BOX, 0, 0, 0x00FF1100L, 2, 16, 60, 3);
	d_end();
	lf.obj = o_fam;
	ls.obj = o_size;
	if (!fam[lf.sel].mono || !nsizes)
		tree[o_ok].ob_state |= DISABLED;

	form_center(tree, &x, &y, &w, &h);
	wind_update(BEG_UPDATE);
	wind_update(3);                      /* BEG_MCTRL */
	form_dial(FMD_START, x, y, w, h);
	use_sys();
	objc_draw(tree, 0, 8, x, y, w, h);
	list_place(&lf);
	list_place(&ls);
	list_show(&lf, lf.sel);
	list_show(&ls, (short) (ls.sel < 0 ? 0 : ls.sel));
	list_draw(&lf);
	list_draw(&ls);
	draw_type();
	draw_preview();
	log_event("dialog", "font", NULL);
	wait_up();

	while (!done)
	{
		evnt_multi_(MU_KEYBD | MU_BUTTON, 2, 1, 1, 0, m, &e);
		if (e.which & MU_KEYBD)
		{
			short sc = KEY_SCAN(e.kreturn), as = KEY_ASCII(e.kreturn);

			if (as == 0x1b || sc == 0x61)            /* Esc, Undo */
			{
				done = 1;
			} else if (as == 0x0d || sc == 0x72)     /* Return, Enter */
			{
				if (!(tree[o_ok].ob_state & DISABLED))
					done = ok = 1;
			} else if ((sc == 0x48 && lf.sel > 0) || (sc == 0x50 && lf.sel < nfam - 1))
			{
				lf.sel = (short) (lf.sel + (sc == 0x48 ? -1 : 1));
				list_show(&lf, lf.sel);
				list_draw(&lf);
				family_changed(1);
			} else if ((sc == 0x4b && ls.sel > 0) || (sc == 0x4d && ls.sel >= 0 && ls.sel < nsizes - 1))
			{
				ls.sel = (short) (ls.sel + (sc == 0x4b ? -1 : 1));
				list_show(&ls, ls.sel);
				list_draw(&ls);
				draw_preview();
			}
		}
		if (e.which & MU_BUTTON)
		{
			short obj = objc_find(tree, 0, 8, e.mx, e.my);

			if (obj == o_fam || obj == o_size)
			{
				if (list_click(obj == o_fam ? &lf : &ls, e.mx, e.my, e.breturn) &&
					!(tree[o_ok].ob_state & DISABLED))
					done = ok = 1;
			} else if ((obj == o_ok && !(tree[o_ok].ob_state & DISABLED)) || obj == o_cancel)
			{
				tree[obj].ob_state |= SELECTED;
				draw_obj(obj);
				wait_up();
				tree[obj].ob_state &= (unsigned short) ~SELECTED;
				done = 1;
				ok = obj == o_ok;
			}
		}
	}
	form_dial(FMD_FINISH, x, y, w, h);
	wind_update(2);                      /* END_MCTRL */
	wind_update(END_UPDATE);
	if (ok)
	{
		strcpy(pref.name, fam[lf.sel].name);
		pref.point = chosen_pt();
		apply();
		changed();
	} else
	{
		set_font(keep_id, keep_pt);
	}
}
