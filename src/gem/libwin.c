/*
 * EHypView - the library window: every hypertext found, and one search
 * box.  Typing goes into the search line at the top; Up/Down choose a
 * row; Return (or a click) opens it in the viewer, or starts the
 * full-text search; Esc clears the search.
 *
 * Copyright (C) the EHypView authors.  Free software under the GNU
 * General Public License, version 2 or (at your option) any later
 * version; see COPYING.
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

static void ensure_model(void)
{
	if (!lib_ready)
	{
		lib_init(&lib, idx);
		lib_ready = 1;
	}
}

/* ---------------------------------------------------------------- */
/* geometry: a search line, a gap, then the list                     */

static void work_area(short *x, short *y, short *w, short *h)
{
	wind_get(lib_h, WF_WORKXYWH, x, y, w, h);
}

static short header_h(void)
{
	return (short) (ch + ch / 2 + 2);
}

static long visible_rows(void)
{
	short x, y, w, h;
	long n;

	work_area(&x, &y, &w, &h);
	n = (h - header_h()) / ch;
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
	}
	/* show the heading above the first row */
	if (lib.top == 1 && lib.nrows > 0 && lib.rows[0].kind == LIB_GROUP)
		lib.top = 0;
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
		snprintf(lib_info, sizeof(lib_info), " Looking for hypertexts... %lu found", scan_found());
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

static void draw_rect(short cx, short cy, short cw_, short ch_)
{
	short wx, wy, ww, wh;
	short clip[4], pxy[4];
	char text[160], detail[64];
	long r, rows;
	short y;

	work_area(&wx, &wy, &ww, &wh);
	clip[0] = cx;
	clip[1] = cy;
	clip[2] = (short) (cx + cw_ - 1);
	clip[3] = (short) (cy + ch_ - 1);
	vs_clip(vdi_h, 1, clip);
	fill(0, clip[0], clip[1], clip[2], clip[3]);
	vswr_mode(vdi_h, MD_TRANS);

	/* the search line */
	vst_color(vdi_h, 1);
	vst_effects(vdi_h, 1);
	text_at((short) (wx + MARGIN), (short) (wy + 1), "Search:", 7);
	vst_effects(vdi_h, 0);
	snprintf(text, sizeof(text), "%s_", lib.query);
	text_at((short) (wx + MARGIN + 8 * cw), (short) (wy + 1), text, (long) strlen(text));
	pxy[0] = wx;
	pxy[1] = (short) (wy + ch + ch / 4 + 1);
	pxy[2] = (short) (wx + ww - 1);
	pxy[3] = pxy[1];
	vsl_color(vdi_h, 1);
	vsl_type(vdi_h, 1);
	v_pline(vdi_h, 2, pxy);

	/* the list */
	rows = visible_rows();
	y = (short) (wy + header_h());
	for (r = lib.top; r < lib.top + rows && (unsigned long) r < lib.nrows; r++, y = (short) (y + ch))
	{
		const lib_row *row = &lib.rows[r];
		short fx = 0, color = 1;
		long dl;

		if (y >= cy + ch_ || y + ch <= cy)
			continue;
		lib_row_text(&lib, (unsigned long) r, text, sizeof(text));
		lib_row_detail(&lib, (unsigned long) r, detail, sizeof(detail));
		switch (row->kind)
		{
		case LIB_GROUP:  fx = 1; break;                 /* bold */
		case LIB_ACTION: fx = 8; color = link_color(); break;
		case LIB_NOTE:   fx = 4; break;                 /* italic */
		}
		if (r == lib.sel)
		{
			fill(link_color(), wx, y, (short) (wx + ww - 1), (short) (y + ch - 1));
			vswr_mode(vdi_h, MD_TRANS);
			color = 0;
		}
		vst_color(vdi_h, color);
		vst_effects(vdi_h, fx);
		text_at((short) (wx + MARGIN), y, text, (long) strlen(text));
		dl = (long) strlen(detail);
		if (dl > 0 && (long) (strlen(text) + dl + 3) * cw < ww - 2 * MARGIN)
		{
			vst_effects(vdi_h, r == lib.sel ? 0 : 2);  /* light */
			text_at((short) (wx + ww - MARGIN - dl * cw), y, detail, dl);
		}
	}
	vst_effects(vdi_h, 0);
	vst_color(vdi_h, 1);
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
	int shift = (kstate & K_SHIFT) != 0, ctrl = (kstate & K_CTRL) != 0;
	long rows = visible_rows();
	char q[LIB_QUERY_MAX];
	size_t n = strlen(lib.query);

	if (ctrl)
	{
		if (ascii == 0x0c && viewer_handle() > 0)
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
	case 0x01:                             /* Esc */
		if (n > 0)
			set_query("");
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
	row = lib.top + (my - y - header_h()) / ch;
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
	r = lib_fulltext_step(&lib, 4);
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
