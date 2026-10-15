/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the GEM viewer.
 *
 * One window showing one page.  Text is laid out on the character grid
 * of the system font, as ST-Guide does: graphics (@line, @box, @image)
 * are positioned in character cells.  Navigation (links, history, help
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
#include "view.h"
#include "gemapp.h"
#include "pagedraw.h"
#include "menu.h"
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
short sys_cw, sys_ch;
short desk_x, desk_y, desk_w, desk_h;
ehv_index *idx;
static short menu_id = -1;
static short win_h = -1;
static short win_icon;                 /* shown as EHypView's icon (WM_ICONIFY) */
static short win_place[4];             /* where it was before */
static ehv_menu menu;
static short has_menu;
static signed char menu_on[CMD_COUNT];   /* what menu_ienable last set */
static char win_name[160], win_info[200];

static ehv_view view;
static ehv_scan *scanner;
static ref_db *allref;
static char cache_path[200];
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

static playout lay;

static void free_layout(void)
{
	layout_free(&lay);
}

static void make_layout(void)
{
	layout_make(&lay, view.doc, view.page);
}

/* ---------------------------------------------------------------- */
/* window geometry and scrolling                                     */

static void work_area(short *x, short *y, short *w, short *h)
{
	wind_get(win_h, WF_WORKXYWH, x, y, w, h);
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
	long c = view.page ? view.page->width : 0;

	if (view.doc != NULL && view.doc->width > c)
		c = view.doc->width;
	return c;
}

static long max_top(void)
{
	long n = view.page ? view.page->nlines : 0, rows = visible_lines();

	return n > rows ? n - rows : 0;
}

static long max_left(void)
{
	long c = page_cols(), cols = visible_cols();

	return c > cols ? c - cols : 0;
}

static void clamp_scroll(void)
{
	if (win_icon)                      /* the work area is the icon's */
		return;
	if (view.top > max_top())
		view.top = max_top();
	if (view.top < 0)
		view.top = 0;
	if (view.left > max_left())
		view.left = max_left();
	if (view.left < 0)
		view.left = 0;
}

static void set_sliders(void)
{
	long n = view.page ? view.page->nlines : 0, rows = visible_lines();
	long c = page_cols(), cols = visible_cols();
	long mt = max_top(), ml = max_left();

	if (win_icon)
		return;
	wind_set(win_h, WF_VSLSIZE, (short) (n > rows ? rows * 1000L / n : 1000), 0, 0, 0);
	wind_set(win_h, WF_VSLIDE, (short) (mt > 0 ? view.top * 1000L / mt : 0), 0, 0, 0);
	wind_set(win_h, WF_HSLSIZE, (short) (c > cols ? cols * 1000L / c : 1000), 0, 0, 0);
	wind_set(win_h, WF_HSLIDE, (short) (ml > 0 ? view.left * 1000L / ml : 0), 0, 0, 0);
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

/* draw the work area within one rectangle (screen coordinates) */
static void draw_rect(short cx, short cy, short cw_, short ch_)
{
	short wx, wy, ww, wh;
	short clip[4];
	const hyp_page *pg = view.page;
	long oy;
	short ox;

	work_area(&wx, &wy, &ww, &wh);
	clip[0] = cx;
	clip[1] = cy;
	clip[2] = (short) (cx + cw_ - 1);
	clip[3] = (short) (cy + ch_ - 1);
	vs_clip(vdi_h, 1, clip);
	fill(0, clip[0], clip[1], clip[2], clip[3]);

	if (pg == NULL || lay.line_y == NULL)
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

	ox = (short) (wx + MARGIN - view.left * cw);
	oy = wy - lay.gfx_y[view.top];
	layout_draw(&lay, ox, oy, view.top, cx, cy, cw_, ch_, view.sel, page_cols());
	vs_clip(vdi_h, 0, clip);
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

	if (win_h <= 0)
		return;
	if (win_icon)
	{
		app_icon_redraw(win_h, x, y, w, h);
		return;
	}
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, NULL);
	wind_get(win_h, WF_FIRSTXYWH, &rx, &ry, &rw, &rh);
	while (rw > 0 && rh > 0)
	{
		short ix = rx, iy = ry, iw = rw, ih = rh;

		if (intersect(&ix, &iy, &iw, &ih, x, y, w, h) &&
			intersect(&ix, &iy, &iw, &ih, desk_x, desk_y, desk_w, desk_h))
			draw_rect(ix, iy, iw, ih);
		wind_get(win_h, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
	}
	graf_mouse(M_ON, NULL);
	wind_update(END_UPDATE);
}

static void redraw_all(void)
{
	short x, y, w, h;

	if (win_icon)                      /* nothing of the page shows */
		return;
	work_area(&x, &y, &w, &h);
	clamp_scroll();
	set_sliders();
	redraw(x, y, w, h);
}

/* the iconified window as it was: a page asked for, the menu */
static void restore(void)
{
	if (win_h <= 0 || !win_icon)
		return;
	wind_set(win_h, WF_UNICONIFY, win_place[0], win_place[1], win_place[2], win_place[3]);
	win_icon = 0;
	log_event("uniconify", "viewer", NULL);
	clamp_scroll();
	set_sliders();
}

/* ---------------------------------------------------------------- */
/* window name, info line, page changes                              */

static void update_info(void)
{
	const ehv_scan_status *st = scanner ? ehv_scan_status_get(scanner) : NULL;

	if (win_h <= 0)
		return;
	if (view.status[0] != '\0')
		snprintf(win_info, sizeof(win_info), " %s", view.status);
	else if (st != NULL)
		snprintf(win_info, sizeof(win_info), " Looking for hypertexts... %lu found",
			st->opened + st->reused);
	else if (view.page != NULL)
		snprintf(win_info, sizeof(win_info), " %s", view.doc->path);
	else
		snprintf(win_info, sizeof(win_info), " %lu hypertexts known", idx ? idx->nguides : 0UL);
	wind_set_str(win_h, WF_INFO, win_info);
}

/* after the view changed page */
static void page_changed(void)
{
	restore();
	if (view.page != NULL)
		log_event("page", view.doc->path, view.doc->entries[view.page->num].name);
	make_layout();
	clamp_scroll();
	if (win_h > 0)
	{
		view_title(&view, win_name, sizeof(win_name));
		wind_set_str(win_h, WF_NAME, win_name);
		update_info();
		redraw_all();
	}
}

/* ---------------------------------------------------------------- */
/* pop-ups: a @pnode in a framed box over the page, as ST-Guide shows */

#define POP_PAD 4                  /* pixels between the frame and the text */

static void after(int rc);

/* the screen position of link run r of the page (its top left) */
static void link_pos(long r, short *x, short *y)
{
	short wx, wy, ww, wh;
	long line = view_run_line(&view, r), col = 0;
	unsigned short i;

	work_area(&wx, &wy, &ww, &wh);
	if (line < 0 || lay.line_y == NULL)
	{
		*x = (short) (wx + ww / 4);       /* no link to go by: the window's middle */
		*y = (short) (wy + wh / 4);
		return;
	}
	for (i = 0; i < view.page->lines[line].nruns; i++)
	{
		unsigned long k = view.page->lines[line].first_run + i;

		if ((long) k == r)
			break;
		col += view.page->runs[k].len;
	}
	*x = (short) (wx + MARGIN + (col - view.left) * cw);
	*y = (short) (wy + lay.line_y[line] - lay.gfx_y[view.top]);
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
 * Show view.popup next to link run `r` (below it, or above it when there
 * is no room) until a key or a click.  A click on one of its links
 * follows that link; anything else closes it, and the page stays.
 */
static void popup_box(long r)
{
	playout pl;
	short ax, ay, bx, by, bw, bh, clip[4], m[8], done = 0;
	long cols = view_popup_cols(&view), follow = -1;
	const char *name = view.doc->entries[view.popup_node].name;
	AEVENT e;

	memset(&pl, 0, sizeof(pl));
	layout_make(&pl, view.doc, view.popup);
	if (pl.pg == NULL)
	{
		view_popup_close(&view);
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
					follow = view_popup_link_at(&view, line, x / cw);
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
		after(view_popup_follow(&view, follow));
		graf_mouse(ARROW, NULL);
	} else
	{
		log_event("popup-closed", name, NULL);
		view_popup_close(&view);
	}
}

/* show a navigation result */
static void after(int rc)
{
	if (rc == VIEW_CLOSE)
	{
		if (is_acc)
		{
			av_window(win_h, 0);
			wind_close(win_h);
			wind_delete(win_h);
			win_h = -1;
			win_icon = 0;
		} else
		{
			quit_now = 1;
		}
		return;
	}
	if (rc == VIEW_OK)
		page_changed();
	else if (rc == VIEW_POPUP)
		popup_box(view.sel);
	else
	{
		log_event("status", view.status, NULL);
		update_info();
	}
}

static void open_window(void)
{
	short x, y, w, h, cols;

	if (win_h > 0)
	{
		restore();
		wind_set(win_h, WF_TOP, win_h, 0, 0, 0);
		return;
	}
	win_h = wind_create(icon_kind(WKIND), desk_x, desk_y, desk_w, desk_h);
	win_icon = 0;
	if (win_h <= 0)
	{
		win_h = -1;
		alert(1, "[3][EHypView: no more windows.][ OK ]");
		return;
	}
	cols = (short) (view.doc && view.doc->width ? view.doc->width : 75);
	wind_calc(WC_BORDER, WKIND, 0, 0, (short) (cols * cw + 2 * MARGIN), (short) (desk_h * 3 / 4),
		&x, &y, &w, &h);
	if (w > desk_w)
		w = desk_w;
	if (h > desk_h)
		h = desk_h;
	x = (short) (desk_x + (desk_w - w) / 2);
	y = (short) (desk_y + (desk_h - h) / 3);
	view_title(&view, win_name, sizeof(win_name));
	wind_set_str(win_h, WF_NAME, win_name);
	update_info();
	wind_open(win_h, x, y, w, h);
	if (is_acc)                        /* the desktop drops files on it */
		av_window(win_h, 1);
	clamp_scroll();
	set_sliders();
}

void app_window_closed(void)
{
	/* with a menu bar, the program stays until Quit */
	if (!is_acc && !has_menu && win_h <= 0 && libwin_handle() <= 0)
		quit_now = 1;
}

static void close_window(void)
{
	if (win_h > 0)
	{
		if (is_acc)
			av_window(win_h, 0);
		wind_close(win_h);
		wind_delete(win_h);
		log_event("closed", "viewer", NULL);
	}
	win_h = -1;
	win_icon = 0;
	app_window_closed();
}

short viewer_handle(void)
{
	return win_h;
}

void viewer_top(void)
{
	restore();
	if (win_h > 0)
		wind_set(win_h, WF_TOP, win_h, 0, 0, 0);
}

void viewer_show(const char *path, const char *node, unsigned short line)
{
	int rc = view_open(&view, path, node, line);

	if (win_h <= 0)
	{
		make_layout();
		open_window();
		if (rc != VIEW_OK)
			log_event("status", view.status, NULL);
		else if (view.page != NULL)
			log_event("page", view.doc->path, view.doc->entries[view.page->num].name);
		return;
	}
	viewer_top();
	after(rc);
}

/* ---------------------------------------------------------------- */
/* scrolling and navigation                                          */

static void scroll_to(long top, long left)
{
	long ot = view.top, ol = view.left;

	view.top = top;
	view.left = left;
	clamp_scroll();
	if (view.top != ot || view.left != ol)
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
	if (line < view.top)
		scroll_to(line, view.left);
	else if (line >= view.top + rows)
		scroll_to(line - rows + 1, view.left);
}

static void select_link(int dir)
{
	long rows = visible_lines();
	long line = view_select_link(&view, dir, view.top, view.top + rows - 1);

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
	case CMD_HOME: return VIEW_HOME;
	case CMD_INDEX: return VIEW_INDEX;
	case CMD_TOC: return VIEW_TOC;
	case CMD_HELP: return VIEW_HELP;
	case CMD_PREV: return VIEW_PREV;
	case CMD_NEXT: return VIEW_NEXT;
	}
	return -1;
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
	if (what != CLIP_COPY || win_h <= 0 || view.page == NULL || (t = malloc(32000)) == NULL)
		return;
	n = view_page_text(&view, t, 32000);
	if (clip_put(t, (long) n))
	{
		char lines[16];

		sprintf(lines, "%u", view.page->nlines);
		log_event("copy", "page", lines);
		snprintf(view.status, sizeof(view.status), "The page is on the clipboard.");
	} else
	{
		snprintf(view.status, sizeof(view.status), "Couldn't write the clipboard.");
	}
	free(t);
	update_info();
}

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
		if (libwin_handle() > 0 && (top_window() == libwin_handle() || win_h <= 0))
			libwin_close();
		else
			close_window();
		return;
	case CMD_FONT:
		font_dialog();
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
			close_window();
		} else
		{
			quit_now = 1;
		}
		return;
	}
	if (target < 0 || win_h <= 0)
		return;
	viewer_top();
	graf_mouse(BUSYBEE, NULL);
	rc = target == VIEW_BACK ? view_back(&view) : view_relative(&view, target);
	graf_mouse(ARROW, NULL);
	after(rc);
}

/* grey out what cannot be done now */
static void update_menu(void)
{
	int c;

	if (!has_menu)
		return;
	for (c = CMD_ABOUT; c < CMD_COUNT; c++)
	{
		int on = 1, target = nav_target(c);

		if (c == CMD_CLOSE)
			on = win_h > 0 || libwin_handle() > 0;
		else if (c == CMD_CUT || c == CMD_PASTE)
			on = libwin_handle() > 0 && top_window() == libwin_handle();
		else if (c == CMD_COPY)
			on = (libwin_handle() > 0 && top_window() == libwin_handle()) || (win_h > 0 && view.page != NULL);
		else if (target >= 0)
			on = win_h > 0 && view_can(&view, target);
		if (menu_on[c] != on)
		{
			menu_ienable(menu.tree, menu_object(&menu, c), (short) on);
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
	log_font();
	make_layout();
	if (win_h > 0)
	{
		clamp_scroll();
		set_sliders();
		redraw_all();
	}
	libwin_font_changed();
}

/* shortcuts that work whichever window (or none) is on top */
int app_key(short kstate, short kreturn)
{
	if (!(kstate & K_CTRL))
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
	}
	switch (KEY_ASCII(kreturn))
	{
	case 0x06:                                        /* Shift+^F */
		if (!(kstate & K_SHIFT))
			return 0;
		command(CMD_FONT);
		return 1;
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
	int shift = (kstate & K_SHIFT) != 0, ctrl = (kstate & K_CTRL) != 0;
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
	case 0x0d:                         /* Return */
		rc = view_follow_selected(&view);
		break;
	case 0x08:                         /* Backspace */
		rc = view_back(&view);
		break;
	}
	if (rc == -2)
	{
		switch (scan)
		{
		case 0x61:                     /* Undo */
			command(CMD_BACK);
			return;
		case 0x62:                     /* Help */
			command(CMD_HELP);
			return;
		case 0x72:                     /* Enter */
			rc = view_follow_selected(&view);
			break;
		case 0x73:                     /* ^Left */
			command(CMD_PREV);
			return;
		case 0x74:                     /* ^Right */
			command(CMD_NEXT);
			return;
		case 0x48:                     /* Up */
			scroll_to(shift ? view.top - rows : view.top - 1, view.left);
			return;
		case 0x50:                     /* Down */
			scroll_to(shift ? view.top + rows : view.top + 1, view.left);
			return;
		case 0x4b:                     /* Left */
			scroll_to(view.top, shift ? view.left - visible_cols() : view.left - 1);
			return;
		case 0x4d:                     /* Right */
			scroll_to(view.top, shift ? view.left + visible_cols() : view.left + 1);
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
	} items[] = {
		{ "  Back                Undo", CMD_BACK },
		{ "-", CMD_NONE },
		{ "  Main page          ^Home", CMD_HOME },
		{ "  Contents              ^T", CMD_TOC },
		{ "  Index                 ^I", CMD_INDEX },
		{ "  Help page           Help", CMD_HELP },
		{ "-", CMD_NONE },
		{ "  Copy page             ^C", CMD_COPY },
		{ "  Library and search    ^L", CMD_LIBRARY },
		{ "  Font...              \x01^F", CMD_FONT },
		{ "-", CMD_NONE },
		{ "  About EHypView...", CMD_ABOUT }
	};
	enum { N = sizeof(items) / sizeof(items[0]) };
	static char off[N][40];
	const char *lab[N];
	short i, r;

	for (i = 0; i < N; i++)
	{
		int target = nav_target(items[i].cmd);

		lab[i] = items[i].label;
		if (target >= 0 && !view_can(&view, target))
		{
			off[i][0] = '~';           /* greyed: popup.h */
			strcpy(off[i] + 1, items[i].label);
			lab[i] = off[i];
		}
	}
	log_event("menu", "popup", NULL);
	r = popup(x, y, lab, N);
	if (r < 0 || items[r].cmd == CMD_NONE)
		return;
	log_event("menu", menu_command_name(items[r].cmd), NULL);
	command(items[r].cmd);
}

static void click(short mx, short my, short button)
{
	short x, y, w, h;
	long py, line, col, r;

	if (button & 2)
	{
		context_menu(mx, my);
		return;
	}
	if (view.page == NULL || lay.line_y == NULL)
		return;
	work_area(&x, &y, &w, &h);
	if (mx < x || my < y || mx >= x + w || my >= y + h)
		return;
	py = my - y + lay.gfx_y[view.top];
	line = layout_line_at(&lay, py, view.top);
	if (line < 0)
		return;
	if (mx - x - MARGIN < 0)
		return;
	col = (mx - x - MARGIN) / cw + view.left;
	r = view_link_at(&view, line, col);
	if (r < 0)
		return;
	view.sel = r;
	redraw_all();
	graf_mouse(BUSYBEE, NULL);
	after(view_follow(&view, &view.page->runs[r]));
	graf_mouse(ARROW, NULL);
}

/* a hypertext and page asked for (VA_START, AC_HELP): the window, then
   the page */
static void show_request(const char *args)
{
	if ((args == NULL || args[0] == '\0') && view.page == NULL)
	{
		libwin_open();             /* nothing asked, nothing shown: the library */
		return;
	}
	open_window();
	if (args != NULL && args[0] != '\0')
	{
		graf_mouse(BUSYBEE, NULL);
		after(view_request(&view, args));
		graf_mouse(ARROW, NULL);
	}
}

static void message(short *msg)
{
	short w, h;

	switch (msg[0])
	{
	case MN_SELECTED:
		if (has_menu)
		{
			int cmd = menu_command(&menu, msg[4]);

			menu_tnormal(menu.tree, msg[3], 1);
			log_event("menu", menu_command_name(cmd), NULL);
			command(cmd);
		}
		break;
	case WM_REDRAW:
		if (msg[3] == win_h)
			redraw(msg[4], msg[5], msg[6], msg[7]);
		break;
	case WM_TOPPED:
		if (msg[3] == win_h)
			wind_set(win_h, WF_TOP, win_h, 0, 0, 0);
		break;
	case WM_BOTTOMED:
		wind_set(msg[3], WF_BOTTOM, 0, 0, 0, 0);
		break;
	case WM_CLOSED:
		if (msg[3] == win_h)
			close_window();
		break;
	case WM_ICONIFY:
	case WM_ALLICONIFY:
		if (msg[3] == win_h && !win_icon)
		{
			wind_get(win_h, WF_CURRXYWH, &win_place[0], &win_place[1], &win_place[2], &win_place[3]);
			win_icon = 1;
			wind_set(win_h, WF_ICONIFY, msg[4], msg[5], msg[6], msg[7]);
			log_event("iconify", "viewer", NULL);
		}
		break;
	case WM_UNICONIFY:
		if (msg[3] == win_h && win_icon)
		{
			win_place[0] = msg[4];
			win_place[1] = msg[5];
			win_place[2] = msg[6];
			win_place[3] = msg[7];
			restore();
			redraw_all();
		}
		break;
	case WM_FULLED:
		if (msg[3] == win_h && !win_icon)
		{
			short cx, cy, cw_, ch_, fx, fy, fw, fh;

			wind_get(win_h, WF_CURRXYWH, &cx, &cy, &cw_, &ch_);
			wind_get(win_h, WF_FULLXYWH, &fx, &fy, &fw, &fh);
			if (cx == fx && cy == fy && cw_ == fw && ch_ == fh)
				wind_get(win_h, WF_PREVXYWH, &fx, &fy, &fw, &fh);
			wind_set(win_h, WF_CURRXYWH, fx, fy, fw, fh);
			redraw_all();
		}
		break;
	case WM_SIZED:
	case WM_MOVED:
		if (msg[3] == win_h && win_icon)    /* the icon goes where it is put */
			wind_set(win_h, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
		else if (msg[3] == win_h)
		{
			w = msg[6] < 12 * cw ? (short) (12 * cw) : msg[6];
			h = msg[7] < 6 * ch ? (short) (6 * ch) : msg[7];
			wind_set(win_h, WF_CURRXYWH, msg[4], msg[5], w, h);
			clamp_scroll();
			set_sliders();
			if (msg[0] == WM_SIZED)
				redraw_all();
		}
		break;
	case WM_ARROWED:
		if (msg[3] == win_h)
		{
			long rows = visible_lines(), cols = visible_cols();

			switch (msg[4])
			{
			case WA_UPPAGE: scroll_to(view.top - rows, view.left); break;
			case WA_DNPAGE: scroll_to(view.top + rows, view.left); break;
			case WA_UPLINE: scroll_to(view.top - 1, view.left); break;
			case WA_DNLINE: scroll_to(view.top + 1, view.left); break;
			case WA_LFPAGE: scroll_to(view.top, view.left - cols); break;
			case WA_RTPAGE: scroll_to(view.top, view.left + cols); break;
			case WA_LFLINE: scroll_to(view.top, view.left - 1); break;
			case WA_RTLINE: scroll_to(view.top, view.left + 1); break;
			}
		}
		break;
	case WM_VSLID:
		if (msg[3] == win_h)
			scroll_to(max_top() * msg[4] / 1000, view.left);
		break;
	case WM_HSLID:
		if (msg[3] == win_h)
			scroll_to(view.top, max_left() * msg[4] / 1000);
		break;
	case AC_OPEN:
		if (msg[4] == menu_id && twin_pass(""))
		{
			log_event("acc", "to the program", NULL);
			break;                     /* EHYPVIEW.PRG runs: its window to the front */
		}
		if (msg[4] == menu_id && screen_check("EHypView", ehv_ui_scr_w))	/* not in ST low */
		{
			av_init("EHYPVIEW|ST-GUIDE");	/* a desktop started since */
			font_check();
			if (view.page != NULL)
				open_window();
			else
				libwin_open();
		}
		break;
	case AC_CLOSE:
		twin_reset();                  /* another program in front */
		if (msg[3] == menu_id)
		{
			/* the AES took the windows */
			if (win_h > 0)
				av_window(win_h, 0);
			win_h = -1;
			win_icon = 0;
			libwin_forget();
		}
		break;
	case AP_TERM:
		if (!is_acc)                   /* an accessory never ends */
			quit_now = 1;
		break;
	case FONT_CHANGED:
		/* a font selector dropped a font on one of our windows */
		if (msg[3] == win_h || msg[3] == libwin_handle() || msg[3] < 0)
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
					viewer_show(path, NULL, 0);
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

/* ---------------------------------------------------------------- */
/* index: load the cache, then rescan in the background              */

/* ---------------------------------------------------------------- */
/* EHYPVIEW.INF: in the boot drive's root, where the accessory finds  */
/* it too; FOLDER= says where the rest is (the AERA guidelines, 6.1)  */

static const char inf_head[] =
	"; EHYPVIEW.INF: EHypView's settings, the program's and the accessory's.\r\n"
	"; A line starting with ';' is a note. EHypView keeps the notes, and\r\n"
	"; any setting it doesn't know, when it writes the file.\r\n\r\n";

int settings_put(const char *key, const char *value, const char *note)
{
	char *text = pf_load(settings_path, NULL), *t;
	int r = -1;

	t = inf_put(text != NULL ? text : inf_head, key, value, note);
	if (t != NULL)
	{
		r = pf_save(settings_path, t, (unsigned long) strlen(t));
		free(t);
	}
	free(text);
	return r;
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
		inf = inf_load(settings_path, NULL);
		f = inf != NULL ? inf_get(inf, "FOLDER") : NULL;
		if (f == NULL || atari_casecmp(f, home_dir) != 0)
			settings_put("FOLDER", home_dir, "where EHypView keeps its files (EHYPVIEW.IDX, the library; "
				     "EHYPVIEW.FNT, the fonts measured): the program writes the folder it was started from");
		inf_free(inf);
		return;
	}
	inf = inf_load(settings_path, NULL);
	f = inf != NULL ? inf_get(inf, "FOLDER") : NULL;
	if (f != NULL && f[0] != '\0' && strlen(f) < sizeof(home_dir) &&
	    (strlen(f) <= 3 || (pf_stat(f, &st) == 0 && st.is_dir)))
		strcpy(home_dir, f);
	else
		strcpy(home_dir, settings_path), home_dir[3] = '\0';	/* X:\ */
	inf_free(inf);
}

static inf_file *read_inf(void)
{
	char path[32];

	path[0] = (char) ('A' + boot_drive());
	strcpy(path + 1, ":\\ST-GUIDE.INF");
	return inf_load(path, NULL);
}

static void start_index(const inf_file *inf)
{
	char **roots;
	unsigned int n = ehv_default_roots(inf, &roots);
	const char *ref = inf ? inf_get(inf, "REF") : NULL;
	char *p;

	p = fs_join(home_dir, "EHYPVIEW.IDX");
	if (p != NULL)
	{
		strncpy(cache_path, p, sizeof(cache_path) - 1);
		free(p);
	}
	if (ref != NULL)
		allref = ref_open(ref, NULL);
	idx = ehv_index_load(cache_path);
	view.idx = idx;
	scanner = ehv_scan_begin((const char *const *) roots, n, idx);
	inf_free_list(roots, n);
}

int scanning(void)
{
	return scanner != NULL;
}

unsigned long scan_found(void)
{
	const ehv_scan_status *st;

	if (scanner == NULL)
		return 0;
	st = ehv_scan_status_get(scanner);
	return st->opened + st->reused;
}

/* a little more scanning; called when there is nothing else to do */
static void scan_some(void)
{
	int r;

	if (scanner == NULL)
		return;
	r = ehv_scan_step(scanner, 4);
	if (r == 0)
	{
		static unsigned long last;
		const ehv_scan_status *st = ehv_scan_status_get(scanner);

		if (st->opened + st->reused != last)
		{
			last = st->opened + st->reused;
			if (view.status[0] == '\0')
				update_info();
			libwin_index_changed();
		}
		return;
	}
	if (r == 1)
	{
		ehv_index *fresh = ehv_scan_finish(scanner, allref);

		scanner = NULL;
		if (fresh != NULL)
		{
			char num[16];

			ehv_index_free(idx);
			idx = fresh;
			view.idx = idx;
			ehv_index_save(idx, cache_path);
			sprintf(num, "%lu", idx->nguides);
			log_event("index", num, "guides");
		}
	} else
	{
		ehv_scan_abort(scanner);
		scanner = NULL;
	}
	update_info();
	libwin_index_changed();
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

/* an accessory takes its memory once, at start-up (see libc.c) */
static void acc_heap(void)
{
	long avail = (long) Malloc(-1L);
	long want = avail / 3;
	void *p;

	if (want > 2L * 1024 * 1024)
		want = 2L * 1024 * 1024;
	if (want < 64L * 1024)
		want = avail - 16L * 1024;
	p = want > 0 ? Malloc(want) : NULL;
	if ((long) p > 0)
		heap_add(p, (size_t) want);
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
	if (win_h > 0)
	{
		wind_close(win_h);
		wind_delete(win_h);
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
	inf_file *inf;
	int fts_busy = 0;

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
	vq_extnd(vdi_h, 1, work_out);
	planes = work_out[4];
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

	find_home();
	font_init(home_dir, settings_path);
	log_font();
	view_init(&view, NULL);
	inf = read_inf();
	start_index(inf);

	if (!is_acc && menu_build(&menu, sys_cw, sys_ch, screen_w, screen_h) == 0)
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
		graf_mouse(BUSYBEE, NULL);
		if (args[0] != '\0')
			view_request(&view, args);
		else if (def != NULL)
			view_open(&view, def, NULL, 0);
		graf_mouse(ARROW, NULL);
		if (view.page != NULL || view.status[0] != '\0')
		{
			make_layout();
			open_window();
			if (view.page != NULL)
				log_event("page", view.doc->path, view.doc->entries[view.page->num].name);
			else
				log_event("status", view.status, NULL);
		} else
		{
			libwin_open();
		}
		app_window_closed();
	}
	inf_free(inf);
	crash_offer("EHypView", "next to EHYPVIEW.PRG");

	while (!quit_now)
	{
		short flags = MU_MESAG, which;
		/* the accessory leaves the library's index to EHYPVIEW.PRG
		   while it runs */
		int paused = is_acc && scanner != NULL && twin_prg() >= 0;
		int busy = (scanner != NULL && !paused) || fts_busy;

		if (win_h > 0 || libwin_handle() > 0 || has_menu)
			flags |= MU_KEYBD | MU_BUTTON;
		if (busy || paused)
			flags |= MU_TIMER;
		which = evnt_multi_(flags, is_acc ? 0x102 : 0x101, 3, 0, busy ? 0 : 1000, msg, &ev);
		if ((which & MU_MESAG) && !libwin_message(msg))
			message(msg);
		if ((which & MU_KEYBD) && crash_test(KEY_SCAN(ev.kreturn)))
			;                         /* test builds only: F1-F4 crash */
		else if (which & MU_KEYBD)
		{
			short top = top_window();

			if (top > 0 && ((top == win_h && win_icon) ||
					(top == libwin_handle() && libwin_iconified())))
				;                     /* an icon takes no keys */
			else if (top == libwin_handle() && top > 0)
				libwin_key(ev.kstate, ev.kreturn);
			else if (win_h > 0 && !win_icon)
				key(ev.kstate, ev.kreturn);
			else
				app_key(ev.kstate, ev.kreturn);
		}
		if (which & MU_BUTTON)
		{
			short h = wind_find(ev.mx, ev.my);

			if (h == win_h && win_h > 0 && !win_icon)
				click(ev.mx, ev.my, ev.mbutton);
			else if (h == libwin_handle() && h > 0)
				libwin_click(ev.mx, ev.my);
		}
		if (which & MU_TIMER)
		{
			if (!paused)
				scan_some();
			fts_busy = libwin_idle();
		}
		else if (fts_busy == 0)
			fts_busy = libwin_idle();
		if (which & (MU_MESAG | MU_KEYBD | MU_BUTTON))
			update_menu();
	}

	if (win_h > 0)
	{
		wind_close(win_h);
		wind_delete(win_h);
	}
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
	free_layout();
	view_free(&view);
	ehv_scan_abort(scanner);
	ehv_index_free(idx);
	ref_close(allref);
	crash_remove();
	av_exit();
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
