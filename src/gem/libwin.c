/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the library window: every hypertext found, and one search
 * box.  Typing goes into the search line at the top; Up/Down choose a
 * row; Return (or a click) opens it in the viewer, or starts the
 * full-text search.  Backspace and ^X take text out of the search; Esc
 * clears no text (the AERA guidelines, 3.5).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "gemapp.h"
#include "alert.h"
#include "hypfile.h"
#include "library.h"
#include "grect.h"
#include "iconify.h"
#include "av.h"
#include "clip.h"

#define LKIND (NAME | CLOSER | FULLER | MOVER | INFO | SIZER | UPARROW | DNARROW | VSLIDE)
#define MARGIN 4

static short lib_h = -1;
static short lib_icon;                 /* shown as EHypView's icon (WM_ICONIFY) */
static short lib_place[4];             /* where it was before */
static ehv_library lib;
static int lib_ready;
static char lib_name[] = " Hypertexts ";
static char lib_info[120];

short libwin_handle(void)
{
	return lib_h;
}

int libwin_iconified(void)
{
	return lib_h > 0 && lib_icon;
}

static const ehv_bookmarks *lib_bm;   /* the program's, shown first */

static void ensure_model(void)
{
	if (!lib_ready)
	{
		lib_init(&lib, idx);
		lib_set_bookmarks(&lib, lib_bm);
		lib_ready = 1;
	}
}

/* ---------------------------------------------------------------- */
/* geometry: a search line, a gap, then the list                     */

static void work_area(short *x, short *y, short *w, short *h)
{
	wind_get(lib_h, WF_WORKXYWH, x, y, w, h);
}

/* a row: the text with a pixel above and below */
static short row_h(void)
{
	return (short) (ch + 2);
}

/* the search field (framed, with room around it) and a rule under it */
static short header_h(void)
{
	return (short) (ch + 12);
}

static long visible_rows(void)
{
	short x, y, w, h;
	long n;

	work_area(&x, &y, &w, &h);
	n = (h - header_h()) / row_h();
	return n > 0 ? n : 1;
}

static void clamp(void)
{
	long rows = visible_rows(), max = (long) lib.nrows > rows ? (long) lib.nrows - rows : 0;

	if (lib.sel >= 0)
	{
		if (lib.sel < lib.top)
			lib.top = lib.sel;
		else if (lib.sel >= lib.top + rows)
			lib.top = lib.sel - rows + 1;
		/* following the selection to the first row: its heading too
		   (only then: scrolled, row 1 can be the top) */
		if (lib.top == 1 && lib.sel < rows && lib.rows[0].kind == LIB_GROUP)
			lib.top = 0;
	}
	if (lib.top > max)
		lib.top = max;
	if (lib.top < 0)
		lib.top = 0;
}

static void set_slider(void)
{
	long rows = visible_rows(), n = (long) lib.nrows, max = n > rows ? n - rows : 0;

	wind_set(lib_h, WF_VSLSIZE, (short) (n > rows ? rows * 1000L / n : 1000), 0, 0, 0);
	wind_set(lib_h, WF_VSLIDE, (short) (max > 0 ? lib.top * 1000L / max : 0), 0, 0, 0);
}

static void update_info(void)
{
	if (lib_h <= 0)
		return;
	if (scanning())
		snprintf(lib_info, sizeof(lib_info), " %s", index_busy());
	else if (lib.fts != NULL)
		snprintf(lib_info, sizeof(lib_info), " %s", lib.note);
	else if (lib.query[0] != '\0')
		snprintf(lib_info, sizeof(lib_info), " %lu pages, %lu hypertexts", lib.npages, lib.nguides);
	else
		snprintf(lib_info, sizeof(lib_info), " %lu hypertexts", lib.nguides);
	wind_set_str(lib_h, WF_INFO, lib_info);
}

/* ---------------------------------------------------------------- */
/* drawing                                                           */

static void line_(short x1, short y1, short x2, short y2, short color)
{
	short pxy[4];

	pxy[0] = x1;
	pxy[1] = y1;
	pxy[2] = x2;
	pxy[3] = y2;
	vswr_mode(vdi_h, MD_REPLACE);
	vsl_color(vdi_h, color);
	vsl_type(vdi_h, 1);
	v_pline(vdi_h, 2, pxy);
}

static void frame_(short x1, short y1, short x2, short y2, short color)
{
	short p[10];

	p[0] = p[6] = p[8] = x1;
	p[1] = p[3] = p[9] = y1;
	p[2] = p[4] = x2;
	p[5] = p[7] = y2;
	vswr_mode(vdi_h, MD_REPLACE);
	vsl_color(vdi_h, color);
	vsl_type(vdi_h, 1);
	v_pline(vdi_h, 5, p);
}

/*
 * A small picture in front of a row, a character wide, so the kinds tell
 * apart at a glance (and in mono): a book for a hypertext, a ribbon for a
 * bookmark, a page with its corner turned for a page, lines on it for a
 * full-text hit, a magnifier for the search action.
 */
static void glyph(int kind, short x, short y, short color)
{
	/* as wide as three quarters of its height, on square pixels or tall
	   ones (the system font is 8x16 on the first, 8x8 on the second) */
	short h = (short) (ch - 4), w, t = (short) (y + 2), b, r, k;

	if (h < 5)
		h = 5;
	w = (short) ((long) h * 3 * 2 * sys_cw / (4 * sys_ch));
	if (w < 5)
		w = 5;
	if (w > 2 * cw)
		w = (short) (2 * cw);
	b = (short) (t + h - 1);
	r = (short) (x + w - 1);
	switch (kind)
	{
	case LIB_GUIDE:                        /* a book: its spine dark, a label */
		frame_(x, t, r, b, color);
		fill(color, x, t, (short) (x + 2), b);
		frame_((short) (x + 4), (short) (t + 2), (short) (r - 2), (short) (t + 2 + h / 4), color);
		break;
	case LIB_BOOKMARK:                     /* a ribbon with a notch */
		fill(color, (short) (x + 1), t, (short) (r - 1), (short) (b - h / 4));
		for (k = 0; k <= (r - x) / 2 - 1; k++)
		{
			line_((short) (x + 1 + k), (short) (b - h / 4), (short) (x + 1 + k), (short) (b - k), color);
			line_((short) (r - 1 - k), (short) (b - h / 4), (short) (r - 1 - k), (short) (b - k), color);
		}
		break;
	case LIB_PAGE:
	case LIB_TEXT:                         /* a page, its corner turned */
		{
			short c = (short) (w / 3 + 1), p[12];

			p[0] = x;          p[1] = t;
			p[2] = (short) (r - c); p[3] = t;
			p[4] = r;          p[5] = (short) (t + c);
			p[6] = r;          p[7] = b;
			p[8] = x;          p[9] = b;
			p[10] = x;         p[11] = t;
			vswr_mode(vdi_h, MD_REPLACE);
			vsl_color(vdi_h, color);
			v_pline(vdi_h, 6, p);
			line_((short) (r - c), t, (short) (r - c), (short) (t + c), color);
			line_((short) (r - c), (short) (t + c), r, (short) (t + c), color);
			for (k = (short) (t + c + 2); kind == LIB_TEXT && k < b - 1; k = (short) (k + 2))
				line_((short) (x + 2), k, (short) (r - 2), k, color);
		}
		break;
	case LIB_ACTION:                       /* a magnifier: a lens, a handle */
		{
			short d = (short) (w * 2 / 3), dh = (short) ((long) d * sys_ch / (2 * sys_cw)), p[18];

			/* the lens, its corners cut */
			p[0] = (short) (x + 2);  p[1] = t;
			p[2] = (short) (x + d - 2); p[3] = t;
			p[4] = (short) (x + d);  p[5] = (short) (t + 2);
			p[6] = (short) (x + d);  p[7] = (short) (t + dh - 2);
			p[8] = (short) (x + d - 2); p[9] = (short) (t + dh);
			p[10] = (short) (x + 2); p[11] = (short) (t + dh);
			p[12] = x;               p[13] = (short) (t + dh - 2);
			p[14] = x;               p[15] = (short) (t + 2);
			p[16] = (short) (x + 2); p[17] = t;
			vswr_mode(vdi_h, MD_REPLACE);
			vsl_color(vdi_h, color);
			v_pline(vdi_h, 9, p);
			for (k = 0; k < 3; k++)
				line_((short) (x + d - 1 + k), (short) (t + dh - 1), r, (short) (b - 2 + k), color);
		}
		break;
	}
}

static void draw_rect(short cx, short cy, short cw_, short ch_)
{
	short wx, wy, ww, wh;
	short clip[4], fx0, fx1;
	char text[160], detail[64];
	long r, rows;
	short y, rh = row_h(), grey = ncolors >= 16 ? 9 : 1;   /* dark grey */

	work_area(&wx, &wy, &ww, &wh);
	clip[0] = cx;
	clip[1] = cy;
	clip[2] = (short) (cx + cw_ - 1);
	clip[3] = (short) (cy + ch_ - 1);
	vs_clip(vdi_h, 1, clip);
	fill(0, clip[0], clip[1], clip[2], clip[3]);

	/* the search field: a label, then a framed line to type in */
	vswr_mode(vdi_h, MD_TRANS);
	vst_color(vdi_h, 1);
	vst_effects(vdi_h, 1);
	text_at((short) (wx + MARGIN + cw / 2), (short) (wy + 6), "Search", 6);
	vst_effects(vdi_h, 0);
	fx0 = (short) (wx + MARGIN + 8 * cw);
	fx1 = (short) (wx + ww - MARGIN - cw / 2);
	if (fx1 < fx0 + 4 * cw)
		fx1 = (short) (fx0 + 4 * cw);
	frame_(fx0, (short) (wy + 4), fx1, (short) (wy + 4 + ch + 3), 1);
	snprintf(text, sizeof(text), "%s_", lib.query);
	{
		long n = (long) strlen(text), room = (fx1 - fx0 - cw) / cw;
		const char *s = n > room ? text + (n - room) : text;      /* the end, as it is typed */

		vswr_mode(vdi_h, MD_TRANS);
		vst_color(vdi_h, 1);
		text_at((short) (fx0 + cw / 2), (short) (wy + 6), s, (long) strlen(s));
		if (lib.query[0] == '\0' && ncolors >= 16)
		{
			/* what can be typed, in grey (in mono it would be the
			   dotted 'light' text, hard to read: left out) */
			static const char hint[] = "a title, a page, or any word";

			if ((long) sizeof(hint) + 1 < room)
			{
				vst_color(vdi_h, grey);
				text_at((short) (fx0 + cw / 2 + 2 * cw), (short) (wy + 6), hint, (long) sizeof(hint) - 1);
			}
		}
	}
	line_(wx, (short) (wy + header_h() - 2), (short) (wx + ww - 1), (short) (wy + header_h() - 2), 1);

	/* the list */
	rows = visible_rows();
	y = (short) (wy + header_h());
	for (r = lib.top; r < lib.top + rows && (unsigned long) r < lib.nrows; r++, y = (short) (y + rh))
	{
		const lib_row *row = &lib.rows[r];
		short fx = 0, color = 1, dcolor = grey, tx = (short) (wx + MARGIN + 3 * cw);
		long dl, tl, room;

		if (y >= cy + ch_ || y + rh <= cy)
			continue;
		lib_row_text(&lib, (unsigned long) r, text, sizeof(text));
		lib_row_detail(&lib, (unsigned long) r, detail, sizeof(detail));
		switch (row->kind)
		{
		case LIB_GROUP:                    /* bold, at the left, a rule after it */
			fx = 1;
			tx = (short) (wx + MARGIN + cw / 2);
			break;
		case LIB_ACTION: color = link_color(); break;
		case LIB_NOTE:   fx = 4; break;    /* italic */
		}
		if (r == lib.sel)
		{
			fill(link_color(), wx, y, (short) (wx + ww - 1), (short) (y + rh - 1));
			color = dcolor = 0;
		}
		if (row->kind != LIB_GROUP && row->kind != LIB_NOTE)
			glyph(row->kind, (short) (wx + MARGIN + cw), y, r == lib.sel ? 0 : row->kind == LIB_ACTION ? color : 1);
		dl = (long) strlen(detail);
		tl = (long) strlen(text);
		room = (wx + ww - MARGIN - tx) / cw;
		if (dl > 0 && tl + dl + 3 > room)
			dl = 0;                        /* no room for both: the text */
		if (tl > room)
			tl = room;
		vswr_mode(vdi_h, MD_TRANS);
		vst_color(vdi_h, color);
		vst_effects(vdi_h, fx);
		text_at(tx, (short) (y + 1), text, tl);
		vst_effects(vdi_h, 0);
		if (row->kind == LIB_GROUP)
		{
			short lx = (short) (tx + (tl + 1) * cw), ly = (short) (y + rh / 2);

			if (lx < wx + ww - MARGIN)
				line_(lx, ly, (short) (wx + ww - MARGIN - 1), ly, r == lib.sel ? 0 : grey);
		} else if (dl > 0)
		{
			vswr_mode(vdi_h, MD_TRANS);
			vst_color(vdi_h, dcolor);
			text_at((short) (wx + ww - MARGIN - cw / 2 - dl * cw), (short) (y + 1), detail, dl);
		}
	}
	vst_effects(vdi_h, 0);
	vst_color(vdi_h, 1);
	vswr_mode(vdi_h, MD_REPLACE);
	vs_clip(vdi_h, 0, clip);
}

static void redraw(short x, short y, short w, short h)
{
	short rx, ry, rw, rh;

	if (lib_h <= 0)
		return;
	if (lib_icon)
	{
		app_icon_redraw(lib_h, x, y, w, h);
		return;
	}
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, NULL);
	wind_get(lib_h, WF_FIRSTXYWH, &rx, &ry, &rw, &rh);
	while (rw > 0 && rh > 0)
	{
		short ix = rx, iy = ry, iw = rw, ih = rh;

		if (intersect(&ix, &iy, &iw, &ih, x, y, w, h) &&
			intersect(&ix, &iy, &iw, &ih, desk_x, desk_y, desk_w, desk_h))
			draw_rect(ix, iy, iw, ih);
		wind_get(lib_h, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
	}
	graf_mouse(M_ON, NULL);
	wind_update(END_UPDATE);
}

/* the iconified window as it was: the menu, ^L */
static void restore(void)
{
	if (lib_h <= 0 || !lib_icon)
		return;
	wind_set(lib_h, WF_UNICONIFY, lib_place[0], lib_place[1], lib_place[2], lib_place[3]);
	lib_icon = 0;
	log_event("uniconify", "library", NULL);
	clamp();
	set_slider();
}

static void redraw_all(void)
{
	short x, y, w, h;

	if (lib_h <= 0)
		return;
	if (lib_icon)                      /* the work area is the icon's */
	{
		update_info();
		return;
	}
	clamp();
	set_slider();
	update_info();
	work_area(&x, &y, &w, &h);
	redraw(x, y, w, h);
}

/* ---------------------------------------------------------------- */

static void log_search(void)
{
	char q[LIB_QUERY_MAX + 2], n[16];

	snprintf(q, sizeof(q), "\"%s\"", lib.query);
	sprintf(n, "%lu", lib.npages + lib.nguides);
	log_event("search", q, n);
}

void libwin_open(void)
{
	short x, y, w, h;
	char n[16];

	ensure_model();
	if (lib_h > 0)
	{
		restore();
		wind_set(lib_h, WF_TOP, lib_h, 0, 0, 0);
	} else
	{
		lib_h = wind_create(icon_kind(LKIND), desk_x, desk_y, desk_w, desk_h);
		lib_icon = 0;
		if (lib_h <= 0)
		{
			lib_h = -1;
			alert(1, "[3][EHypView: no more windows.][ OK ]");
			return;
		}
		wind_calc(WC_BORDER, LKIND, 0, 0, (short) (60 * cw + 2 * MARGIN), (short) (desk_h * 2 / 3),
			&x, &y, &w, &h);
		if (w > desk_w)
			w = desk_w;
		if (h > desk_h)
			h = desk_h;
		x = (short) (desk_x + desk_w - w - 8 * cw > desk_x ? desk_x + desk_w - w - 8 * cw : desk_x);
		y = (short) (desk_y + ch);
		wind_set_str(lib_h, WF_NAME, lib_name);
		update_info();
		wind_open(lib_h, x, y, w, h);
		if (is_acc)                    /* the desktop drops files on it */
			av_window(lib_h, 1);
		clamp();
		set_slider();
	}
	{
		unsigned long g, readable = 0;

		for (g = 0; idx != NULL && g < idx->nguides; g++)
			if (idx->guides[g].error == HYP_OK)
				readable++;
		sprintf(n, "%lu", readable);
	}
	log_event("library", n, NULL);
}

void libwin_close(void)
{
	if (lib_h > 0)
	{
		if (is_acc)
			av_window(lib_h, 0);
		wind_close(lib_h);
		wind_delete(lib_h);
		log_event("closed", "library", NULL);
	}
	lib_h = -1;
	lib_icon = 0;
	app_window_closed();
}

void libwin_forget(void)
{
	/* AC_CLOSE: the AES has taken the window already */
	if (lib_h > 0)
		av_window(lib_h, 0);
	lib_h = -1;
	lib_icon = 0;
}

void libwin_free(void)
{
	if (lib_ready)
		lib_free(&lib);
	lib_ready = 0;
}

void libwin_font_changed(void)
{
	redraw_all();
}

void libwin_bookmarks(const ehv_bookmarks *bm)
{
	lib_bm = bm;
	if (!lib_ready)
		return;
	lib_set_bookmarks(&lib, bm);
	redraw_all();
}

void libwin_index_changed(void)
{
	if (!lib_ready)
		return;
	if (lib.idx != idx)
	{
		lib_set_index(&lib, idx);
		redraw_all();
	} else
	{
		update_info();
	}
}

static void activate(void)
{
	const char *path, *node;
	unsigned short line;

	switch (lib_activate(&lib, &path, &node, &line))
	{
	case LIB_OPEN:
		graf_mouse(BUSYBEE, NULL);
		viewer_show(path, node, line);
		graf_mouse(ARROW, NULL);
		break;
	case LIB_FULLTEXT:
		if (lib_fulltext_start(&lib) == 0)
		{
			char q[LIB_QUERY_MAX + 2];

			snprintf(q, sizeof(q), "\"%s\"", lib.query);
			log_event("fulltext-start", q, NULL);
			redraw_all();
		}
		break;
	}
}

static void set_query(const char *q)
{
	lib_set_query(&lib, q);
	log_search();
	redraw_all();
}

/* ^C ^X ^V: the search line (^C: the hypertext chosen, else the search) */
void libwin_clip(int what)
{
	char t[LIB_QUERY_MAX + 80], q[LIB_QUERY_MAX];
	size_t n = strlen(lib.query), k;
	const char *p;

	if (what == CLIP_COPY)
	{
		if (lib.sel >= 0)
		{
			lib_row_text(&lib, (unsigned long) lib.sel, t, sizeof(t));
			clip_put(t, (long) strlen(t));
		} else if (n > 0)
		{
			clip_put(lib.query, (long) n);
		}
		return;
	}
	if (what == CLIP_CUT)
	{
		if (n > 0 && clip_put(lib.query, (long) n))
			set_query("");
		return;
	}
	clip_get(t, sizeof(t));
	memcpy(q, lib.query, n);
	for (p = clip_line(t), k = n; *p && k + 1 < sizeof(q); p++)
		if ((unsigned char) *p >= ' ')
			q[k++] = *p;
	q[k] = '\0';
	set_query(q);
}

void libwin_key(short kstate, short kreturn)
{
	short scan = KEY_SCAN(kreturn), ascii = KEY_ASCII(kreturn);
	int shift = (kstate & K_SHIFT) != 0, ctrl = key_ctrl(kstate, kreturn);
	long rows = visible_rows();
	char q[LIB_QUERY_MAX];
	size_t n = strlen(lib.query);

	if (ctrl)
	{
		if (ascii == 0x0c && scan != 0x33 && viewer_handle() > 0)
		{
			viewer_top();                  /* ^L: back to the viewer */
			return;
		}
		app_key(kstate, kreturn);          /* ^O, ^W, ^Q */
		return;
	}
	switch (scan)
	{
	case 0x48:                             /* Up */
		lib_move(&lib, shift ? -rows : -1);
		redraw_all();
		return;
	case 0x50:                             /* Down */
		lib_move(&lib, shift ? rows : 1);
		redraw_all();
		return;
	case 0x47:                             /* Clr/Home */
		lib_move(&lib, shift ? (long) lib.nrows : -(long) lib.nrows);
		redraw_all();
		return;
	case 0x1c:                             /* Return */
	case 0x72:                             /* Enter */
		activate();
		return;
	case 0x53:                             /* Delete: the bookmark chosen */
		if (lib_selected_bookmark(&lib) >= 0)
			app_bookmark_remove(lib_selected_bookmark(&lib));
		return;
	case 0x01:                             /* Esc: nothing (the guidelines,
	                                          3.5: Esc clears no text) */
		return;
	case 0x0e:                             /* Backspace */
		if (n > 0)
		{
			memcpy(q, lib.query, n - 1);
			q[n - 1] = '\0';
			set_query(q);
		}
		return;
	}
	if (ascii >= 32 && ascii != 127 && n + 1 < sizeof(q))
	{
		memcpy(q, lib.query, n);
		q[n] = (char) ascii;
		q[n + 1] = '\0';
		set_query(q);
	}
}

void libwin_click(short mx, short my)
{
	short x, y, w, h;
	long row;

	if (lib_icon)
		return;
	work_area(&x, &y, &w, &h);
	if (mx < x || mx >= x + w || my < y + header_h() || my >= y + h)
		return;
	row = lib.top + (my - y - header_h()) / row_h();
	if (lib_select(&lib, row) != row)
		return;
	redraw_all();
	activate();
}

int libwin_message(short *msg)
{
	short w, h;

	if (lib_h <= 0 || msg[3] != lib_h)
		return 0;
	switch (msg[0])
	{
	case WM_REDRAW:
		redraw(msg[4], msg[5], msg[6], msg[7]);
		break;
	case WM_TOPPED:
		wind_set(lib_h, WF_TOP, lib_h, 0, 0, 0);
		break;
	case WM_CLOSED:
		libwin_close();
		break;
	case WM_ICONIFY:
	case WM_ALLICONIFY:
		if (!lib_icon)
		{
			wind_get(lib_h, WF_CURRXYWH, &lib_place[0], &lib_place[1], &lib_place[2], &lib_place[3]);
			lib_icon = 1;
			wind_set(lib_h, WF_ICONIFY, msg[4], msg[5], msg[6], msg[7]);
			log_event("iconify", "library", NULL);
		}
		break;
	case WM_UNICONIFY:
		if (lib_icon)
		{
			lib_place[0] = msg[4];
			lib_place[1] = msg[5];
			lib_place[2] = msg[6];
			lib_place[3] = msg[7];
			restore();
			redraw_all();
		}
		break;
	case WM_FULLED:
		if (!lib_icon)
		{
			short cx, cy, cw_, ch_, fx, fy, fw, fh;

			wind_get(lib_h, WF_CURRXYWH, &cx, &cy, &cw_, &ch_);
			wind_get(lib_h, WF_FULLXYWH, &fx, &fy, &fw, &fh);
			if (cx == fx && cy == fy && cw_ == fw && ch_ == fh)
				wind_get(lib_h, WF_PREVXYWH, &fx, &fy, &fw, &fh);
			wind_set(lib_h, WF_CURRXYWH, fx, fy, fw, fh);
			redraw_all();
		}
		break;
	case WM_SIZED:
	case WM_MOVED:
		if (lib_icon)                  /* the icon goes where it is put */
		{
			wind_set(lib_h, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
			break;
		}
		w = msg[6] < 20 * cw ? (short) (20 * cw) : msg[6];
		h = msg[7] < 6 * ch ? (short) (6 * ch) : msg[7];
		wind_set(lib_h, WF_CURRXYWH, msg[4], msg[5], w, h);
		clamp();
		set_slider();
		if (msg[0] == WM_SIZED)
			redraw_all();
		break;
	case WM_ARROWED:
		{
			long rows = visible_rows();

			switch (msg[4])
			{
			case WA_UPPAGE: lib.top -= rows; break;
			case WA_DNPAGE: lib.top += rows; break;
			case WA_UPLINE: lib.top--; break;
			case WA_DNLINE: lib.top++; break;
			}
			/* scrolling moves the view, not the selection */
			{
				long sel = lib.sel;

				lib.sel = -1;
				clamp();
				lib.sel = sel;
			}
			set_slider();
			{
				short x, y;

				work_area(&x, &y, &w, &h);
				redraw(x, y, w, h);
			}
		}
		break;
	case WM_VSLID:
		{
			long rows = visible_rows(), n = (long) lib.nrows, sel = lib.sel;
			short x, y;

			lib.top = (n > rows ? n - rows : 0) * msg[4] / 1000;
			lib.sel = -1;
			clamp();
			lib.sel = sel;
			set_slider();
			work_area(&x, &y, &w, &h);
			redraw(x, y, w, h);
		}
		break;
	default:
		return 0;
	}
	return 1;
}

int libwin_idle(void)
{
	unsigned long before, after;
	int r;

	if (!lib_ready || lib.fts == NULL || ehv_fts_done(lib.fts))
		return 0;
	ehv_fts_hits(lib.fts, &before);
	r = lib_fulltext_step(&lib, 1);       /* a page: the caller goes on for a slice */
	ehv_fts_hits(lib.fts, &after);
	if (r != 0 || after != before)
	{
		redraw_all();
		if (r != 0)
		{
			char q[LIB_QUERY_MAX + 2], num[16];

			snprintf(q, sizeof(q), "\"%s\"", lib.query);
			sprintf(num, "%lu", after);
			log_event("fulltext", q, num);
		}
	} else
	{
		update_info();
	}
	return r == 0;
}
