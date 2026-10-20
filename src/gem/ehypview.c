/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the GEM viewer.
 *
 * Windows showing a page each, under a toolbar (toolbar.c: Back,
 * Forward, the main page, the page before and after, Contents, Index,
 * See also, the library, bookmarks, a menu).  Text is laid out on the
 * character grid of the text font, as ST-Guide does: graphics (@line,
 * @box, @image) are positioned in character cells.  Navigation (links, history, help
 * requests) lives in src/view and the core library; this file draws and
 * handles events.
 *
 * The same program runs as an application (EHYPVIEW.APP/.PRG) and as a
 * desk accessory (EHYPVIEW.ACC; name it ST-GUIDE.ACC so that programs
 * looking for ST-Guide find it).  The application has a menu bar
 * (menu.c); every menu item has a keyboard shortcut and both go through
 * command().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "fsys.h"
#include "plat.h"
#include "inf.h"
#include "charset.h"
#include "scan.h"
#include "libjob.h"
#include "resolve.h"
#include "view.h"
#include "bookmark.h"
#include "gemapp.h"
#include "pagedraw.h"
#include "menu.h"
#include "toolbar.h"
#include "stmem.h"
#include "crash.h"
#include "av.h"
#include "grect.h"
#include "popup.h"
#include "aboutbox.h"
#include "alert.h"
#include "iconify.h"
#include "clip.h"
#include "dd.h"
#include "twin.h"
#include "bgwork.h"
#include "bigbuf.h"
#include "settings.h"
#include "library.h"
#include "version.h"

#define VA_START   0x4711
#define AC_HELP    1025
#define FONT_CHANGED 0x7a18        /* the font protocol */
#define FONT_ACK     0x7a1a

#define MARGIN       4             /* pixels left of the text */
#define WKIND (NAME | CLOSER | FULLER | MOVER | INFO | SIZER | UPARROW | DNARROW | \
               VSLIDE | LFARROW | RTARROW | HSLIDE)

extern short _is_acc;              /* crt0.S */

short ap_id, is_acc, quit_now;
short vdi_h, cw, ch, planes, ncolors;
short tall_pixels;
short sys_cw, sys_ch;
short desk_x, desk_y, desk_w, desk_h;
ehv_index *idx;
static short menu_id = -1;
static ehv_menu menu;
static short has_menu;
static signed char menu_on[CMD_COUNT];   /* what menu_ienable last set */

/*
 * The viewer windows: each shows a page, with its own history, scroll
 * position, layout and pop-up.  `cur` is the one worked on: the one in
 * front, or the one last used when the library is.
 */
typedef struct {
	short h;                       /* its window, -1 when none        */
	short used;                    /* the slot holds a viewer         */
	short icon;                    /* shown as EHypView's icon        */
	short place[4];                /* where it was before (WM_ICONIFY) */
	char name[160], info[200];
	ehv_view view;
	playout lay;                   /* the page laid out (pagedraw.c)  */
} viewer;

#define MAX_VIEWERS 6
static viewer viewers[MAX_VIEWERS];
static viewer *cur = &viewers[0];

static ehv_bookmarks bookmarks;        /* EHYPVIEW.INF's BOOKMARKn */
static unsigned int bm_written;        /* how many BOOKMARKn lines it has */
#define RECENT_MAX 5
static ehv_bookmarks recent;           /* the pages read last, RECENTn */
static unsigned int recent_written;    /* how many RECENTn lines it has */
static int recent_changed;             /* not written yet */
static short menu_scr_w, menu_scr_h;   /* for building the menu bar again */

/*
 * The library's index: read from EHYPVIEW.IDX, then checked against the
 * folders, all in short steps between events (libjob.h, bgwork.h; the
 * AERA guidelines, 2.2).  The accessory starts on it when it is first
 * opened or asked for a page, or as START= in EHYPVIEW.INF says.
 */
static ehv_libjob *job;
static int job_over;                  /* done for this run (or failed) */
static int woken;                     /* opened, or asked for a page, once */
static int index_news;                /* the windows to show the index anew */
static long start_when = BG_OPENED;   /* START=: EHypView's default */
static char cache_path[200];
static void wake(void);
static int needs_index(const char *args);
static void job_wait_index(void);
static char start_dir[160];           /* where the program started: CRASH.LOG */
static char home_dir[160];            /* FOLDER=: EHYPVIEW.IDX and .FNT */
static char settings_path[40];        /* X:\EHYPVIEW.INF, the boot drive's root */

/*
 * Events for the emulator console (NatFeats NF_STDERR; a no-op on real
 * hardware).  tests/run_gui.py follows them to check what happened.
 */
void log_event(const char *what, const char *a, const char *b)
{
	char line[300];

	snprintf(line, sizeof(line), "EHV %s %s%s%s", what, a ? a : "", b ? " " : "", b ? b : "");
	crash_note(line + 4);             /* the last one goes in CRASH.LOG */
	strcat(line, "\n");
	nf_debug(line);
}

/* ---------------------------------------------------------------- */
/* page layout (pagedraw.c): the viewer's page                       */


static void make_layout(void)
{
	layout_make(&cur->lay, cur->view.doc, cur->view.page);
}

/* ---------------------------------------------------------------- */
/* the viewers                                                       */

static viewer *viewer_of(short h)
{
	int i;

	for (i = 0; h > 0 && i < MAX_VIEWERS; i++)
		if (viewers[i].used && viewers[i].h == h)
			return &viewers[i];
	return NULL;
}

/* how many viewer windows are open */
static int viewer_windows(void)
{
	int i, n = 0;

	for (i = 0; i < MAX_VIEWERS; i++)
		if (viewers[i].used && viewers[i].h > 0)
			n++;
	return n;
}

/* a free slot, made a viewer with no page yet; NULL when all are taken */
static viewer *viewer_new(void)
{
	int i;

	for (i = 0; i < MAX_VIEWERS; i++)
		if (!viewers[i].used)
		{
			viewer *v = &viewers[i];

			memset(v, 0, sizeof(*v));
			v->h = -1;
			v->used = 1;
			view_init(&v->view, idx);
			return v;
		}
	return NULL;
}

/* let a viewer go (its window is closed already) */
static void viewer_drop(viewer *v)
{
	layout_free(&v->lay);
	view_free(&v->view);
	memset(v, 0, sizeof(*v));
	v->h = -1;
}

static short top_window(void);

/*
 * The viewer to work on: the one in front, else the one last used while
 * it has a window, else any with a window.  With none, the one kept
 * without a window (its page comes back when it opens again).
 */
static void pick_current(void)
{
	viewer *v = viewer_of(top_window());
	int i;

	if (v != NULL)
	{
		cur = v;
		return;
	}
	if (cur->used && cur->h > 0)
		return;
	for (i = 0; i < MAX_VIEWERS; i++)
		if (viewers[i].used && viewers[i].h > 0)
		{
			cur = &viewers[i];
			return;
		}
	if (!cur->used)
	{
		for (i = 0; i < MAX_VIEWERS && !viewers[i].used; i++)
			;
		cur = i < MAX_VIEWERS ? &viewers[i] : viewer_new();
	}
}

/* ---------------------------------------------------------------- */
/* window geometry and scrolling                                     */

/* the toolbar of the current viewer's window, laid out */
static void bar(toolbar *t)
{
	short x, y, w, h;

	wind_get(cur->h, WF_WORKXYWH, &x, &y, &w, &h);
	tb_layout(t, x, y, w, sys_cw, sys_ch);
}

/* where the page is: the work area below the toolbar */
static void work_area(short *x, short *y, short *w, short *h)
{
	short th = tb_height(sys_ch);

	wind_get(cur->h, WF_WORKXYWH, x, y, w, h);
	*y = (short) (*y + th);
	*h = (short) (*h > th ? *h - th : 0);
}

static long visible_lines(void)
{
	short x, y, w, h;

	work_area(&x, &y, &w, &h);
	return h / ch > 0 ? h / ch : 1;
}

static long visible_cols(void)
{
	short x, y, w, h;

	work_area(&x, &y, &w, &h);
	return (w - MARGIN) / cw > 0 ? (w - MARGIN) / cw : 1;
}

static long page_cols(void)
{
	long c = cur->view.page ? cur->view.page->width : 0;

	if (cur->view.doc != NULL && cur->view.doc->width > c)
		c = cur->view.doc->width;
	return c;
}

static long max_top(void)
{
	long n = cur->view.page ? cur->view.page->nlines : 0, rows = visible_lines();

	return n > rows ? n - rows : 0;
}

static long max_left(void)
{
	long c = page_cols(), cols = visible_cols();

	return c > cols ? c - cols : 0;
}

static void clamp_scroll(void)
{
	if (cur->icon)                      /* the work area is the icon's */
		return;
	if (cur->view.top > max_top())
		cur->view.top = max_top();
	if (cur->view.top < 0)
		cur->view.top = 0;
	if (cur->view.left > max_left())
		cur->view.left = max_left();
	if (cur->view.left < 0)
		cur->view.left = 0;
}

static void set_sliders(void)
{
	long n = cur->view.page ? cur->view.page->nlines : 0, rows = visible_lines();
	long c = page_cols(), cols = visible_cols();
	long mt = max_top(), ml = max_left();

	if (cur->icon)
		return;
	wind_set(cur->h, WF_VSLSIZE, (short) (n > rows ? rows * 1000L / n : 1000), 0, 0, 0);
	wind_set(cur->h, WF_VSLIDE, (short) (mt > 0 ? cur->view.top * 1000L / mt : 0), 0, 0, 0);
	wind_set(cur->h, WF_HSLSIZE, (short) (c > cols ? cols * 1000L / c : 1000), 0, 0, 0);
	wind_set(cur->h, WF_HSLIDE, (short) (ml > 0 ? cur->view.left * 1000L / ml : 0), 0, 0, 0);
}

/* ---------------------------------------------------------------- */
/* drawing                                                           */

short link_color(void)
{
	return ncolors >= 16 ? 4 : 1;          /* blue, or black */
}

void fill(short color, short x1, short y1, short x2, short y2)
{
	short pxy[4];

	pxy[0] = x1;
	pxy[1] = y1;
	pxy[2] = x2;
	pxy[3] = y2;
	vswr_mode(vdi_h, MD_REPLACE);
	vsf_interior(vdi_h, FIS_SOLID);
	vsf_color(vdi_h, color);
	vsf_perimeter(vdi_h, 0);
	vr_recfl(vdi_h, pxy);
}

void text_at(short x, short y, const char *s, long n)
{
	while (n > 0)
	{
		short k = n > 120 ? 120 : (short) n;

		v_gtext_n(vdi_h, x, y, s, k);
		x = (short) (x + k * cw);
		s += k;
		n -= k;
	}
}

static void draw_message(short x, short y, const char *const *lines)
{
	vst_color(vdi_h, 1);
	vst_effects(vdi_h, 0);
	vswr_mode(vdi_h, MD_TRANS);
	for (; *lines != NULL; lines++, y = (short) (y + ch))
		text_at(x, y, *lines, (long) strlen(*lines));
}

/* draw the page within one rectangle (screen coordinates) */
static void draw_page(short cx, short cy, short cw_, short ch_)
{
	short wx, wy, ww, wh;
	short clip[4];
	const hyp_page *pg = cur->view.page;
	long oy;
	short ox;

	work_area(&wx, &wy, &ww, &wh);
	clip[0] = cx;
	clip[1] = cy;
	clip[2] = (short) (cx + cw_ - 1);
	clip[3] = (short) (cy + ch_ - 1);
	vs_clip(vdi_h, 1, clip);
	fill(0, clip[0], clip[1], clip[2], clip[3]);

	if (pg == NULL || cur->lay.line_y == NULL)
	{
		static const char *const help[] = {
			"No hypertext is open.",
			"",
			"Control+O  open a hypertext",
			"Control+Q  quit",
			NULL
		};

		draw_message((short) (wx + MARGIN + cw), (short) (wy + ch), help);
		vs_clip(vdi_h, 0, clip);
		return;
	}

	ox = (short) (wx + MARGIN - cur->view.left * cw);
	oy = wy - cur->lay.gfx_y[cur->view.top];
	layout_draw(&cur->lay, ox, oy, cur->view.top, cx, cy, cw_, ch_, cur->view.sel, page_cols());
	vs_clip(vdi_h, 0, clip);
}

/* ---------------------------------------------------------------- */
/* the toolbar (toolbar.c lays it out)                               */

/* what a button can do now */
static int bar_enabled(int b)
{
	switch (b)
	{
	case TB_BACK: return view_can(&cur->view, VIEW_BACK);
	case TB_FORWARD: return view_can(&cur->view, VIEW_FORWARD);
	case TB_HOME: return view_can(&cur->view, VIEW_HOME);
	case TB_PREV: return view_can(&cur->view, VIEW_PREV);
	case TB_TOC: return view_can(&cur->view, VIEW_TOC);
	case TB_NEXT: return view_can(&cur->view, VIEW_NEXT);
	case TB_INDEX: return view_can(&cur->view, VIEW_INDEX);
	case TB_SEEALSO: return view_can(&cur->view, VIEW_SEEALSO);
	}
	return 1;
}

static void vline_(short x, short y1, short y2)
{
	short p[4];

	p[0] = p[2] = x;
	p[1] = y1;
	p[3] = y2;
	v_pline(vdi_h, 2, p);
}

/* an arrow as ECal draws it: a solid head and a shaft, so it looks the
   same whatever the font; as wide as high, on tall pixels too */
static void draw_arrow(short x, short y, short w, short h, int dir)
{
	short hh = (short) ((h - 6) / 2), cy = (short) (y + h / 2), tw, sh, c, x0;

	if (hh < 2)
		hh = 2;
	tw = (short) (hh * 2 * sys_cw / sys_ch);
	if (tw < 3)
		tw = 3;
	sh = (short) (hh / 3);
	x0 = (short) (x + w / 2 + dir * tw);       /* the tip */
	for (c = 0; c <= tw; c++)
	{
		short half = (short) ((long) hh * c / tw);

		vline_((short) (x0 - dir * c), (short) (cy - half), (short) (cy + half));
	}
	for (c = tw; c <= 2 * tw; c++)
		vline_((short) (x0 - dir * c), (short) (cy - sh), (short) (cy + sh));
}

/* a house, for the main page: a solid roof over a framed wall and a door */
static void draw_house(short x, short y, short w, short h)
{
	short top = (short) (y + 3), bot = (short) (y + h - 4), cx = (short) (x + w / 2);
	short hw = (short) ((long) (bot - top) * sys_cw / sys_ch), rh = (short) ((bot - top) / 2), c, p[10];

	if (hw < 3)
		hw = 3;
	for (c = 0; c <= hw; c++)
	{
		short yy = (short) (top + (long) rh * c / hw);

		vline_((short) (cx - c), yy, (short) (top + rh));
		vline_((short) (cx + c), yy, (short) (top + rh));
	}
	p[0] = p[6] = p[8] = (short) (cx - hw + 2);
	p[1] = p[3] = p[9] = (short) (top + rh);
	p[2] = p[4] = (short) (cx + hw - 2);
	p[5] = p[7] = bot;
	v_pline(vdi_h, 5, p);
	fill(1, (short) (cx - hw / 4), (short) (bot - rh / 2 - 1), (short) (cx + hw / 4), bot);
	vswr_mode(vdi_h, MD_REPLACE);
}

/* the inside of a button greyed, as the AES greys what can't be used:
   every other pixel cleared */
static void dim(short x1, short y1, short x2, short y2)
{
	short pxy[4];

	pxy[0] = x1;
	pxy[1] = y1;
	pxy[2] = x2;
	pxy[3] = y2;
	vswr_mode(vdi_h, MD_TRANS);
	vsf_interior(vdi_h, FIS_PATTERN);
	vsf_style(vdi_h, 4);
	vsf_color(vdi_h, 0);
	vsf_perimeter(vdi_h, 0);
	vr_recfl(vdi_h, pxy);
	vsf_interior(vdi_h, FIS_SOLID);
	vswr_mode(vdi_h, MD_REPLACE);
}

static void draw_button(const toolbar *t, int b)
{
	short x = t->bx[b], y = t->by, w = t->bw[b], h = t->bh, p[10];
	const char *l = tb_label(b);

	fill(0, x, y, (short) (x + w - 1), (short) (y + h - 1));
	vswr_mode(vdi_h, MD_REPLACE);
	vsl_color(vdi_h, 1);
	vsl_type(vdi_h, 1);
	p[0] = p[6] = p[8] = x;
	p[1] = p[3] = p[9] = y;
	p[2] = p[4] = (short) (x + w - 1);
	p[5] = p[7] = (short) (y + h - 1);
	v_pline(vdi_h, 5, p);
	switch (tb_glyph(b))
	{
	case TB_ARROW_LEFT: draw_arrow(x, y, w, h, -1); break;
	case TB_ARROW_RIGHT: draw_arrow(x, y, w, h, 1); break;
	case TB_HOUSE: draw_house(x, y, w, h); break;
	default:
		vswr_mode(vdi_h, MD_TRANS);
		vst_color(vdi_h, 1);
		vst_effects(vdi_h, 0);
		text_at((short) (x + (w - (short) strlen(l) * cw) / 2), (short) (y + 1), l, (long) strlen(l));
	}
	if (!bar_enabled(b))
		dim((short) (x + 1), (short) (y + 1), (short) (x + w - 2), (short) (y + h - 2));
}

/* the toolbar within one rectangle (screen coordinates) */
static void draw_bar(short cx, short cy, short cw_, short ch_)
{
	toolbar t;
	short clip[4], p[4];
	int b;

	bar(&t);
	clip[0] = cx;
	clip[1] = cy;
	clip[2] = (short) (cx + cw_ - 1);
	clip[3] = (short) (cy + ch_ - 1);
	vs_clip(vdi_h, 1, clip);
	fill(0, t.x, t.y, (short) (t.x + t.w - 1), (short) (t.y + t.h - 1));
	font_sys_push();
	for (b = 0; b < TB_COUNT; b++)
		if (t.bw[b] > 0)
			draw_button(&t, b);
	font_sys_pop();
	vswr_mode(vdi_h, MD_REPLACE);
	vsl_color(vdi_h, 1);
	p[0] = t.x;
	p[2] = (short) (t.x + t.w - 1);
	p[1] = p[3] = (short) (t.y + t.h - 1);
	v_pline(vdi_h, 2, p);
	vs_clip(vdi_h, 0, clip);
}

/* the work area within one rectangle: the toolbar, the page */
static void draw_rect(short cx, short cy, short cw_, short ch_)
{
	short x, y, w, h, ix = cx, iy = cy, iw = cw_, ih = ch_;

	wind_get(cur->h, WF_WORKXYWH, &x, &y, &w, &h);
	if (intersect(&ix, &iy, &iw, &ih, x, y, w, tb_height(sys_ch)))
		draw_bar(ix, iy, iw, ih);
	work_area(&x, &y, &w, &h);
	ix = cx;
	iy = cy;
	iw = cw_;
	ih = ch_;
	if (h > 0 && intersect(&ix, &iy, &iw, &ih, x, y, w, h))
		draw_page(ix, iy, iw, ih);
}

int intersect(short *x, short *y, short *w, short *h, short x2, short y2, short w2, short h2)
{
	short l = *x > x2 ? *x : x2;
	short t = *y > y2 ? *y : y2;
	short r = (*x + *w < x2 + w2) ? *x + *w : x2 + w2;
	short b = (*y + *h < y2 + h2) ? *y + *h : y2 + h2;

	if (r <= l || b <= t)
		return 0;
	*x = l;
	*y = t;
	*w = (short) (r - l);
	*h = (short) (b - t);
	return 1;
}

/* redraw an area of the window through the AES rectangle list */
static void redraw(short x, short y, short w, short h)
{
	short rx, ry, rw, rh;

	if (cur->h <= 0)
		return;
	if (cur->icon)
	{
		app_icon_redraw(cur->h, x, y, w, h);
		return;
	}
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, NULL);
	wind_get(cur->h, WF_FIRSTXYWH, &rx, &ry, &rw, &rh);
	while (rw > 0 && rh > 0)
	{
		short ix = rx, iy = ry, iw = rw, ih = rh;

		if (intersect(&ix, &iy, &iw, &ih, x, y, w, h) &&
			intersect(&ix, &iy, &iw, &ih, desk_x, desk_y, desk_w, desk_h))
			draw_rect(ix, iy, iw, ih);
		wind_get(cur->h, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
	}
	graf_mouse(M_ON, NULL);
	wind_update(END_UPDATE);
}

static void redraw_all(void)
{
	short x, y, w, h;

	if (cur->icon)                      /* nothing of the page shows */
		return;
	wind_get(cur->h, WF_WORKXYWH, &x, &y, &w, &h);   /* the toolbar too */
	clamp_scroll();
	set_sliders();
	redraw(x, y, w, h);
}

/* the iconified window as it was: a page asked for, the menu */
static void restore(void)
{
	if (cur->h <= 0 || !cur->icon)
		return;
	wind_set(cur->h, WF_UNICONIFY, cur->place[0], cur->place[1], cur->place[2], cur->place[3]);
	cur->icon = 0;
	log_event("uniconify", "viewer", NULL);
	clamp_scroll();
	set_sliders();
}

/* ---------------------------------------------------------------- */
/* window name, info line, page changes                              */

static void update_info(void)
{
	if (cur->h <= 0)
		return;
	if (cur->view.status[0] != '\0')
		snprintf(cur->info, sizeof(cur->info), " %s", cur->view.status);
	else if (scanning())
		snprintf(cur->info, sizeof(cur->info), " %s", index_busy());
	else if (cur->view.page != NULL)
		snprintf(cur->info, sizeof(cur->info), " %s", cur->view.doc->path);
	else
		snprintf(cur->info, sizeof(cur->info), " %lu hypertexts known", idx ? idx->nguides : 0UL);
	wind_set_str(cur->h, WF_INFO, cur->info);
}

/* the page shown: for the tests, and first of the pages read last
   (written when its window closes, or at the end) */
static void page_read(void)
{
	char title[160], *t;
	size_t len;

	if (cur->view.page == NULL)
		return;
	log_event("page", cur->view.doc->path, cur->view.doc->entries[cur->view.page->num].name);
	view_title(&cur->view, title, sizeof(title));        /* " database - page " */
	for (t = title; *t == ' '; t++)
		;
	for (len = strlen(t); len > 0 && t[len - 1] == ' '; len--)
		t[len - 1] = '\0';
	if (bm_touch(&recent, cur->view.doc->path, cur->view.doc->entries[cur->view.page->num].name, t,
		     RECENT_MAX) > 0)
	{
		recent_changed = 1;
		libwin_recent(&recent);
	}
}

/* after the view changed page */
static void page_changed(void)
{
	restore();
	page_read();
	make_layout();
	clamp_scroll();
	if (cur->h > 0)
	{
		view_title(&cur->view, cur->name, sizeof(cur->name));
		wind_set_str(cur->h, WF_NAME, cur->name);
		update_info();
		redraw_all();
	}
}

/* ---------------------------------------------------------------- */
/* pop-ups: a @pnode in a framed box over the page, as ST-Guide shows */

#define POP_PAD 4                  /* pixels between the frame and the text */

static void after(int rc);
static void close_window(void);
static void recent_save(void);
static int library_layout(void);

/* the screen position of link run r of the page (its top left) */
static void link_pos(long r, short *x, short *y)
{
	short wx, wy, ww, wh;
	long line = view_run_line(&cur->view, r), col = 0;
	unsigned short i;

	work_area(&wx, &wy, &ww, &wh);
	if (line < 0 || cur->lay.line_y == NULL)
	{
		*x = (short) (wx + ww / 4);       /* no link to go by: the window's middle */
		*y = (short) (wy + wh / 4);
		return;
	}
	for (i = 0; i < cur->view.page->lines[line].nruns; i++)
	{
		unsigned long k = cur->view.page->lines[line].first_run + i;

		if ((long) k == r)
			break;
		col += cur->view.page->runs[k].len;
	}
	*x = (short) (wx + MARGIN + (col - cur->view.left) * cw);
	*y = (short) (wy + cur->lay.line_y[line] - cur->lay.gfx_y[cur->view.top]);
}

static void pop_wait_up(void)
{
	short mx, my, mb, ks;

	do
	{
		graf_mkstate(&mx, &my, &mb, &ks);
		if (mb & 3)
			evnt_timer_(10);
	} while (mb & 3);
}

/*
 * Show the viewer's pop-up next to link run `r` (below it, or above it when there
 * is no room) until a key or a click.  A click on one of its links
 * follows that link; anything else closes it, and the page stays.
 */
static void popup_box(long r)
{
	playout pl;
	short ax, ay, bx, by, bw, bh, clip[4], m[8], done = 0;
	long cols = view_popup_cols(&cur->view), follow = -1;
	const char *name = cur->view.doc->entries[cur->view.popup_node].name;
	AEVENT e;

	memset(&pl, 0, sizeof(pl));
	layout_make(&pl, cur->view.doc, cur->view.popup);
	if (pl.pg == NULL)
	{
		view_popup_close(&cur->view);
		return;
	}
	bw = (short) (layout_width(&pl, cols) + 2 * POP_PAD + 2);
	bh = (short) (layout_height(&pl) + 2 * POP_PAD + 2);
	if (bw > desk_w - 8)
		bw = (short) (desk_w - 8);
	if (bh > desk_h - 8)
		bh = (short) (desk_h - 8);
	if (r >= 0)
		link_pos(r, &ax, &ay);
	else
		link_pos(-1, &ax, &ay);
	bx = (short) (ax - POP_PAD - 1);
	by = (short) (ay + ch + 2);
	if (by + bh + 3 > desk_y + desk_h)
		by = (short) (ay - bh - 2);           /* no room below the link: above it */
	if (by < desk_y)
		by = desk_y;
	if (bx + bw + 3 > desk_x + desk_w)
		bx = (short) (desk_x + desk_w - bw - 3);
	if (bx < desk_x)
		bx = desk_x;

	wind_update(BEG_UPDATE);
	wind_update(3);                            /* BEG_MCTRL: the mouse is ours */
	form_dial(FMD_START, bx, by, (short) (bw + 3), (short) (bh + 3));
	graf_mouse(M_OFF, NULL);
	clip[0] = bx;
	clip[1] = by;
	clip[2] = (short) (bx + bw + 2);
	clip[3] = (short) (by + bh + 2);
	vs_clip(vdi_h, 1, clip);
	fill(1, (short) (bx + 3), (short) (by + 3), (short) (bx + bw + 2), (short) (by + bh + 2));  /* shadow */
	fill(1, bx, by, (short) (bx + bw - 1), (short) (by + bh - 1));                          /* frame */
	fill(0, (short) (bx + 1), (short) (by + 1), (short) (bx + bw - 2), (short) (by + bh - 2));
	clip[0] = (short) (bx + 1);
	clip[1] = (short) (by + 1);
	clip[2] = (short) (bx + bw - 2);
	clip[3] = (short) (by + bh - 2);
	vs_clip(vdi_h, 1, clip);
	layout_draw(&pl, (short) (bx + 1 + POP_PAD), by + 1 + POP_PAD, 0,
		clip[0], clip[1], (short) (bw - 2), (short) (bh - 2), -1, cols);
	vs_clip(vdi_h, 0, clip);
	graf_mouse(M_ON, NULL);
	graf_mouse(ARROW, NULL);                   /* a link click left the busy bee */
	log_event("popup", name, NULL);
	pop_wait_up();

	while (!done)
	{
		evnt_multi_(MU_KEYBD | MU_BUTTON, 0x101, 3, 0, 0, m, &e);
		if (e.which & MU_KEYBD)
			done = 1;
		if (e.which & MU_BUTTON)
		{
			/* (not by e.mbutton: after a quick click it is 0 again) */
			if (e.mx > bx && e.my > by && e.mx < bx + bw - 1 && e.my < by + bh - 1)
			{
				long line = layout_line_at(&pl, e.my - (by + 1 + POP_PAD), 0);
				long x = e.mx - (bx + 1 + POP_PAD);

				if (line >= 0 && x >= 0)
					follow = view_popup_link_at(&cur->view, line, x / cw);
			}
			pop_wait_up();
			done = 1;
		}
	}
	form_dial(FMD_FINISH, bx, by, (short) (bw + 3), (short) (bh + 3));
	wind_update(2);                            /* END_MCTRL */
	wind_update(END_UPDATE);
	layout_free(&pl);
	if (follow >= 0)
	{
		log_event("popup-link", name, NULL);
		graf_mouse(BUSYBEE, NULL);
		after(view_popup_follow(&cur->view, follow));
		graf_mouse(ARROW, NULL);
	} else
	{
		log_event("popup-closed", name, NULL);
		view_popup_close(&cur->view);
	}
}

/* show a navigation result */
static void after(int rc)
{
	if (rc == VIEW_CLOSE)
	{
		/* a "quit" link: its window closes; the program quits with its
		   last one (the accessory stays) */
		if (is_acc || viewer_windows() > 1)
			close_window();
		else
			quit_now = 1;
		return;
	}
	if (rc == VIEW_OK)
		page_changed();
	else if (rc == VIEW_POPUP)
		popup_box(cur->view.sel);
	else
	{
		log_event("status", cur->view.status, NULL);
		update_info();
	}
}

static void open_window(void)
{
	short x, y, w, h, cols;

	if (cur->h > 0)
	{
		restore();
		wind_set(cur->h, WF_TOP, cur->h, 0, 0, 0);
		return;
	}
	cur->h = wind_create(icon_kind(WKIND), desk_x, desk_y, desk_w, desk_h);
	cur->icon = 0;
	if (cur->h <= 0)
	{
		cur->h = -1;
		alert(1, "[3][The system has no window left for EHypView.|Close a window and try again.][ OK ]");
		return;
	}
	cols = (short) (cur->view.doc && cur->view.doc->width ? cur->view.doc->width : 75);
	w = (short) (cols * cw + 2 * MARGIN);
	if (w < tb_full_width(sys_cw))         /* room for every button */
		w = tb_full_width(sys_cw);
	wind_calc(WC_BORDER, WKIND, 0, 0, w, (short) (desk_h * 3 / 4), &x, &y, &w, &h);
	if (w > desk_w)
		w = desk_w;
	if (h > desk_h)
		h = desk_h;
	x = (short) (desk_x + (desk_w - w) / 2);
	y = (short) (desk_y + (desk_h - h) / 3);
	{
		/* each further window a little down and to the right (this
		   one, just made, is counted too) */
		short step = (short) (2 * sys_ch), n = (short) (viewer_windows() - 1);

		if (x + w + n * step <= desk_x + desk_w)
			x = (short) (x + n * step);
		if (y + h + n * step <= desk_y + desk_h)
			y = (short) (y + n * step);
	}
	view_title(&cur->view, cur->name, sizeof(cur->name));
	wind_set_str(cur->h, WF_NAME, cur->name);
	update_info();
	wind_open(cur->h, x, y, w, h);
	if (is_acc)                        /* the desktop drops files on it */
		av_window(cur->h, 1);
	clamp_scroll();
	set_sliders();
}

void app_window_closed(void)
{
	/* with a menu bar, the program stays until Quit */
	if (!is_acc && !has_menu && viewer_windows() == 0 && libwin_handle() <= 0)
		quit_now = 1;
}

/*
 * Close the current viewer's window.  While others are open it goes; the
 * last one keeps its page and history for when it opens again.
 */
static void close_window(void)
{
	viewer *closed = cur;

	if (cur->h > 0)
	{
		if (is_acc)
			av_window(cur->h, 0);
		wind_close(cur->h);
		wind_delete(cur->h);
		log_event("closed", "viewer", NULL);
		recent_save();
	}
	cur->h = -1;
	cur->icon = 0;
	if (viewer_windows() > 0)
	{
		viewer_drop(closed);
		pick_current();
	}
	app_window_closed();
}

/* all viewer windows (Quit in the accessory) */
static void close_all_windows(void)
{
	int i;

	for (i = 0; i < MAX_VIEWERS; i++)
		if (viewers[i].used && viewers[i].h > 0)
		{
			cur = &viewers[i];
			close_window();
		}
}

short viewer_handle(void)
{
	pick_current();
	return cur->h;
}

void viewer_top(void)
{
	restore();
	if (cur->h > 0)
		wind_set(cur->h, WF_TOP, cur->h, 0, 0, 0);
}

void viewer_show(const char *path, const char *node, unsigned short line)
{
	int rc = view_open(&cur->view, path, node, line);

	if (cur->h <= 0)
	{
		make_layout();
		open_window();
		if (rc != VIEW_OK)
			log_event("status", cur->view.status, NULL);
		else
			page_read();
		return;
	}
	viewer_top();
	after(rc);
}

/* ---------------------------------------------------------------- */
/* scrolling and navigation                                          */

static void scroll_to(long top, long left)
{
	long ot = cur->view.top, ol = cur->view.left;

	cur->view.top = top;
	cur->view.left = left;
	clamp_scroll();
	if (cur->view.top != ot || cur->view.left != ol)
	{
		short x, y, w, h;

		set_sliders();
		work_area(&x, &y, &w, &h);
		redraw(x, y, w, h);
	}
}

static void show_line(long line)
{
	long rows = visible_lines();

	if (line < 0)
		return;
	if (line < cur->view.top)
		scroll_to(line, cur->view.left);
	else if (line >= cur->view.top + rows)
		scroll_to(line - rows + 1, cur->view.left);
}

static void select_link(int dir)
{
	long rows = visible_lines();
	long line = view_select_link(&cur->view, dir, cur->view.top, cur->view.top + rows - 1);

	if (line >= 0)
	{
		char num[16];

		sprintf(num, "%ld", line);
		log_event("select", num, NULL);
		show_line(line);
	}
	redraw_all();
}

static void open_file_selector(void)
{
	static char path[200], name[64];
	short button;

	if (path[0] == '\0')
	{
		path[0] = (char) ('A' + Dgetdrv());
		path[1] = ':';
		Dgetpath(path + 2, 0);
		strcat(path, "\\*.HYP");
	}
	if (fsel_exinput(path, name, &button, "Open hypertext") && button && name[0] != '\0')
	{
		char full[260];
		char *slash = strrchr(path, '\\');
		size_t n = slash ? (size_t) (slash - path + 1) : 0;

		memcpy(full, path, n);
		strcpy(full + n, name);
		graf_mouse(BUSYBEE, NULL);
		viewer_show(full, NULL, 0);
		graf_mouse(ARROW, NULL);
	}
}

static short top_window(void)
{
	short top, d;

	wind_get(0, WF_TOP, &top, &d, &d, &d);
	return top;
}

/* the VIEW_ target of a navigation command, or -1 */
static int nav_target(int cmd)
{
	switch (cmd)
	{
	case CMD_BACK: return VIEW_BACK;
	case CMD_FORWARD: return VIEW_FORWARD;
	case CMD_SEEALSO: return VIEW_SEEALSO;
	case CMD_HOME: return VIEW_HOME;
	case CMD_INDEX: return VIEW_INDEX;
	case CMD_TOC: return VIEW_TOC;
	case CMD_HELP: return VIEW_HELP;
	case CMD_PREV: return VIEW_PREV;
	case CMD_NEXT: return VIEW_NEXT;
	}
	return -1;
}

/* ---------------------------------------------------------------- */
/* several windows, bookmarks                                        */

static void bookmarks_changed(void);
static void bookmarks_load(void);
static void update_menu(void);

/* the link run the selection is on, or -1 */
static long selected_link(void)
{
	const hyp_page *pg = cur->view.page;

	if (pg == NULL || cur->view.sel < 0 || (unsigned long) cur->view.sel >= pg->nruns ||
	    pg->runs[cur->view.sel].kind != HYP_RUN_LINK)
		return -1;
	return cur->view.sel;
}

/* the link run under the mouse in the current viewer, or -1 */
static long link_under(short mx, short my)
{
	short x, y, w, h;
	long line;

	if (cur->view.page == NULL || cur->lay.line_y == NULL)
		return -1;
	work_area(&x, &y, &w, &h);
	if (mx < x + MARGIN || my < y || mx >= x + w || my >= y + h)
		return -1;
	line = layout_line_at(&cur->lay, my - y + cur->lay.gfx_y[cur->view.top], cur->view.top);
	if (line < 0)
		return -1;
	return view_link_at(&cur->view, line, (mx - x - MARGIN) / cw + cur->view.left);
}

/* every viewer's info line again (the index scan moves on) */
static void info_everywhere(void)
{
	viewer *keep = cur;
	int i;

	for (i = 0; i < MAX_VIEWERS; i++)
		if (viewers[i].used && viewers[i].h > 0)
		{
			cur = &viewers[i];
			update_info();
		}
	cur = keep;
}

/* a new index: every viewer resolves links with it */
static void set_index_everywhere(void)
{
	int i;

	for (i = 0; i < MAX_VIEWERS; i++)
		if (viewers[i].used)
			viewers[i].view.idx = idx;
}

/*
 * A new viewer window with the current page in it, where it was
 * scrolled to; follow: the selected link, followed in the new window.
 */
static void new_window(int follow)
{
	viewer *from = cur, *v;
	int rc = VIEW_OK;
	char n[8];

	if (follow && selected_link() < 0)
		return;
	v = viewer_new();
	if (v == NULL)
	{
		char k[8];

		log_event("alert", "six-windows", NULL);
		sprintf(k, "%d", alert(1, "[1][EHypView shows at most|six windows.|Close one first.][ OK ]"));
		log_event("alert-closed", "six-windows", k);
		return;
	}
	graf_mouse(BUSYBEE, NULL);
	if (from->view.page != NULL)
	{
		rc = view_open(&v->view, from->view.doc->path, from->view.doc->entries[from->view.page->num].name,
			follow ? 0 : (unsigned short) from->view.top);
		if (rc == VIEW_OK && follow)
		{
			v->view.sel = from->view.sel;
			rc = view_follow_selected(&v->view);
		}
	}
	graf_mouse(ARROW, NULL);
	if (rc != VIEW_OK)
	{
		/* a pop-up, or a link that leads nowhere: shown where it was;
		   a "quit" link opens nothing */
		viewer_drop(v);
		cur = from;
		if (follow && rc != VIEW_CLOSE)
			after(view_follow_selected(&cur->view));
		return;
	}
	cur = v;
	make_layout();
	open_window();
	if (cur->h <= 0)
	{
		viewer_drop(v);
		cur = from;
		return;
	}
	sprintf(n, "%d", viewer_windows());
	log_event("window", "new", n);
	page_read();
}

/* the bookmark of the current page, or -1 */
static int page_bookmark(void)
{
	if (cur->h <= 0 || cur->view.page == NULL)
		return -1;
	return bm_find(&bookmarks, cur->view.doc->path, cur->view.doc->entries[cur->view.page->num].name);
}

static void set_status(const char *text)
{
	snprintf(cur->view.status, sizeof(cur->view.status), "%s", text);
	log_event("status", text, NULL);
	update_info();
}

/* ^D: the current page */
static void bookmark_add(void)
{
	char title[160], *t;
	unsigned int had = bookmarks.n;
	size_t len;
	int i;

	if (cur->h <= 0 || cur->view.page == NULL)
		return;
	view_title(&cur->view, title, sizeof(title));        /* " database - page " */
	for (t = title; *t == ' '; t++)
		;
	for (len = strlen(t); len > 0 && t[len - 1] == ' '; len--)
		t[len - 1] = '\0';
	i = bm_add(&bookmarks, cur->view.doc->path, cur->view.doc->entries[cur->view.page->num].name, t);
	if (i >= 0 && bookmarks.n > had)
	{
		log_event("bookmark", "added", bookmarks.b[i].title);
		bookmarks_changed();
		set_status("Bookmarked: in the Bookmarks menu and the library.");
	} else if (i >= 0)
		set_status("This page is bookmarked already.");
	else if (bookmarks.n >= BM_MAX)
		set_status("All ten bookmarks are in use: remove one first.");
	else
		set_status("Couldn't add the bookmark.");
}

/* remove bookmark i (the Bookmarks menu, Delete in the library) */
void app_bookmark_remove(int i)
{
	char title[BM_TITLE_MAX + 1];

	if (i < 0 || (unsigned int) i >= bookmarks.n)
		return;
	strcpy(title, bookmarks.b[i].title);
	bm_remove(&bookmarks, (unsigned int) i);
	log_event("bookmark", "removed", title);
	bookmarks_changed();
	if (cur->h > 0)
		set_status("Bookmark removed.");
}

/* the menu bar again, with the bookmarks as they are now */
static void menu_rebuild(void)
{
	ehv_menu fresh;

	if (!has_menu || menu_build(&fresh, &bookmarks, sys_cw, sys_ch, menu_scr_w, menu_scr_h) != 0)
		return;
	menu_bar(fresh.tree, 1);
	menu_free(&menu);
	menu = fresh;
	memset(menu_on, -1, sizeof(menu_on));
	update_menu();
}

/* a menu item or its shortcut */
/* ^C ^X ^V: in the library, its search line (^C: the hypertext chosen);
   in the viewer, ^C copies the page, as text */
static void clip_command(int cmd)
{
	int what = cmd == CMD_COPY ? CLIP_COPY : cmd == CMD_CUT ? CLIP_CUT : CLIP_PASTE;
	short top = top_window();
	char *t;
	unsigned long n;

	if (top > 0 && top == libwin_handle())
	{
		libwin_clip(what);
		return;
	}
	if (what != CLIP_COPY || cur->h <= 0 || cur->view.page == NULL || (t = malloc(32000)) == NULL)
		return;
	n = view_page_text(&cur->view, t, 32000);
	if (clip_put(t, (long) n))
	{
		char lines[16];

		sprintf(lines, "%u", cur->view.page->nlines);
		log_event("copy", "page", lines);
		snprintf(cur->view.status, sizeof(cur->view.status), "The page is on the clipboard.");
	} else
	{
		snprintf(cur->view.status, sizeof(cur->view.status), "Couldn't write the clipboard.");
	}
	free(t);
	update_info();
}

static void seealso_here(void);
static void job_again(void);
static void show_request(const char *args);

static void command(int cmd)
{
	int target = nav_target(cmd), rc;

	switch (cmd)
	{
	case CMD_CUT:
	case CMD_COPY:
	case CMD_PASTE:
		clip_command(cmd);
		return;
	case CMD_ABOUT:
		about_box();
		return;
	case CMD_OPEN:
		open_file_selector();
		return;
	case CMD_LIBRARY:
		libwin_open();
		return;
	case CMD_CLOSE:
		if (libwin_handle() > 0 && (top_window() == libwin_handle() || viewer_windows() == 0))
			libwin_close();
		else
			close_window();
		return;
	case CMD_NEWWIN:
		new_window(0);
		return;
	case CMD_LINKWIN:
		new_window(1);
		return;
	case CMD_BM_ADD:
		bookmark_add();
		return;
	case CMD_BM_REMOVE:
		if (cur->h > 0 && cur->view.page != NULL)
			app_bookmark_remove(bm_find(&bookmarks, cur->view.doc->path,
				cur->view.doc->entries[cur->view.page->num].name));
		return;
	case CMD_SEEALSO:
		seealso_here();
		return;
	case CMD_FONT:
		font_dialog();
		return;
	case CMD_SETTINGS:
		{
			int changed = settings_dialog(settings_path, home_dir);

			if (changed & SETTINGS_LAYOUT)
				libwin_layout(library_layout());
			if (changed & SETTINGS_PATHS)
				job_again();
		}
		return;
	case CMD_GUIDE:
		app_guide(NULL);
		return;
	case CMD_LARGER:
	case CMD_SMALLER:
		if (!font_step(cmd == CMD_LARGER ? 1 : -1))
			log_event("status", cmd == CMD_LARGER ? "No larger size" : "No smaller size", NULL);
		return;
	case CMD_QUIT:
		if (is_acc)
		{
			libwin_close();
			close_all_windows();
		} else
		{
			quit_now = 1;
		}
		return;
	}
	if (cmd >= CMD_BM0 && cmd < CMD_COUNT)
	{
		const ehv_bookmark *b = (unsigned int) (cmd - CMD_BM0) < bookmarks.n ? &bookmarks.b[cmd - CMD_BM0] : NULL;

		if (b != NULL)
		{
			graf_mouse(BUSYBEE, NULL);
			viewer_show(b->path, b->node, 0);
			graf_mouse(ARROW, NULL);
		}
		return;
	}
	if (target < 0 || cur->h <= 0)
		return;
	viewer_top();
	graf_mouse(BUSYBEE, NULL);
	rc = target == VIEW_BACK ? view_back(&cur->view) : target == VIEW_FORWARD ? view_forward(&cur->view) :
		view_relative(&cur->view, target);
	graf_mouse(ARROW, NULL);
	after(rc);
}

/* grey out what cannot be done now */
static void update_menu(void)
{
	int c;

	if (!has_menu)
		return;
	pick_current();
	for (c = CMD_ABOUT; c < CMD_COUNT; c++)
	{
		int on = 1, target = nav_target(c);
		short obj;

		if (c == CMD_CLOSE)
			on = viewer_windows() > 0 || libwin_handle() > 0;
		else if (c == CMD_NEWWIN)
			on = viewer_windows() < MAX_VIEWERS;
		else if (c == CMD_LINKWIN)
			on = cur->h > 0 && selected_link() >= 0 && viewer_windows() < MAX_VIEWERS;
		else if (c == CMD_BM_ADD)
			on = cur->h > 0 && cur->view.page != NULL && bookmarks.n < BM_MAX;
		else if (c == CMD_BM_REMOVE)
			on = page_bookmark() >= 0;
		else if (c == CMD_CUT || c == CMD_PASTE)
			on = libwin_handle() > 0 && top_window() == libwin_handle();
		else if (c == CMD_COPY)
			on = (libwin_handle() > 0 && top_window() == libwin_handle()) || (cur->h > 0 && cur->view.page != NULL);
		else if (target >= 0)
			on = cur->h > 0 && view_can(&cur->view, target);
		if (menu_on[c] != on && (obj = menu_object(&menu, c)) >= 0)
		{
			menu_ienable(menu.tree, obj, (short) on);
			menu_on[c] = (signed char) on;
		}
	}
}

static void log_font(void)
{
	char pt[8];

	sprintf(pt, "%d", font_point());
	log_event("font", font_name(), pt);
}

/* the font changed: lay out and redraw */
void app_font_changed(void)
{
	viewer *keep = cur;
	int i;

	log_font();
	for (i = 0; i < MAX_VIEWERS; i++)
	{
		if (!viewers[i].used)
			continue;
		cur = &viewers[i];
		make_layout();
		if (cur->h > 0)
		{
			clamp_scroll();
			set_sliders();
			redraw_all();
		}
	}
	cur = keep;
	libwin_font_changed();
}

/* shortcuts that work whichever window (or none) is on top; Control by
   key_ctrl(): a ^key typed while busy comes with kstate 0 (guidelines 3.2) */
int app_key(short kstate, short kreturn)
{
	if (!key_ctrl(kstate, kreturn))
		return 0;
	/* by scan code: with Control, '-' gives the code of Return */
	switch (KEY_SCAN(kreturn))
	{
	case 0x0d:                                        /* ^= (^+ on many keyboards) */
	case 0x1b:                                        /* ^+ on German keyboards */
	case 0x4e: command(CMD_LARGER); return 1;         /* ^+ on the keypad */
	case 0x0c:                                        /* ^- (^ß on German keyboards) */
	case 0x35:                                        /* ^- on German keyboards */
	case 0x4a: command(CMD_SMALLER); return 1;        /* ^- on the keypad */
	case 0x33: command(CMD_SETTINGS); return 1;       /* ^, (its code is ^L's) */
	}
	switch (KEY_ASCII(kreturn))
	{
	case 0x06:                                        /* Shift+^F */
		if (!(kstate & K_SHIFT))
			return 0;
		command(CMD_FONT);
		return 1;
	case 0x0e: command(CMD_NEWWIN); return 1;         /* ^N */
	case 0x04: command(CMD_BM_ADD); return 1;         /* ^D */
	case 0x11: command(CMD_QUIT); return 1;          /* ^Q */
	case 0x15:                                        /* ^U */
	case 0x17: command(CMD_CLOSE); return 1;          /* ^W */
	case 0x0f: command(CMD_OPEN); return 1;           /* ^O */
	case 0x0c: command(CMD_LIBRARY); return 1;        /* ^L */
	case 0x03: command(CMD_COPY); return 1;           /* ^C */
	case 0x18: command(CMD_CUT); return 1;            /* ^X */
	case 0x16: command(CMD_PASTE); return 1;          /* ^V */
	}
	return 0;
}

static void context_menu(short x, short y);

static void key(short kstate, short kreturn)
{
	short scan = KEY_SCAN(kreturn), ascii = KEY_ASCII(kreturn);
	int shift = (kstate & K_SHIFT) != 0, ctrl = key_ctrl(kstate, kreturn);
	long rows = visible_lines();
	int rc = -2;

	if (ctrl && ascii != 0x09 && app_key(kstate, kreturn))
		return;
	if (scan == 0x52)                  /* Insert: the right button's menu */
	{
		short x, y, w, h;

		work_area(&x, &y, &w, &h);
		context_menu(x + 2 * cw, y + ch);
		return;
	}
	switch (ascii)
	{
	case 0x09:                         /* ^I and Tab share 9: Tab has scan 0x0f */
		if (scan != 0x0f)
		{
			command(CMD_INDEX);
			return;
		}
		select_link(shift ? -1 : 1);
		return;
	case 0x14:                         /* ^T */
		command(CMD_TOC);
		return;
	case 0x0d:                         /* Return; with Shift, in a new window */
		if (shift)
		{
			command(CMD_LINKWIN);
			return;
		}
		rc = view_follow_selected(&cur->view);
		break;
	case 0x08:                         /* Backspace */
		rc = view_back(&cur->view);
		break;
	}
	if (rc == -2)
	{
		switch (scan)
		{
		case 0x61:                     /* Undo; with Shift, forward again */
			command(shift ? CMD_FORWARD : CMD_BACK);
			return;
		case 0x62:                     /* Help */
			command(CMD_HELP);
			return;
		case 0x72:                     /* Enter */
			if (shift)
			{
				command(CMD_LINKWIN);
				return;
			}
			rc = view_follow_selected(&cur->view);
			break;
		case 0x73:                     /* ^Left */
			command(CMD_PREV);
			return;
		case 0x74:                     /* ^Right */
			command(CMD_NEXT);
			return;
		case 0x48:                     /* Up */
			scroll_to(shift ? cur->view.top - rows : cur->view.top - 1, cur->view.left);
			return;
		case 0x50:                     /* Down */
			scroll_to(shift ? cur->view.top + rows : cur->view.top + 1, cur->view.left);
			return;
		case 0x4b:                     /* Left */
			scroll_to(cur->view.top, shift ? cur->view.left - visible_cols() : cur->view.left - 1);
			return;
		case 0x4d:                     /* Right */
			scroll_to(cur->view.top, shift ? cur->view.left + visible_cols() : cur->view.left + 1);
			return;
		case 0x47:                     /* Clr/Home */
			if (ctrl)
				command(CMD_HOME);
			else
				scroll_to(shift ? max_top() : 0, 0);
			return;
		default:
			return;
		}
	}
	after(rc);
}

/*
 * The right button (or Insert): a menu of what the menu bar has for the
 * page, Back first (the guidelines, 3.3, for viewers) and About last,
 * which is where the accessory, without a menu bar, finds it.
 */
static void context_menu(short x, short y)
{
	static const struct
	{
		const char *label;
		int cmd;
	} all[] = {
		{ "  Back                  Undo", CMD_BACK },
		{ "  Forward             \x01Undo", CMD_FORWARD },
		{ "  Link in new window \x01Return", CMD_LINKWIN },
		{ "-", CMD_NONE },
		{ "  Main page            ^Home", CMD_HOME },
		{ "  Contents                ^T", CMD_TOC },
		{ "  Index                   ^I", CMD_INDEX },
		{ "  Help page             Help", CMD_HELP },
		{ "-", CMD_NONE },
		{ "  New window              ^N", CMD_NEWWIN },
		{ "  Add bookmark            ^D", CMD_BM_ADD },
		{ "  Copy page               ^C", CMD_COPY },
		{ "  Library and search      ^L", CMD_LIBRARY },
		{ "  Font...                \x01^F", CMD_FONT },
		{ "-", CMD_NONE },
		{ "  About EHypView...", CMD_ABOUT }
	};
	enum { N = sizeof(all) / sizeof(all[0]) };
	static char off[N][40];
	const char *lab[N];
	int cmds[N];
	short i, n = 0, r;

	for (i = 0; i < N; i++)
	{
		int c = all[i].cmd, target = nav_target(c), grey;

		if (c == CMD_LINKWIN && selected_link() < 0)
			continue;                  /* only on a link */
		grey = (target >= 0 && !view_can(&cur->view, target)) ||
			((c == CMD_NEWWIN || c == CMD_LINKWIN) && viewer_windows() >= MAX_VIEWERS) ||
			(c == CMD_BM_ADD && (cur->view.page == NULL || page_bookmark() >= 0 || bookmarks.n >= BM_MAX));
		lab[n] = all[i].label;
		if (grey)
		{
			off[n][0] = '~';           /* greyed: popup.h */
			strcpy(off[n] + 1, all[i].label);
			lab[n] = off[n];
		}
		cmds[n++] = c;
	}
	log_event("menu", "popup", NULL);
	r = popup(x, y, lab, n);
	if (r < 0 || cmds[r] == CMD_NONE)
		return;
	log_event("menu", menu_command_name(cmds[r]), NULL);
	command(cmds[r]);
}

/* ---------------------------------------------------------------- */
/* the toolbar's buttons, menus and tooltips                         */

/* a menu line: the text, then its shortcut on the right */
static void menu_line(char *buf, size_t n, const char *text, const char *key)
{
	snprintf(buf, n, "  %-20s%6s ", text, key != NULL ? key : "");
}

/* the history, back (dir < 0) or forward (dir > 0), nearest first */
#define HIST_SHOWN 12
static void history_menu(int dir, short x, short y)
{
	static char lines[HIST_SHOWN][48];
	const char *lab[HIST_SHOWN], *t;
	short n = 0, r;
	char num[8];
	int rc;

	while (n < HIST_SHOWN && (t = view_jump_title(&cur->view, dir * (n + 1))) != NULL)
	{
		snprintf(lines[n], sizeof(lines[n]), "  %.40s ", t);
		lab[n] = lines[n];
		n++;
	}
	if (n == 0)
		return;
	log_event("menu", dir < 0 ? "back-history" : "forward-history", NULL);
	r = popup(x, y, lab, n);
	if (r < 0)
		return;
	sprintf(num, "%d", dir * (r + 1));
	log_event("history", num, NULL);
	graf_mouse(BUSYBEE, NULL);
	rc = view_jump(&cur->view, dir * (r + 1));
	graf_mouse(ARROW, NULL);
	after(rc);
}

/* See also: the page's @xref entries */
static void seealso_menu(short x, short y)
{
	static char lines[16][48];
	const char *lab[16];
	char label[44];
	short n, r;
	int rc;

	for (n = 0; n < 16 && (unsigned int) n < view_xref_count(&cur->view); n++)
	{
		view_xref_label(&cur->view, (unsigned int) n, label, sizeof(label));
		snprintf(lines[n], sizeof(lines[n]), "  %s ", label);
		lab[n] = lines[n];
	}
	if (n == 0)
		return;
	log_event("menu", "see-also", NULL);
	r = popup(x, y, lab, n);
	if (r < 0)
		return;
	view_xref_label(&cur->view, (unsigned int) r, label, sizeof(label));
	log_event("see-also", label, NULL);
	graf_mouse(BUSYBEE, NULL);
	rc = view_xref_follow(&cur->view, (unsigned int) r);
	graf_mouse(ARROW, NULL);
	after(rc);
}

/* Navigate > See also...: under the toolbar's button, or on the page */
static void seealso_here(void)
{
	toolbar t;
	short x, y, w, h;

	if (cur->h <= 0 || cur->icon || !view_can(&cur->view, VIEW_SEEALSO))
		return;
	viewer_top();
	bar(&t);
	if (t.bw[TB_SEEALSO] > 0)
	{
		seealso_menu(t.bx[TB_SEEALSO], (short) (t.by + t.bh));
		return;
	}
	work_area(&x, &y, &w, &h);
	seealso_menu((short) (x + 2 * cw), (short) (y + ch));
}

/* the Bookmarks button: add, remove, then the bookmarks */
static void bookmarks_menu(short x, short y)
{
	static char lines[3 + BM_MAX][48];
	const char *lab[3 + BM_MAX];
	int cmds[3 + BM_MAX];
	short n = 0, r;
	unsigned int i;

	menu_line(lines[n] + 1, sizeof(lines[n]) - 1, "Add bookmark", "^D");
	lines[n][0] = '~';
	lab[n] = cur->view.page != NULL && page_bookmark() < 0 && bookmarks.n < BM_MAX ? lines[n] + 1 : lines[n];
	cmds[n++] = CMD_BM_ADD;
	menu_line(lines[n] + 1, sizeof(lines[n]) - 1, "Remove bookmark", NULL);
	lines[n][0] = '~';
	lab[n] = page_bookmark() >= 0 ? lines[n] + 1 : lines[n];
	cmds[n++] = CMD_BM_REMOVE;
	if (bookmarks.n > 0)
	{
		lab[n] = "-";
		cmds[n++] = CMD_NONE;
	}
	for (i = 0; i < bookmarks.n && i < BM_MAX; i++)
	{
		snprintf(lines[n], sizeof(lines[n]), "  %.40s ", bookmarks.b[i].title);
		lab[n] = lines[n];
		cmds[n++] = CMD_BM0 + (int) i;
	}
	log_event("menu", "bookmarks", NULL);
	r = popup(x, y, lab, n);
	if (r < 0 || cmds[r] == CMD_NONE)
		return;
	log_event("menu", menu_command_name(cmds[r]), NULL);
	command(cmds[r]);
}

/*
 * The menu button: what the menu bar has besides navigating (the
 * buttons do that), About first.  The accessory has no menu bar; this is
 * where it finds these (the guidelines, 1 and 3.1).
 */
static void bar_menu(short x, short y)
{
	static const struct
	{
		const char *text, *key;
		int cmd;
	} all[] = {
		{ "About EHypView...", NULL, CMD_ABOUT },
		{ "-", NULL, CMD_NONE },
		{ "New window", "^N", CMD_NEWWIN },
		{ "Open hypertext...", "^O", CMD_OPEN },
		{ "Library and search", "^L", CMD_LIBRARY },
		{ "-", NULL, CMD_NONE },
		{ "Copy page", "^C", CMD_COPY },
		{ "Add bookmark", "^D", CMD_BM_ADD },
		{ "-", NULL, CMD_NONE },
		{ "Font...", "\x01^F", CMD_FONT },
		{ "Larger text", "^+", CMD_LARGER },
		{ "Smaller text", "^-", CMD_SMALLER },
		{ "Settings...", "^,", CMD_SETTINGS },
		{ "-", NULL, CMD_NONE },
		{ "EHypView guide", NULL, CMD_GUIDE },
		{ "-", NULL, CMD_NONE },
		{ "Close window", "^W", CMD_CLOSE },
		{ "Quit", "^Q", CMD_QUIT }
	};
	enum { N = sizeof(all) / sizeof(all[0]) };
	static char lines[N][48];
	const char *lab[N];
	short i, r;

	for (i = 0; i < N; i++)
	{
		int c = all[i].cmd, grey;

		if (c == CMD_NONE)
		{
			lab[i] = "-";
			continue;
		}
		grey = (c == CMD_NEWWIN && viewer_windows() >= MAX_VIEWERS) ||
			(c == CMD_COPY && cur->view.page == NULL) ||
			(c == CMD_BM_ADD && (cur->view.page == NULL || page_bookmark() >= 0 || bookmarks.n >= BM_MAX));
		lines[i][0] = '~';
		menu_line(lines[i] + 1, sizeof(lines[i]) - 1, all[i].text, all[i].key);
		lab[i] = grey ? lines[i] : lines[i] + 1;
	}
	log_event("menu", "toolbar", NULL);
	r = popup(x, y, lab, N);
	if (r < 0 || all[r].cmd == CMD_NONE)
		return;
	log_event("menu", menu_command_name(all[r].cmd), NULL);
	command(all[r].cmd);
}

/* tooltips: a button's, after the mouse rests on it for a second */
static struct
{
	short win;                         /* the window it is in, or -1 */
	int b;                             /* the button under the mouse, -1 */
	unsigned long since;               /* when the mouse came there (ms) */
	short done;                        /* shown, or clicked: not again there */
	short shown, x, y, w, h;           /* what it covers */
} tip = { -1, -1, 0, 0, 0, 0, 0, 0, 0 };

static void tip_hide(void)
{
	viewer *keep = cur, *v;

	if (!tip.shown)
		return;
	tip.shown = 0;
	v = viewer_of(tip.win);
	if (v != NULL && !v->icon)
	{
		cur = v;
		redraw(tip.x, tip.y, tip.w, tip.h);
		cur = keep;
	}
}

static void tip_show(const toolbar *t, int b)
{
	const char *text = tb_tip(b);
	short n = (short) strlen(text), wx, wy, ww, wh, clip[4];

	wind_get(cur->h, WF_WORKXYWH, &wx, &wy, &ww, &wh);
	font_sys_push();
	tip.w = (short) ((n + 2) * cw);
	tip.h = (short) (ch + 4);
	tip.x = t->bx[b];
	tip.y = (short) (t->by + t->bh + 3);
	if (tip.x + tip.w > wx + ww)
		tip.x = (short) (wx + ww - tip.w);
	if (tip.x < wx)
		tip.x = wx;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, NULL);
	clip[0] = wx;
	clip[1] = wy;
	clip[2] = (short) (wx + ww - 1);
	clip[3] = (short) (wy + wh - 1);
	vs_clip(vdi_h, 1, clip);
	fill(1, tip.x, tip.y, (short) (tip.x + tip.w - 1), (short) (tip.y + tip.h - 1));
	fill(0, (short) (tip.x + 1), (short) (tip.y + 1), (short) (tip.x + tip.w - 2), (short) (tip.y + tip.h - 2));
	vswr_mode(vdi_h, MD_TRANS);
	vst_color(vdi_h, 1);
	vst_effects(vdi_h, 0);
	text_at((short) (tip.x + cw), (short) (tip.y + 2), text, n);
	vs_clip(vdi_h, 0, clip);
	graf_mouse(M_ON, NULL);
	wind_update(END_UPDATE);
	font_sys_pop();
	tip.shown = 1;
	tip.done = 1;
	log_event("tip", tb_name(b), NULL);
}

/* after each event and timer tick: where the mouse rests */
static void tip_check(void)
{
	short top = top_window(), mx, my, mb, ks;
	viewer *v = viewer_of(top), *keep = cur;
	toolbar t;
	int b = -1;

	if (v != NULL && !v->icon)
	{
		graf_mkstate(&mx, &my, &mb, &ks);
		cur = v;
		bar(&t);
		if (mb == 0 && wind_find(mx, my) == top)
			b = tb_hit(&t, mx, my);
		cur = keep;
	}
	if (b != tip.b || top != tip.win)
	{
		tip_hide();
		tip.b = b;
		tip.win = top;
		tip.since = tos_ms();
		tip.done = 0;
		return;
	}
	/* a second, on any machine: tos_ms() corrects a _hz_200 that runs
	   faster (312 Hz on a Falcon with its timer C set faster) */
	if (b >= 0 && !tip.done && tos_ms() - tip.since >= 1000)
	{
		cur = v;
		tip_show(&t, b);
		cur = keep;
	}
}

/* a button held down: shown pressed while the mouse is on it; 1 when
   let go there */
static int bar_press(const toolbar *t, int b)
{
	short x = t->bx[b], y = t->by, w = t->bw[b], h = t->bh, mx, my, mb, ks, in = 1, now, pxy[4];

	pxy[0] = (short) (x + 1);
	pxy[1] = (short) (y + 1);
	pxy[2] = (short) (x + w - 2);
	pxy[3] = (short) (y + h - 2);
	if (top_window() != cur->h)            /* covered: no drawing on it */
		return 1;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, NULL);
	vswr_mode(vdi_h, MD_XOR);
	vsf_interior(vdi_h, FIS_SOLID);
	vsf_color(vdi_h, 1);
	vr_recfl(vdi_h, pxy);
	graf_mouse(M_ON, NULL);
	for (;;)
	{
		graf_mkstate(&mx, &my, &mb, &ks);
		now = (short) (mx >= x && mx < x + w && my >= y && my < y + h);
		if (now != in)
		{
			graf_mouse(M_OFF, NULL);
			vswr_mode(vdi_h, MD_XOR);
			vr_recfl(vdi_h, pxy);
			graf_mouse(M_ON, NULL);
			in = now;
		}
		if (!(mb & 1))
			break;
		evnt_timer_(10);
	}
	if (in)
	{
		graf_mouse(M_OFF, NULL);
		vswr_mode(vdi_h, MD_XOR);
		vr_recfl(vdi_h, pxy);
		graf_mouse(M_ON, NULL);
	}
	vswr_mode(vdi_h, MD_REPLACE);
	wind_update(END_UPDATE);
	return in;
}

/* a click on the toolbar: 0 when it was elsewhere */
static int bar_click(short mx, short my, short button)
{
	toolbar t;
	short x, y, w, h, px, py;
	int b;

	wind_get(cur->h, WF_WORKXYWH, &x, &y, &w, &h);
	if (my < y || my >= y + tb_height(sys_ch) || mx < x || mx >= x + w)
		return 0;
	tip_hide();
	tip.done = 1;
	bar(&t);
	b = tb_hit(&t, mx, my);
	if (b < 0)
	{
		if (button & 2)
			context_menu(mx, my);
		return 1;
	}
	px = t.bx[b];                          /* its menus open below it */
	py = (short) (t.by + t.bh);
	if (button & 2)
	{
		/* the right button: the history on Back and Forward, the
		   bookmarks on Bookmarks, else the page's menu */
		if ((b == TB_BACK || b == TB_FORWARD) && bar_enabled(b))
			history_menu(b == TB_BACK ? -1 : 1, px, py);
		else if (b == TB_BOOKMARKS)
			bookmarks_menu(px, py);
		else
			context_menu(mx, my);
		return 1;
	}
	if (!bar_enabled(b) || !bar_press(&t, b))
		return 1;
	log_event("toolbar", tb_name(b), NULL);
	switch (b)
	{
	case TB_BACK: command(CMD_BACK); break;
	case TB_FORWARD: command(CMD_FORWARD); break;
	case TB_HOME: command(CMD_HOME); break;
	case TB_PREV: command(CMD_PREV); break;
	case TB_TOC: command(CMD_TOC); break;
	case TB_NEXT: command(CMD_NEXT); break;
	case TB_INDEX: command(CMD_INDEX); break;
	case TB_SEEALSO: seealso_menu(px, py); break;
	case TB_LIBRARY: command(CMD_LIBRARY); break;
	case TB_BOOKMARKS: bookmarks_menu(px, py); break;
	case TB_MENU: bar_menu(px, py); break;
	}
	return 1;
}

static void click(short mx, short my, short button, short kstate)
{
	short x, y, w, h;
	long py, line, col, r;

	if (bar_click(mx, my, button))
		return;
	if (button & 2)
	{
		/* on a link: it is the one the menu's "in a new window" takes */
		long l = link_under(mx, my);

		if (l >= 0 && l != cur->view.sel)
		{
			cur->view.sel = l;
			redraw_all();
		}
		context_menu(mx, my);
		return;
	}
	if (cur->view.page == NULL || cur->lay.line_y == NULL)
		return;
	work_area(&x, &y, &w, &h);
	if (mx < x || my < y || mx >= x + w || my >= y + h)
		return;
	py = my - y + cur->lay.gfx_y[cur->view.top];
	line = layout_line_at(&cur->lay, py, cur->view.top);
	if (line < 0)
		return;
	if (mx - x - MARGIN < 0)
		return;
	col = (mx - x - MARGIN) / cw + cur->view.left;
	r = view_link_at(&cur->view, line, col);
	if (r < 0)
		return;
	cur->view.sel = r;
	redraw_all();
	if (kstate & K_SHIFT)
	{
		new_window(1);                 /* Shift+click: the link in a new window */
		return;
	}
	graf_mouse(BUSYBEE, NULL);
	after(view_follow(&cur->view, &cur->view.page->runs[r]));
	graf_mouse(ARROW, NULL);
}

/* EHypView's own guide at page (NULL: its front page), for Help >
   EHypView guide and the library's Help key: EHYPVIEW.HYP in EHypView's
   folder, else wherever the library has it; else where to put it */
void app_guide(const char *page)
{
	char *p = fs_join(home_dir, "EHYPVIEW.HYP"), req[240];
	const char *file = NULL;
	pf_info st;
	unsigned long hit;

	if (p != NULL && strchr(p, ' ') == NULL && pf_stat(p, &st) == 0 && !st.is_dir)
		file = p;
	else
	{
		job_wait_index();
		if (idx != NULL && ehv_index_find_guides(idx, "EHYPVIEW.HYP", &hit, 1) > 0)
			file = "*:\\EHYPVIEW.HYP";
	}
	if (file != NULL)
	{
		if (page != NULL)
			snprintf(req, sizeof(req), "%s '%s'", file, page);
		else
			snprintf(req, sizeof(req), "%s", file);
		free(p);
		show_request(req);
		return;
	}
	free(p);
	log_event("alert", "no-guide", NULL);
	alert(1, "[1][EHypView's guide, EHYPVIEW.HYP, isn't in the library.|Copy it into \\GUIDES, or next "
	      "to EHYPVIEW.PRG.][ OK ]");
}

/* a hypertext and page asked for (VA_START, AC_HELP): the window, then
   the page */
static void show_request(const char *args)
{
	wake();
	if (needs_index(args))
		job_wait_index();
	if ((args == NULL || args[0] == '\0') && cur->view.page == NULL)
	{
		libwin_open();             /* nothing asked, nothing shown: the library */
		return;
	}
	open_window();
	if (args != NULL && args[0] != '\0')
	{
		graf_mouse(BUSYBEE, NULL);
		after(view_request(&cur->view, args));
		graf_mouse(ARROW, NULL);
	}
}

static void viewer_message(short *msg)
{
	short w, h;

	switch (msg[0])
	{
	case MN_SELECTED:
		if (has_menu)
		{
			int cmd = menu_command(&menu, msg[4]);

			menu_tnormal(menu.tree, msg[3], 1);
			pick_current();
			log_event("menu", menu_command_name(cmd), NULL);
			command(cmd);
		}
		break;
	case WM_REDRAW:
		if (msg[3] == cur->h)
			redraw(msg[4], msg[5], msg[6], msg[7]);
		break;
	case WM_TOPPED:
		if (msg[3] == cur->h)
			wind_set(cur->h, WF_TOP, cur->h, 0, 0, 0);
		break;
	case WM_BOTTOMED:
		wind_set(msg[3], WF_BOTTOM, 0, 0, 0, 0);
		break;
	case WM_CLOSED:
		if (msg[3] == cur->h)
			close_window();
		break;
	case WM_ICONIFY:
	case WM_ALLICONIFY:
		if (msg[3] == cur->h && !cur->icon)
		{
			wind_get(cur->h, WF_CURRXYWH, &cur->place[0], &cur->place[1], &cur->place[2], &cur->place[3]);
			cur->icon = 1;
			wind_set(cur->h, WF_ICONIFY, msg[4], msg[5], msg[6], msg[7]);
			log_event("iconify", "viewer", NULL);
		}
		break;
	case WM_UNICONIFY:
		if (msg[3] == cur->h && cur->icon)
		{
			cur->place[0] = msg[4];
			cur->place[1] = msg[5];
			cur->place[2] = msg[6];
			cur->place[3] = msg[7];
			restore();
			redraw_all();
		}
		break;
	case WM_FULLED:
		if (msg[3] == cur->h && !cur->icon)
		{
			short cx, cy, cw_, ch_, fx, fy, fw, fh;

			wind_get(cur->h, WF_CURRXYWH, &cx, &cy, &cw_, &ch_);
			wind_get(cur->h, WF_FULLXYWH, &fx, &fy, &fw, &fh);
			if (cx == fx && cy == fy && cw_ == fw && ch_ == fh)
				wind_get(cur->h, WF_PREVXYWH, &fx, &fy, &fw, &fh);
			wind_set(cur->h, WF_CURRXYWH, fx, fy, fw, fh);
			redraw_all();
		}
		break;
	case WM_SIZED:
	case WM_MOVED:
		if (msg[3] == cur->h && cur->icon)    /* the icon goes where it is put */
			wind_set(cur->h, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
		else if (msg[3] == cur->h)
		{
			/* at least the toolbar's Back, Forward and menu, and a few lines */
			w = msg[6] < 16 * sys_cw ? (short) (16 * sys_cw) : msg[6];
			w = w < 12 * cw ? (short) (12 * cw) : w;
			h = msg[7] < 6 * ch + tb_height(sys_ch) ? (short) (6 * ch + tb_height(sys_ch)) : msg[7];
			wind_set(cur->h, WF_CURRXYWH, msg[4], msg[5], w, h);
			clamp_scroll();
			set_sliders();
			if (msg[0] == WM_SIZED)
				redraw_all();
		}
		break;
	case WM_ARROWED:
		if (msg[3] == cur->h)
		{
			long rows = visible_lines(), cols = visible_cols();

			switch (msg[4])
			{
			case WA_UPPAGE: scroll_to(cur->view.top - rows, cur->view.left); break;
			case WA_DNPAGE: scroll_to(cur->view.top + rows, cur->view.left); break;
			case WA_UPLINE: scroll_to(cur->view.top - 1, cur->view.left); break;
			case WA_DNLINE: scroll_to(cur->view.top + 1, cur->view.left); break;
			case WA_LFPAGE: scroll_to(cur->view.top, cur->view.left - cols); break;
			case WA_RTPAGE: scroll_to(cur->view.top, cur->view.left + cols); break;
			case WA_LFLINE: scroll_to(cur->view.top, cur->view.left - 1); break;
			case WA_RTLINE: scroll_to(cur->view.top, cur->view.left + 1); break;
			}
		}
		break;
	case WM_VSLID:
		if (msg[3] == cur->h)
			scroll_to(max_top() * msg[4] / 1000, cur->view.left);
		break;
	case WM_HSLID:
		if (msg[3] == cur->h)
			scroll_to(cur->view.top, max_left() * msg[4] / 1000);
		break;
	case AC_OPEN:
		if (msg[4] == menu_id && twin_pass(""))
		{
			log_event("acc", "to the program", NULL);
			break;                     /* EHYPVIEW.PRG runs: its window to the front */
		}
		if (msg[4] == menu_id && screen_check("EHypView", ehv_ui_scr_w))	/* not in ST low */
		{
			aes_profile_tag("opening");
			log_event("acc", "opened", NULL);
			av_init("EHYPVIEW|ST-GUIDE");	/* a desktop started since */
			wake();
			font_check();
			bookmarks_load();
			pick_current();
			if (cur->view.page != NULL)
			open_window();
			else
				libwin_open();
		}
		break;
	case AC_CLOSE:
		twin_reset();                  /* another program in front */
		if (msg[3] == menu_id)
		{
			/* the AES took the windows: the viewer in front is kept, with
			   its page, for the next opening; the others go */
			int i;

			log_event("acc", "closed by the AES", NULL);
			pick_current();
			for (i = 0; i < MAX_VIEWERS; i++)
			{
				viewer *x = &viewers[i];

				if (!x->used)
					continue;
				if (x->h > 0)
					av_window(x->h, 0);
				x->h = -1;
				x->icon = 0;
				if (x != cur)
					viewer_drop(x);
			}
			libwin_forget();
		}
		break;
	case AP_TERM:
		if (!is_acc)                   /* an accessory never ends */
			quit_now = 1;
		break;
	case FONT_CHANGED:
		/* a font selector dropped a font on one of our windows */
		if (viewer_of(msg[3]) != NULL || msg[3] == libwin_handle() || msg[3] < 0)
		{
			short reply[8];

			memset(reply, 0, sizeof(reply));
			reply[0] = FONT_ACK;
			reply[1] = ap_id;
			reply[3] = (short) font_protocol(msg[4], msg[5]);
			appl_write(msg[1], 16, reply);
		}
		break;
	case VA_START:
		{
			/* answered (AV_STARTED) once our own copy is taken:
			   common/ui/av.c */
			const char *args;

			if (av_message(msg, &args) == AV_START && !(is_acc && twin_pass(args)))
				show_request(args);
		}
		break;
	case AC_HELP:
		{
			const char *args = (const char *) (((unsigned long) (unsigned short) msg[3] << 16) |
				(unsigned short) msg[4]);

			if (!(is_acc && twin_pass(args)))
				show_request(args);
		}
		break;
	default:
		{
			/* a hypertext dropped (drag and drop, or the desktop on the
			   accessory's window): shown, the first if several */
			DROP d;
			char path[256];

			switch (dd_message(msg, DD_WANT_FILES, &d))
			{
			case DD_FILES:
				if (av_arg(d.data, path, sizeof(path)))
				{
					wake();
					viewer_show(path, NULL, 0);
				}
				break;
			case DD_NONE:
				{
					/* our accessory asking whether we run (twin.h) */
					const char *a;

					av_message(msg, &a);
				}
				break;
			}
			dd_done(&d);
		}
		break;
	}
}

/*
 * A message.  A window's goes to that viewer, which stays the current
 * one only when it comes to the front (or the old one went).
 */
static void message(short *msg)
{
	viewer *keep = cur, *v = NULL;

	if (msg[0] >= WM_REDRAW && msg[0] <= WM_ALLICONIFY && (v = viewer_of(msg[3])) != NULL)
		cur = v;
	viewer_message(msg);
	if (v != NULL && keep->used && msg[0] != WM_TOPPED && msg[0] != WM_CLOSED)
		cur = keep;
}

/* ---------------------------------------------------------------- */
/* index: load the cache, then rescan in the background              */

/* ---------------------------------------------------------------- */
/* EHYPVIEW.INF: in the boot drive's root, where the accessory finds  */
/* it too; FOLDER= says where the rest is (the AERA guidelines, 6.1)  */

static const char inf_head[] =
	"; EHYPVIEW.INF: EHypView's settings, the program's and the accessory's.\r\n"
	"; A line starting with ';' is a note. EHypView keeps the notes, and\r\n"
	"; any setting it doesn't know, when it writes the file.\r\n\r\n";

/*
 * EHYPVIEW.INF as read: once at the start (the AERA guidelines, 6.5),
 * again when the accessory opens (the program may have changed it), and
 * as written.  Its text, for the font's settings, and its date.
 */
static char *own_text;
static pf_info own_info;
static inf_file *own_inf;

static void own_set(char *text)
{
	free(own_text);
	inf_free(own_inf);
	own_text = text;
	own_inf = text != NULL ? inf_parse(text, NULL) : NULL;
}

static void own_reload(void)
{
	memset(&own_info, 0, sizeof(own_info));
	own_set(pf_load_info(settings_path, NULL, &own_info));
}

int settings_put(const char *key, const char *value, const char *note)
{
	char *text = pf_load(settings_path, NULL), *t;
	int r = -1;

	t = inf_put(text != NULL ? text : inf_head, key, value, note);
	if (t != NULL)
	{
		r = pf_save(settings_path, t, (unsigned long) strlen(t));
		if (r == 0)
			own_set(t);                /* as it is on the disk now */
		else
			free(t);
	}
	free(text);
	return r;
}

/* the bookmarks in EHYPVIEW.INF: read again when the accessory opens,
   as the program may have changed them */
static void bookmarks_load(void)
{
	inf_file *inf;
	unsigned int i;

	if (is_acc)
		own_reload();
	inf = own_inf;

	bm_read(&bookmarks, inf);
	bm_written = 0;
	for (i = 0; inf != NULL && i < BM_MAX; i++)
	{
		char key[16];

		bm_key(i, key, sizeof(key));
		if (inf_get(inf, key) != NULL)
			bm_written = i + 1;
	}
	/* the pages read last: the file's, unless some read here aren't in
	   it yet */
	if (!recent_changed)
	{
		bm_read_as(&recent, inf, "RECENT", RECENT_MAX);
		recent_written = 0;
		for (i = 0; inf != NULL && i < RECENT_MAX; i++)
		{
			char key[16];

			bm_key_as("RECENT", i, key, sizeof(key));
			if (inf_get(inf, key) != NULL)
				recent_written = i + 1;
		}
	}
	libwin_layout(library_layout());
	libwin_recent(&recent);
	libwin_bookmarks(&bookmarks);
	menu_rebuild();
}

/* LIBRARY=: the library's layout (Settings) */
static int library_layout(void)
{
	const char *v = own_inf != NULL ? inf_get(own_inf, "LIBRARY") : NULL;

	return v != NULL && atari_casecmp(v, "panes") == 0 ? LIB_PANES : LIB_OUTLINE;
}

/* BOOKMARK1 to BOOKMARKn (or RECENTn) written back, in one go: the ones
   no longer used left empty */
static int list_save(const ehv_bookmarks *list, const char *prefix, unsigned int *written,
		     const char *note1, const char *note)
{
	char *text = pf_load(settings_path, NULL);	/* the program may have written since */
	unsigned int i, last = list->n > *written ? list->n : *written;
	int r = 0;

	for (i = 0; i < last; i++)
	{
		char key[16], val[400], *t;

		bm_key_as(prefix, i, key, sizeof(key));
		bm_value(list, i, val, sizeof(val));
		t = inf_put(text != NULL ? text : inf_head, key, val, i == 0 ? note1 : note);
		if (t == NULL)
		{
			r = -1;
			break;
		}
		free(text);
		text = t;
	}
	if (r == 0 && text != NULL)
		r = pf_save(settings_path, text, (unsigned long) strlen(text));
	if (r == 0 && text != NULL)
		own_set(text);
	else
		free(text);
	if (r == 0)
		*written = last;
	return r;
}

static int bookmarks_save(void)
{
	return list_save(&bookmarks, "BOOKMARK", &bm_written,
			 "a bookmark (^D, the Bookmarks menu, the library): the hypertext, the page and the title, "
			 "between '|'; empty when removed", "another bookmark, as BOOKMARK1");
}

/* the pages read last, when they changed: as a window closes and at
   the end (not at each page: the disk is left alone, guidelines 6.5) */
static void recent_save(void)
{
	if (!recent_changed)
		return;
	if (list_save(&recent, "RECENT", &recent_written,
		      "a page read lately, newest first (the library's Continue and Recently read): "
		      "the hypertext, the page and the title, between '|'", "another page read lately, as RECENT1") == 0)
		recent_changed = 0;
	else
		log_event("status", "Couldn't write EHYPVIEW.INF", NULL);
}

/* after one was added or removed: kept, and shown everywhere */
static void bookmarks_changed(void)
{
	if (bookmarks_save() != 0)
		log_event("status", "Couldn't write EHYPVIEW.INF", NULL);
	libwin_bookmarks(&bookmarks);
	menu_rebuild();
}

/* The program keeps its files next to itself and says so in FOLDER=;
   the accessory takes FOLDER=, else the boot drive's root (its files
   are only the library's index and the fonts measured, so it never
   asks) */
static void find_home(void)
{
	inf_file *inf;
	const char *f;
	pf_info st;

	settings_path[0] = (char) ('A' + boot_drive());
	strcpy(settings_path + 1, ":\\EHYPVIEW.INF");
	if (!is_acc)
	{
		char *old = fs_join(start_dir, "EHYPVIEW.INF");

		strcpy(home_dir, start_dir);
		/* EHYPVIEW.INF from before, next to the program: to the root,
		   the first time */
		if (old != NULL && pf_stat(settings_path, &st) != 0 && atari_casecmp(old, settings_path) != 0)
		{
			unsigned long n;
			char *t = pf_load(old, &n);

			if (t != NULL)
				pf_save(settings_path, t, n);
			free(t);
		}
		free(old);
		own_reload();
		f = own_inf != NULL ? inf_get(own_inf, "FOLDER") : NULL;
		if (f == NULL || atari_casecmp(f, home_dir) != 0)
			settings_put("FOLDER", home_dir, "where EHypView keeps its files (EHYPVIEW.IDX, the library; "
				     "EHYPVIEW.FNT, the fonts measured): the program writes the folder it was started from");
		if (own_inf == NULL || inf_get(own_inf, "START") == NULL)
			settings_put("START", "opened", "when the accessory looks through the library: opened (when "
				     "first opened or asked for a page), now (at boot), or seconds after boot");
		return;
	}
	own_reload();
	inf = own_inf;
	f = inf != NULL ? inf_get(inf, "START") : NULL;
	if (f != NULL)
		start_when = bg_parse(f);
	f = inf != NULL ? inf_get(inf, "FOLDER") : NULL;
	if (f != NULL && f[0] != '\0' && strlen(f) < sizeof(home_dir) &&
	    (strlen(f) <= 3 || (pf_stat(f, &st) == 0 && st.is_dir)))
		strcpy(home_dir, f);
	else
		strcpy(home_dir, settings_path), home_dir[3] = '\0';	/* X:\ */
}

/* ST-GUIDE.INF, read once a start (guidelines 6.5); not to be freed */
static const inf_file *read_inf(void)
{
	static inf_file *stg;
	static int read;
	char path[32];

	if (!read)
	{
		read = 1;
		path[0] = (char) ('A' + boot_drive());
		strcpy(path + 1, ":\\ST-GUIDE.INF");
		stg = inf_load(path, NULL);
	}
	return stg;
}

/* the job starts: the folders to look in, EHypView's own (PATHS= in
   EHYPVIEW.INF) and ST-GUIDE.INF's */
static void job_begin(void)
{
	const inf_file *inf;
	char **roots;
	unsigned int n;
	char *p;

	if (job != NULL || job_over)
		return;
	inf = read_inf();
	n = ehv_default_roots(inf, own_inf != NULL ? inf_get(own_inf, "PATHS") : NULL, &roots);
	p = fs_join(home_dir, "EHYPVIEW.IDX");
	if (p != NULL)
	{
		strncpy(cache_path, p, sizeof(cache_path) - 1);
		free(p);
	}
	job = ehv_libjob_begin(cache_path, (const char *const *) roots, n, inf ? inf_get(inf, "REF") : NULL);
	inf_free_list(roots, n);
	if (job == NULL)
		job_over = 1;
	aes_profile_tag("index");
}

static void job_end(const char *how)
{
	ehv_libjob_free(job);
	job = NULL;
	job_over = 1;
	log_event("index", how, NULL);
	index_news = 1;
}

/* the folders to look in changed (Settings): the job again, from the
   index kept, with the new ones */
static void job_again(void)
{
	ehv_libjob_free(job);
	job = NULL;
	job_over = 0;
	log_event("index", "again", NULL);
}

/* one short step of the job */
static void job_step(void)
{
	int r;

	if (job == NULL)
		return;
#ifdef AES_PROFILE
	{
		static const char *const tag[] = { "idx-load", "idx-scan", "idx-sort", "idx-save", "idx-done" };

		aes_profile_tag(tag[ehv_libjob_phase(job)]);
	}
#endif
	r = ehv_libjob_step(job);
	if (r == EHV_JOB_INDEX)
	{
		char num[16];

		/* the one before (if any) isn't read by the job any more */
		ehv_index_free(idx);
		idx = ehv_libjob_take(job);
		set_index_everywhere();
		sprintf(num, "%lu", idx->nguides);
		log_event("index", num, "guides");
		index_news = 1;
	} else if (r == EHV_JOB_DONE)
	{
		job_end(ehv_libjob_unchanged(job) ? "unchanged" : ehv_libjob_saved(job) ? "saved" : "not saved");
	} else if (r == EHV_JOB_FAILED)
	{
		job_end("failed");
	} else
	{
		/* progress, now and then */
		static unsigned long last;
		unsigned long n = scan_found();

		if (n != last && n % 8 == 0)
		{
			last = n;
			index_news = 1;
		}
	}
}

/* there is work for the job now or later (bgwork.h decides when) */
static int job_pending(void)
{
	return job != NULL || (!job_over && (woken || start_when != BG_OPENED));
}

/* a request that can't be answered without the index (a page name,
   *:\X.HYP): the index read now, with the busy bee, since the user
   asked; the rest goes on in the background */
static void job_wait_index(void)
{
	if (idx != NULL)
		return;
	job_begin();
	if (job == NULL)
		return;
	graf_mouse(BUSYBEE, NULL);
	while (job != NULL && idx == NULL)
		job_step();
	aes_profile_tag("asked");          /* not background work: the user waits */
	graf_mouse(ARROW, NULL);
}

/* whether a request needs the index: not when it names a file that is
   there (as the AERA programs' Help keys do) */
static int needs_index(const char *args)
{
	char **argv;
	unsigned int argc;
	int r = 0;

	if (args == NULL || args[0] == '\0')
		return 0;
	argc = ehv_split_args(args, &argv);
	if (argc > 0)
	{
		const char *f = argv[0];
		pf_info st;

		r = !(fs_has_ext(f, ".hyp") && !(f[0] == '*' && f[1] == ':') && fs_has_dir(f) &&
		      pf_look(f, &st) == 0 && !st.is_dir);
	}
	inf_free_list(argv, argc);
	return r;
}

/*
 * Opened, or asked for a page, the first time: the index may be read
 * (START=opened).  The font was set at start-up (font_init).
 */
static void wake(void)
{
	if (woken)
		return;
	woken = 1;
	bg_opened();
	log_font();
}

int scanning(void)
{
	return job != NULL && (idx == NULL || ehv_libjob_phase(job) == EHV_JOB_SCANNING);
}

unsigned long scan_found(void)
{
	const ehv_scan_status *st = job != NULL ? ehv_libjob_scan_status(job) : NULL;

	return st != NULL ? st->opened + st->reused : 0;
}

const char *index_busy(void)
{
	static char s[48];

	if (job == NULL)
		return "";
	if (ehv_libjob_phase(job) == EHV_JOB_LOADING)
		return "Reading the library...";
	if (ehv_libjob_phase(job) != EHV_JOB_SCANNING)
		return "Sorting the library...";
	snprintf(s, sizeof(s), "Looking for hypertexts... %lu found", scan_found());
	return s;
}

/* ---------------------------------------------------------------- */

/* the command line of an application: length byte, then the text */
static const char *command_line(void)
{
	static char arg[130];
	const unsigned char *cmd = (const unsigned char *) _BasePage + 0x80;
	int n = cmd[0] < 127 ? cmd[0] : 126;

	memcpy(arg, cmd + 1, (size_t) n);
	arg[n] = '\0';
	while (n > 0 && (arg[n - 1] == ' ' || arg[n - 1] == '\r' || arg[n - 1] == '\n'))
		arg[--n] = '\0';
	return arg;
}

/* the program's buffer for writing the index in one go: TT RAM first
   (the AERA guidelines, 6.5, common/atari/bigbuf.c) */
static void *index_buffer(unsigned long want, unsigned long *got)
{
	long n = 0;
	int tt;
	void *p = big_alloc((long) want, &n, &tt);

	*got = (unsigned long) n;
	return p;
}

/* how the index file is read and written (index.h): a 68030 reads 32 KB
   a step and writes the file in one go where memory allows, a 68000 16 KB
   both (the AERA guidelines, 6.5) */
static void index_pieces(void)
{
	int big = get_cookie(0x5F435055L) >= 20;      /* '_CPU' */

	ehv_index_pieces(big ? 32768L : 16384L, big ? (is_acc ? 512L : 1024L) * 1024 : 16384L);
	if (big && !is_acc)
		ehv_index_buffers(index_buffer, big_free);
}

/* an accessory takes its memory once, at start-up (see libc.c) */
static void acc_heap(void)
{
	long st = (long) Mxalloc(-1L, 0), avail, want;
	long tt = (long) Mxalloc(-1L, 1);      /* Fast RAM; -32: TOS 1.x, none */
	void *p;

	/* the About box's icon's ST RAM (st_ram() in gemui.c), now: taken
	   later, it would belong to the program in front (guidelines 2.2) */
	ehv_ui_st_ram(0);

	/* then the pictures' ST RAM (stmem.h): a sixteenth of what is free
	   there, 64 to 128 KB (a page's pictures; the rest is the screen's
	   and the programs'); TOS 1.x has no Mxalloc, and only ST RAM */
	if (st == -32)
		st = (long) Malloc(-1L);
	st /= 16;
	if (st > 128L * 1024)
		st = 128L * 1024;
	if (st < 64L * 1024)
		st = 64L * 1024;
	stmem_reserve(st);

	/* then the heap: a third of the largest block, or a quarter when
	   there is no Fast RAM (it all comes out of ST RAM then), at most
	   2 MB; on a small machine 96 KB (a page and its pictures being
	   converted), but never more than half: the programs started later
	   need the rest */
	avail = (long) Malloc(-1L);
	want = tt > 0 ? avail / 3 : avail / 4;

	if (want > 2L * 1024 * 1024)
		want = 2L * 1024 * 1024;
	if (want < 96L * 1024)
		want = avail / 2 < 96L * 1024 ? avail / 2 : 96L * 1024;
	/* TT RAM first, where there is some (guidelines 6.5); TOS 1.x has no
	   Mxalloc */
	p = want <= 0 ? NULL : tt == -32 ? Malloc(want) : Mxalloc(want, 3);
	if ((long) p > 0)
		heap_add(p, (size_t) want);
	{
		/* for the tests: what it took, where, and what it left the programs */
		char a[80], b[24];

		sprintf(a, "pictures %ldK heap %ldK at %lx", st / 1024, (long) p > 0 ? want / 1024 : 0L,
			(long) p > 0 ? (long) p : 0L);
		sprintf(b, "free %ldK", (long) Malloc(-1L) / 1024);
		log_event("acc-memory", a, b);
	}
}

/*
 * EHypView (the program) crashed and CRASH.LOG is written
 * (common/atari/crash.c): say so and leave GEM tidy.  Only the AES and
 * VDI: the heap may be what went wrong.
 */
static void crash_quit(const char *what)
{
	char a[200];

	log_event("crashed", what, NULL);
	graf_mouse(ARROW, NULL);
	snprintf(a, sizeof(a), "[3][EHypView has crashed|(%s).|What happened is in|CRASH.LOG, next to EHYPVIEW.PRG.][ Quit ]", what);
	form_alert(1, a);
	{
		int i;

		for (i = 0; i < MAX_VIEWERS; i++)
			if (viewers[i].used && viewers[i].h > 0)
			{
				wind_close(viewers[i].h);
				wind_delete(viewers[i].h);
			}
	}
	if (libwin_handle() > 0)
	{
		wind_close(libwin_handle());
		wind_delete(libwin_handle());
	}
	if (has_menu)
		menu_bar(menu.tree, 0);
	v_clsvwk(vdi_h);
	appl_exit();
}

int main(void)
{
	short work_out[57], msg[8], d, screen_w, screen_h;
	AEVENT ev;
	const inf_file *inf;
	int fts_busy = 0;
	unsigned long t0 = tos_hz200();       /* not tos_ms(): it measures the clock first */

	is_acc = _is_acc;
	ap_id = appl_init();
	if (ap_id < 0)
		return 1;
	twin_init("EHYPVIEW");
	if (!is_acc && twin_handover(command_line()))
	{
		appl_exit();                   /* EHYPVIEW.ACC runs, and has it */
		return 0;
	}
	if (is_acc)
	{
		acc_heap();
		menu_id = menu_register(ap_id, "  EHypView");
		log_event("acc", menu_id >= 0 ? "registered" : "no menu entry", NULL);
	}
	vdi_h = graf_handle(&sys_cw, &sys_ch, &d, &d);
	cw = sys_cw;
	ch = sys_ch;
	vdi_h = v_opnvwk_(vdi_h, work_out);
	screen_w = (short) (work_out[0] + 1);
	screen_h = (short) (work_out[1] + 1);
	ehv_ui_scr_w = screen_w;
	ehv_ui_scr_h = screen_h;
	ncolors = work_out[13];
	tall_pixels = work_out[4] >= work_out[3] * 3 / 2;	/* pixel height, width (microns) */
	vq_extnd(vdi_h, 1, work_out);
	planes = work_out[4];
	{
		/* for the tests (tests/run_colour.py) */
		char size[16], depth[24];

		sprintf(size, "%dx%d", screen_w, screen_h);
		sprintf(depth, "%d%s", planes, tall_pixels ? " tall" : "");
		log_event("screen", size, depth);
	}
	vst_alignment(vdi_h, 0, 5);
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	if (!is_acc && !screen_check("EHypView", screen_w))	/* not in ST low */
	{
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}

	start_dir[0] = (char) ('A' + Dgetdrv());
	start_dir[1] = ':';
	Dgetpath(start_dir + 2, 0);
	if (start_dir[2] == '\0')
		strcpy(start_dir + 2, "\\");
	crash_install(start_dir, "EHypView", app_version);	/* the program only */
	crash_cleanup = crash_quit;
	/* the desktop's AV server, if one runs yet; the accessory may be
	   ST-GUIDE.ACC, standing in for ST-Guide */
	av_init("EHYPVIEW|ST-GUIDE");

	/* a file looked up once, and a hypertext opened once, for each
	   event's work (the AERA guidelines, 6.5): forgotten before each
	   event call */
	pf_looks(1);
	hyp_recent(1);
	index_pieces();
	find_home();
	/* the accessory: nothing more before its first event call (the AERA
	   guidelines, 2.2): no index until it is opened, GDOS fonts now or
	   not at all */
	font_init(home_dir, settings_path, own_text, &own_info, is_acc);
	if (!is_acc)
		log_font();
	{
		int i;

		for (i = 0; i < MAX_VIEWERS; i++)
			viewers[i].h = -1;
	}
	viewers[0].used = 1;
	view_init(&cur->view, NULL);
	bg_init(is_acc ? start_when : BG_NOW);
	inf = NULL;
	if (!is_acc)
	{
		woken = 1;                     /* the user started it */
		bg_opened();
		inf = read_inf();
		bookmarks_load();
	}

	menu_scr_w = screen_w;
	menu_scr_h = screen_h;
	if (!is_acc && menu_build(&menu, &bookmarks, sys_cw, sys_ch, screen_w, screen_h) == 0)
	{
		memset(menu_on, -1, sizeof(menu_on));
		has_menu = menu_bar(menu.tree, 1) != 0;
		log_event("menu", has_menu ? "shown" : "refused", NULL);
		update_menu();
	}
	if (!is_acc)
	{
		const char *args = command_line();
		const char *def = inf ? inf_get(inf, "DEFHYP") : NULL;

		if (def == NULL && inf != NULL)
			def = inf_get(inf, "HYPTOP");
		if (needs_index(args[0] != '\0' ? args : def))
			job_wait_index();
		graf_mouse(BUSYBEE, NULL);
		if (args[0] != '\0')
			view_request(&cur->view, args);
		else if (def != NULL)
			view_open(&cur->view, def, NULL, 0);
		graf_mouse(ARROW, NULL);
		if (cur->view.page != NULL || cur->view.status[0] != '\0')
		{
			make_layout();
			open_window();
			if (cur->view.page != NULL)
				page_read();
			else
				log_event("status", cur->view.status, NULL);
		} else
		{
			libwin_open();
		}
		app_window_closed();
	}
	crash_offer("EHypView", "next to EHYPVIEW.PRG");

	{
		char num[16];

		/* the time to the first event call, for the tests */
		sprintf(num, "%lu", (tos_hz200() - t0) * 5);
		log_event("started", num, "ms");
	}
	aes_profile_tag("idle");
	while (!quit_now)
	{
		short flags = MU_MESAG, which;
		int pending = job_pending();
		/* opened and waiting for the library: read it on, whatever the
		   mouse does (the user waits for it) */
		int urgent = job != NULL && idx == NULL && woken;
		/* a viewer in front: the mouse is looked at for tooltips */
		int hover = viewer_of(top_window()) != NULL;
		unsigned long t;

		if (viewer_windows() > 0 || libwin_handle() > 0 || has_menu)
			flags |= MU_KEYBD | MU_BUTTON;
		/* 50 ms while work waits (bgwork.h), 200 for the tooltips, else
		   a second; the full-text search asked for goes on at once in
		   the program, every 50 ms in the accessory (no timer of 0) */
		t = bg_timer(pending, hover ? 200 : 1000);
		if (urgent || index_news || (fts_busy && is_acc))
			t = BG_PACE;
		else if (fts_busy)
			t = 0;
		if (pending || hover || fts_busy || index_news)
			flags |= MU_TIMER;
		pf_forget();
		hyp_forget();
		which = evnt_multi_(flags, is_acc ? 0x102 : 0x101, 3, 0, t, msg, &ev);
		/* what holds the machine until the next event call, for the
		   AESGAP lines of an -DAES_PROFILE build */
		aes_profile_tag(which & (MU_MESAG | MU_KEYBD | MU_BUTTON) ? "event" : "idle");
		bg_tick();
		if ((which & MU_MESAG) && !libwin_message(msg))
			message(msg);
		if ((which & MU_KEYBD) && crash_test(KEY_SCAN(ev.kreturn)))
			;                         /* test builds only: F1-F4 crash */
		else if (which & MU_KEYBD)
		{
			short top = top_window();
			viewer *tv = viewer_of(top);

			tip_hide();
			tip.done = 1;

			if (top > 0 && ((tv != NULL && tv->icon) ||
					(top == libwin_handle() && libwin_iconified())))
				;                     /* an icon takes no keys */
			else if (top == libwin_handle() && top > 0)
				libwin_key(ev.kstate, ev.kreturn);
			else
			{
				pick_current();
				if (cur->h > 0 && !cur->icon)
					key(ev.kstate, ev.kreturn);
				else
					app_key(ev.kstate, ev.kreturn);
			}
		}
		if (which & MU_BUTTON)
		{
			short h = wind_find(ev.mx, ev.my);
			viewer *cv = viewer_of(h);

			if (cv != NULL && !cv->icon)
			{
				cur = cv;
				click(ev.mx, ev.my, ev.mbutton, ev.kstate);
			}
			else if (h == libwin_handle() && h > 0)
				libwin_click(ev.mx, ev.my);
		}
		/* the index, in a slice (bgwork.h): not while the user works, nor
		   while EHYPVIEW.PRG runs (it has the index to itself) */
		if (index_news && which == MU_TIMER)
		{
			/* the windows show the new index (or progress): a turn of
			   its own, the job's slice in the next */
			aes_profile_tag("drawing");
			index_news = 0;
			info_everywhere();
			libwin_index_changed();
		} else if (which == MU_TIMER && job_pending() && (bg_may_work() || (urgent && twin_prg() < 0)))
		{
			/* a slice (bgwork.h), ending a little early: the step that
			   starts last may take a while on a 16 MHz machine, and one
			   that opens a file or reads a folder begins a slice */
			unsigned long t1 = tos_ms();

			job_begin();
			bg_begin();
			do
				job_step();
			while (job != NULL && bg_more() && tos_ms() - t1 < 35 && !ehv_libjob_next_is_big(job));
		}
		if ((which & MU_TIMER) || fts_busy == 0)
		{
			bg_begin();
			do
				fts_busy = libwin_idle();
			while (fts_busy && bg_more());
		}
		if (which & (MU_MESAG | MU_KEYBD | MU_BUTTON))
			update_menu();
		tip_check();
	}

	{
		int i;

		for (i = 0; i < MAX_VIEWERS; i++)
			if (viewers[i].used && viewers[i].h > 0)
			{
				wind_close(viewers[i].h);
				wind_delete(viewers[i].h);
			}
	}
	recent_save();
	log_event("quit", NULL, NULL);
	if (has_menu)
		menu_bar(menu.tree, 0);
	menu_free(&menu);
	font_exit();
	if (libwin_handle() > 0)
	{
		wind_close(libwin_handle());
		wind_delete(libwin_handle());
	}
	libwin_free();
	{
		int i;

		for (i = 0; i < MAX_VIEWERS; i++)
			if (viewers[i].used)
				viewer_drop(&viewers[i]);
	}
	bm_free(&bookmarks);
	bm_free(&recent);
	ehv_libjob_free(job);
	ehv_index_free(idx);
	crash_remove();
	av_exit();
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
