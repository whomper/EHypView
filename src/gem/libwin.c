/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the library window: every hypertext found, and one search
 * box.  Typing goes into the search line at the top; Up/Down choose a
 * row; Return (or a click) opens it in the viewer, or starts the
 * full-text search.  Backspace and ^X take text out of the search; Esc
 * clears no text (the AERA guidelines, 3.5).
 *
 * Two layouts (LIBRARY=, Settings):
 *
 *   Outline    under the search, Continue: the pages read last, as
 *              buttons; then the groups, each heading with its colour,
 *              the number of hypertexts and a triangle that folds it
 *              (a click, Return, Left and Right); a hypertext on two
 *              lines, its title over its author and version.
 *   Two panes  the Subjects on the left, the rows of the one chosen on
 *              the right and, under them, what the row chosen is, with
 *              Open and Index.  Left, Right and Tab go from one side to
 *              the other.  A window narrower than PANES_COLS characters
 *              (an ST in low resolution) shows the Outline.  The
 *              Subjects scroll on their own, with the family's pane
 *              bar of AES objects at their right (libbar.c,
 *              common/ui/panebar.c; the guidelines 5.1); the rest
 *              scrolls as one, with the window's sliders.
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
#include "fsys.h"
#include "libbar.h"
#include "bgwork.h"

#define LKIND (NAME | CLOSER | FULLER | MOVER | INFO | SIZER | UPARROW | DNARROW | VSLIDE)
#define MARGIN 8
#define PANES_COLS 56                  /* the narrowest window with two panes */
#define PANE_COLS 22                   /* the Subjects pane */
#define CHIP_COLS 26                   /* a Continue button's text at most */
#define MAX_CHIPS 5

static short lib_h = -1;
static short lib_icon;                 /* shown as EHypView's icon (WM_ICONIFY) */
static short lib_place[4];             /* where it was before */
static ehv_library lib;
static int lib_ready;
static char lib_name[] = " Hypertexts ";
static char lib_info[120];
static char info_shown[120];           /* the info line the window has */
static int want_layout = LIB_OUTLINE;  /* LIBRARY= */
static int focus_left;                 /* Two panes: the Subjects have the keys */
static long pane_top;                  /* the first line of the Subjects shown */

short libwin_handle(void)
{
	return lib_h;
}

int libwin_iconified(void)
{
	return lib_h > 0 && lib_icon;
}

static const ehv_bookmarks *lib_bm;   /* the program's, shown first */

static const ehv_bookmarks *lib_recent;  /* the pages read last */

/* the rows still to be drawn for the search typed (R_LIST...): in the
   next turn, so that the key itself is answered at once (the
   guidelines, Speed); keys typed meanwhile draw them once */
static int rows_due;

static void ensure_model(void)
{
	if (!lib_ready)
	{
		/* everything in place first: the list made once */
		lib_init(&lib, NULL);
		lib.defer_rows = 1;            /* the rows made in a turn of their own */
		lib.layout = want_layout;
		lib.bm = lib_bm;
		lib.recent = lib_recent;
		lib_set_index(&lib, idx);
		lib_ready = 1;
	}
}

/* ---------------------------------------------------------------- */
/* geometry: the search line, then (Outline) the Continue buttons, or
   (Two panes) the Subjects on the left and a heading, the rows and what
   the row chosen is on the right                                     */

static void work_area(short *x, short *y, short *w, short *h)
{
	wind_get(lib_h, WF_WORKXYWH, x, y, w, h);
}

static int panes(void)
{
	return lib.layout == LIB_PANES;
}

/* the layout asked for, unless the window is too narrow for two panes */
static void apply_layout(void)
{
	int l = want_layout;
	short x, y, w, h;

	if (!lib_ready)
		return;
	if (lib_h > 0 && !lib_icon)
	{
		work_area(&x, &y, &w, &h);
		if (w < PANES_COLS * cw)
			l = LIB_OUTLINE;
	}
	if (l != lib.layout)
	{
		lib_set_layout(&lib, l);
		focus_left = 0;
		log_event("library-layout", l == LIB_PANES ? "panes" : "outline", NULL);
	}
}

/* the search field (framed, with room around it) and a rule under it */
static short header_h(void)
{
	return (short) (ch + 12);
}

/* the leading rows that are pages read last: the Continue buttons */
static long chips(void)
{
	long n = 0;

	if (panes() || lib.query[0] != '\0')
		return 0;
	while ((unsigned long) n < lib.nrows && lib.rows[n].kind == LIB_RECENT)
		n++;
	return n;
}

static short strip_h(void)
{
	return chips() > 0 ? (short) (ch + 14) : 0;
}

/* the Subjects pane, its bar at its right included */
static short pane_w(void)
{
	return panes() ? (short) (PANE_COLS * cw + 2 * MARGIN + libbar_width()) : 0;
}

/* its lines' part: the pane less the bar */
static short pane_text_w(void)
{
	return (short) (pane_w() - libbar_width());
}

static short pane_line_h(void)
{
	return (short) (ch + 6);
}

/* where line i of the Subjects pane is (from pane_top; a rule before
   All hypertexts); below the pane when it isn't shown */
static short pane_y(long i)
{
	short wx, wy, ww, wh, y;
	long k;

	work_area(&wx, &wy, &ww, &wh);
	y = (short) (wy + header_h() + ch + 8);
	for (k = pane_top; k < i && k < (long) lib.npanes; k++)
		y = (short) (y + pane_line_h());
	if (pane_top < (long) lib.npanes && lib.panes[pane_top].kind != PANE_ALL)
		for (k = pane_top + 1; k <= i && k < (long) lib.npanes; k++)
			if (lib.panes[k].kind == PANE_ALL)
				y = (short) (y + 6);
	return i < pane_top ? (short) (wy + wh) : y;
}

/* the Subjects pane's last line on show, the one chosen kept in sight */
static long pane_last(void)
{
	short wx, wy, ww, wh;
	long i;

	work_area(&wx, &wy, &ww, &wh);
	for (i = pane_top; i < (long) lib.npanes; i++)
		if (pane_y(i) + pane_line_h() > wy + wh - 4)
			break;
	return i - 1 >= pane_top ? i - 1 : pane_top;
}

/* the last first line: the one that shows the last line at the bottom
   (pane_y's way: the rule before All hypertexts when it isn't the top) */
static long pane_max_top(void)
{
	short wx, wy, ww, wh, room;
	long t, used = 0, all = -1;

	work_area(&wx, &wy, &ww, &wh);
	room = (short) (wh - 4 - header_h() - ch - 8);
	for (t = 0; t < (long) lib.npanes; t++)
		if (lib.panes[t].kind == PANE_ALL)
			all = t;
	for (t = (long) lib.npanes; t > 0; t--)
	{
		used += pane_line_h();
		if (used + (all > t - 1 ? 6 : 0) > room)
			break;
	}
	if (t >= (long) lib.npanes)
		t = (long) lib.npanes - 1;     /* not even one line fits */
	return t > 0 ? t : 0;
}

static void pane_clamp(void)
{
	if (lib.pane >= 0 && lib.pane < pane_top)
		pane_top = lib.pane;
	while (lib.pane > pane_last() && pane_top < lib.pane)
		pane_top++;
	if (pane_top >= (long) lib.npanes)
		pane_top = 0;
}

/* Two panes: the heading over the rows, what the row chosen is under them */
static short heading_h(void)
{
	return panes() ? (short) (ch + 10) : 0;
}

static short detail_h(void)
{
	return panes() ? (short) (3 * ch + 16) : 0;
}

/* where the rows are */
static void list_area(short *x, short *y, short *w, short *h)
{
	short wx, wy, ww, wh, pw = pane_w();

	work_area(&wx, &wy, &ww, &wh);
	*x = (short) (wx + (pw > 0 ? pw + 4 : 0));
	*y = (short) (wy + header_h() + strip_h() + heading_h());
	*w = (short) (wx + ww - *x);
	*h = (short) (wy + wh - detail_h() - *y);
	if (*h < ch)
		*h = ch;
}

/* a row: the text with a pixel above and below; a hypertext in the
   Outline on two lines, a heading with a little room */
static short row_h(long r)
{
	unsigned char k = lib.rows[r].kind;

	if (k == LIB_GROUP)
		return (short) (ch + 6);
	if (panes())
		return (short) (ch + 4);
	return k == LIB_GUIDE ? (short) (2 * ch + 6) : (short) (ch + 2);
}

static long first_row(void)
{
	return chips();
}

/* the rows from `from` that fit */
static long rows_fit(long from)
{
	short x, y, w, h;
	long r, n = 0;
	long used = 0;

	list_area(&x, &y, &w, &h);
	for (r = from; (unsigned long) r < lib.nrows; r++)
	{
		used += row_h(r);
		if (used > h)
			break;
		n++;
	}
	return n > 0 ? n : 1;
}

static long visible_rows(void)
{
	return rows_fit(lib.top);
}

/* the first row when the last is at the bottom */
static long max_top(void)
{
	short x, y, w, h;
	long r = (long) lib.nrows - 1, used = 0, first = first_row();

	list_area(&x, &y, &w, &h);
	while (r >= first && used + row_h(r) <= h)
		used += row_h(r--);
	return r + 1 > first ? r + 1 : first;
}

static void clamp(void)
{
	long first = first_row(), max = max_top();

	if (lib.top < first)
		lib.top = first;
	if (lib.sel >= first)
	{
		if (lib.sel < lib.top)
			lib.top = lib.sel;
		while (lib.top < lib.sel && lib.sel >= lib.top + rows_fit(lib.top))
			lib.top++;
		/* following the selection to the first row: its heading too
		   (only then: scrolled, row 1 can be the top) */
		if (lib.top == first + 1 && lib.sel < first + rows_fit(first) && lib.rows[first].kind == LIB_GROUP)
			lib.top = first;
	}
	if (lib.top > max)
		lib.top = max;
	if (lib.top < first)
		lib.top = first;
}

static short slider_shown[2] = { -1, -1 };   /* the window's: set again only when they change */

static void set_slider(void)
{
	long first = first_row(), rows = visible_rows(), n = (long) lib.nrows - first, max = max_top() - first;
	short size = (short) (n > rows ? rows * 1000L / n : 1000), at = (short) (max > 0 ? (lib.top - first) * 1000L / max : 0);

	if (size != slider_shown[0])
		wind_set(lib_h, WF_VSLSIZE, size, 0, 0, 0);
	if (at != slider_shown[1])
		wind_set(lib_h, WF_VSLIDE, at, 0, 0, 0);
	slider_shown[0] = size;
	slider_shown[1] = at;
}

/* the subjects (not Bookmarks, Recently read, All, Other) */
static unsigned long subjects(void)
{
	unsigned long i, n = 0;

	for (i = 0; i < lib.npanes; i++)
		if (lib.panes[i].kind == PANE_SUBJECT)
			n++;
	return n;
}

/* "s" after a count of other than one ("1 page", "2 pages") */
static const char *plural(unsigned long n)
{
	return n == 1 ? "" : "s";
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
		snprintf(lib_info, sizeof(lib_info), " %lu page%s, %lu hypertext%s", lib.npages, plural(lib.npages),
			 lib.nguides, plural(lib.nguides));
	else if (panes() && lib.pane >= 0 && lib.panes[lib.pane].kind == PANE_BOOKMARKS)
		snprintf(lib_info, sizeof(lib_info), " %lu bookmark%s", lib.panes[lib.pane].count,
			 plural(lib.panes[lib.pane].count));
	else if (panes() && lib.pane >= 0 && lib.panes[lib.pane].kind == PANE_RECENT)
		snprintf(lib_info, sizeof(lib_info), " %lu page%s read lately", lib.panes[lib.pane].count,
			 plural(lib.panes[lib.pane].count));
	else if (panes() && lib.pane >= 0 && lib.panes[lib.pane].kind != PANE_ALL)
		snprintf(lib_info, sizeof(lib_info), " %s: %lu of %lu hypertext%s", lib.panes[lib.pane].text,
			 lib.nguides, lib.norder, plural(lib.norder));
	else if (subjects() > 1)
		snprintf(lib_info, sizeof(lib_info), " %lu hypertexts in %lu subjects", lib.norder, subjects());
	else
		snprintf(lib_info, sizeof(lib_info), " %lu hypertext%s", lib.norder, plural(lib.norder));
	if (strcmp(lib_info, info_shown) != 0)     /* the AES redraws it: only when it changed */
	{
		strcpy(info_shown, lib_info);
		wind_set_str(lib_h, WF_INFO, lib_info);
	}
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

/* the colours: each subject's (its square, its books' spines), the
   text beside a title, the Subjects pane's ground; in mono, patterns */
static short tint_color(int tint)
{
	static const short c16[] = { 4, 10, 11, 15, 13, 14, 12, 2 };     /* blue, dark red, ... */
	static const short c4[] = { 2, 3 };                             /* red, green */

	if (ncolors >= 16)
		return ui_colour((unsigned short) (tint == 0 ? 9 : c16[(tint - 1) % 8]));
	if (ncolors >= 4)
		return tint == 0 ? 1 : c4[(tint - 1) % 2];
	return 1;
}

static short about_color(void)
{
	return ncolors >= 16 ? ui_colour(13) : 1;  /* dark cyan */
}

static short pane_ground(void)
{
	return ncolors >= 16 ? ui_colour(8) : 0;   /* light grey */
}

static short grey(void)
{
	return ncolors >= 16 ? ui_colour(9) : 1;   /* dark grey */
}

/* a subject's square or spine: its colour; in mono solid, half-tone
   (every other subject) or, with none, a frame */
static void fill_tint(int tint, short x1, short y1, short x2, short y2, short frame)
{
	short pxy[4];

	pxy[0] = x1;
	pxy[1] = y1;
	pxy[2] = x2;
	pxy[3] = y2;
	vswr_mode(vdi_h, MD_REPLACE);
	vsf_perimeter(vdi_h, 0);
	if (ncolors < 4 && tint == 0)
	{
		vsf_interior(vdi_h, FIS_HOLLOW);
		vsf_color(vdi_h, 0);
	} else if (ncolors < 4 && tint % 2 == 0)
	{
		vsf_interior(vdi_h, FIS_PATTERN);
		vsf_style(vdi_h, 4);
		vsf_color(vdi_h, 1);
	} else
	{
		vsf_interior(vdi_h, FIS_SOLID);
		vsf_color(vdi_h, tint_color(tint));
	}
	vr_recfl(vdi_h, pxy);
	vsf_interior(vdi_h, FIS_SOLID);
	frame_(x1, y1, x2, y2, frame);
}

/*
 * A small picture in front of a row, a character wide, so the kinds tell
 * apart at a glance (and in mono): a book for a hypertext (its spine in
 * its subject's colour), a ribbon for a bookmark, a page with its corner
 * turned for a page (and a page read lately), lines on it for a
 * full-text hit, a magnifier for the search action.  h: its height (0:
 * a line's).
 */
static void glyph(int kind, short x, short y, short h, short color, int tint)
{
	/* as wide as three quarters of its height, on square pixels or tall
	   ones (the system font is 8x16 on the first, 8x8 on the second) */
	short w, t = (short) (y + 2), b, r, k;

	if (h <= 0)
		h = (short) (ch - 4);
	if (h < 5)
		h = 5;
	w = (short) ((long) h * 3 * 2 * sys_cw / (4 * sys_ch));
	if (w < 5)
		w = 5;
	if (w > 3 * cw)
		w = (short) (3 * cw);
	b = (short) (t + h - 1);
	r = (short) (x + w - 1);
	switch (kind)
	{
	case LIB_GUIDE:                        /* a book: its spine in colour, a label */
		fill(0, x, t, r, b);
		frame_(x, t, r, b, color);
		fill_tint(tint < 0 ? 1 : tint, x, t, (short) (x + (w + 2) / 4), b, color);
		frame_((short) (x + (w + 2) / 4 + 2), (short) (t + 2), (short) (r - 2), (short) (t + 2 + h / 4), color);
		if (h >= 2 * ch - 6)               /* lines of text on a big one */
			for (k = (short) (t + 4 + h / 4); k < b - 2; k = (short) (k + 3))
				line_((short) (x + (w + 2) / 4 + 2), k, (short) (r - 3), k, color);
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
	case LIB_RECENT:
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

/* a heading's triangle: pointing down when open, right when folded */
static void triangle(short x, short y, int folded, short color)
{
	short n = (short) (cw / 2 + 1), k;

	if (n > (ch + 2) / 2)
		n = (short) ((ch + 2) / 2);
	y = (short) (y + (ch + 6) / 2);
	for (k = 0; k < n; k++)
		if (folded)                        /* |> */
			line_((short) (x + k), (short) (y - n + 1 + k), (short) (x + k), (short) (y + n - 1 - k), color);
		else                               /* \/ */
			line_((short) (x + k), (short) (y - n / 2 + k), (short) (x + 2 * n - 2 - k), (short) (y - n / 2 + k), color);
}

/* text in `room` characters, its first blanks left out; the characters used */
/* the ground under put()'s text is white (a row not chosen): the text
   drawn replacing it, the VDI's fast way */
static int white_ground;

static long put(short x, short y, const char *s, long room, short color, short effects)
{
	long n;

	while (*s == ' ')
		s++;
	n = (long) strlen(s);
	if (n > room)
		n = room;
	if (n <= 0)
		return 0;
	{
		/* bold and underline on the white ground: drawn the cheap way,
		   the same pixels (text_fx_after) */
		short by_hand = white_ground && color != 0 && fast_fx && (effects & ~9) == 0 ? effects : 0;

		vswr_mode(vdi_h, white_ground && color != 0 ? MD_REPLACE : MD_TRANS);
		vst_color(vdi_h, color);
		vst_effects(vdi_h, (short) (effects & ~by_hand));
		text_at(x, y, s, n);
		vst_effects(vdi_h, 0);
		if (by_hand)
			text_fx_after(x, y, n, by_hand, color);
	}
	return n;
}

/* the same at the right, ending before x */
static void put_right(short x, short y, const char *s, long room, short color)
{
	long n;

	while (*s == ' ')
		s++;
	n = (long) strlen(s);
	if (n > room)
		return;                        /* no room: left out */
	put((short) (x - n * cw), y, s, n, color, 0);
}

/* ---------------------------------------------------------------- */
/* what the chosen row is (Two panes), with Open and Index            */

static char about1[96], about2[128], about3[160];
static const ehv_guide *about_guide;

static void describe(void)
{
	const lib_row *r;
	const ehv_guide *g;
	char t[160];

	about1[0] = about2[0] = about3[0] = '\0';
	about_guide = NULL;
	if (lib.sel < 0 || (unsigned long) lib.sel >= lib.nrows)
		return;
	r = &lib.rows[lib.sel];
	g = lib_row_guide(&lib, (unsigned long) lib.sel);
	lib_row_text(&lib, (unsigned long) lib.sel, t, sizeof(t));
	snprintf(about1, sizeof(about1), "%s", t[0] == ' ' && t[1] == ' ' ? t + 2 : t);
	if (r->kind == LIB_PAGE || r->kind == LIB_TEXT)
	{
		lib_row_detail(&lib, (unsigned long) lib.sel, t, sizeof(t));
		snprintf(about2, sizeof(about2), "In %s", t);
	}
	if (g != NULL)
	{
		const char *ver = lib_version(g->version);

		about_guide = g;
		if (r->kind == LIB_GUIDE && g->author != NULL && g->author[0] != '\0')
			snprintf(about2, sizeof(about2), "By %s%s%s", g->author, ver[0] ? " \xfa version " : "", ver);
		else if (r->kind == LIB_GUIDE && ver[0] != '\0')
			snprintf(about2, sizeof(about2), "Version %s", ver);
		else if (r->kind == LIB_GUIDE && lib_subject(g->subject) != NULL)
			lib_subject_text(lib_subject(g->subject), about2, sizeof(about2));
		else if (r->kind != LIB_GUIDE)
			snprintf(about2, sizeof(about2), "In %s", g->database != NULL ? g->database : g->key);
		if (g->ref_path != NULL)
			snprintf(about3, sizeof(about3), "%s \xfa with %s", g->path, fs_basename(g->ref_path));
		else
			snprintf(about3, sizeof(about3), "%s", g->path);
	} else if (r->kind == LIB_BOOKMARK || r->kind == LIB_RECENT)
	{
		const ehv_bookmarks *from = r->kind == LIB_BOOKMARK ? lib.bm : lib.recent;

		if (from != NULL && r->ref < from->n)
			snprintf(about3, sizeof(about3), "%s", from->b[r->ref].path);
	}
}

/* the hypertext chosen has a page called Index: looked up in the
   index file once for it, not at each redraw */
static const ehv_guide *index_of;     /* the guide asked about, NULL: none */
static int index_has;

static int has_index(const ehv_guide *g)
{
	ehv_hits hit;

	/* the model's index: the program's may be a newer one already */
	if (g == NULL || lib.idx == NULL)
		return 0;
	if (g != index_of)
	{
		ehv_hits_init(&hit);
		ehv_index_find_symbol(lib.idx, "Index", (long) (g - lib.idx->guides), &hit, 1);
		index_has = hit.n > 0 && hit.s[0].kind == EHV_SYM_NODE;
		ehv_hits_free(&hit);
		index_of = g;
	}
	return index_has;
}

static int can_open(void)
{
	unsigned char k = lib.sel >= 0 && (unsigned long) lib.sel < lib.nrows ? lib.rows[lib.sel].kind : LIB_NOTE;

	return k == LIB_GUIDE || k == LIB_PAGE || k == LIB_TEXT || k == LIB_BOOKMARK || k == LIB_RECENT;
}

/* the buttons: Open over Index, at the right of the strip */
static void detail_buttons(short *bx, short *oy, short *iy, short *bw, short *bh)
{
	short wx, wy, ww, wh;

	work_area(&wx, &wy, &ww, &wh);
	*bw = (short) (8 * cw);
	*bh = (short) (ch + 4);
	*bx = (short) (wx + ww - MARGIN - 4 - *bw);
	*oy = (short) (wy + wh - detail_h() + 8);
	*iy = (short) (*oy + *bh + 6);
}

static void button(short x, short y, short w, short h, const char *label, int bold)
{
	long n = (long) strlen(label);

	fill(0, x, y, (short) (x + w - 1), (short) (y + h - 1));
	frame_(x, y, (short) (x + w - 1), (short) (y + h - 1), 1);
	line_((short) (x + 2), (short) (y + h), (short) (x + w), (short) (y + h), 1);      /* a shadow */
	line_((short) (x + w), (short) (y + 2), (short) (x + w), (short) (y + h), 1);
	put((short) (x + (w - n * cw) / 2), (short) (y + 2), label, n, 1, bold ? 1 : 0);
}

static void draw_detail(short wx, short ww, short y)
{
	short bx, oy, iy, bw, bh, x = (short) (wx + pane_w() + 4 + MARGIN), tx, wy, wh;
	long room;

	work_area(&wx, &wy, &ww, &wh);
	fill(0, (short) (wx + pane_w() + 4), y, (short) (wx + ww - 1), (short) (wy + wh - 1));
	line_((short) (wx + pane_w() + 4), y, (short) (wx + ww - 1), y, 1);
	line_((short) (wx + pane_w() + 4), (short) (y + 3), (short) (wx + ww - 1), (short) (y + 3), 1);
	describe();
	if (about1[0] == '\0')
		return;
	if (about_guide != NULL)
		glyph(LIB_GUIDE, x, (short) (y + 6), (short) (3 * ch - 2), 1,
		      lib.rows[lib.sel].kind == LIB_GUIDE ? lib.rows[lib.sel].tint :
		      lib.tint != NULL ? lib.tint[about_guide - lib.idx->guides] : 0);
	else
		glyph(lib.rows[lib.sel].kind, x, (short) (y + 6), (short) (ch - 4), 1, 0);
	tx = (short) (x + 4 * cw);
	detail_buttons(&bx, &oy, &iy, &bw, &bh);
	room = (bx - cw - tx) / cw;
	put(tx, (short) (y + 7), about1, room, 1, 1);
	put(tx, (short) (y + 7 + ch), about2, room, 1, 0);
	put(tx, (short) (y + 7 + 2 * ch), about3, room, about_color(), 0);
	if (can_open())
		button(bx, oy, bw, bh, "Open", 1);
	if (has_index(about_guide))
		button(bx, iy, bw, bh, "Index", 0);
}

/* ---------------------------------------------------------------- */

/* the Subjects pane's bar where the window is now */
static void bar_place(void)
{
	short wx, wy, ww, wh, y;

	work_area(&wx, &wy, &ww, &wh);
	y = (short) (wy + header_h());
	libbar_set((short) (wx + pane_text_w()), y, libbar_width(), (short) (wy + wh - y),
		   (long) lib.npanes, pane_last() - pane_top + 1, pane_top);
}

/* the line chosen when the pane was drawn last: when it changed, the
   pane goes to it; the bar scrolls the pane without choosing */
static long pane_seen = -2;

/* the Subjects pane, within the rectangle cx, cy, cw_, ch_ */
static void draw_panes(short wx, short wy, short wh, short cx, short cy, short cw_, short ch_)
{
	short pw = pane_text_w(), y = (short) (wy + header_h()), lh = pane_line_h();
	unsigned long i;

	if (lib.pane != pane_seen)
	{
		pane_clamp();
		pane_seen = lib.pane;
	}
	if (pane_top > pane_max_top())
		pane_top = pane_max_top();     /* a taller window: no room left below */
	fill(pane_ground(), wx, y, (short) (wx + pw - 1), (short) (wy + wh - 1));
	{
		short bx = (short) (wx + pane_w());

		fill(0, (short) (bx + 1), y, (short) (bx + 2), (short) (wy + wh - 1));
		/* a double line beside it */
		line_(bx, y, bx, (short) (wy + wh - 1), 1);
		line_((short) (bx + 3), y, (short) (bx + 3), (short) (wy + wh - 1), 1);
	}
	put((short) (wx + MARGIN + cw / 2), (short) (y + 4), "Subjects", PANE_COLS, 1, 1);
	for (i = (unsigned long) pane_top; i < lib.npanes && (long) i <= pane_last(); i++)
	{
		const lib_pane *p = &lib.panes[i];
		short color = 1, gx = (short) (wx + MARGIN + cw / 2 + (p->level ? 2 * cw : 0));    /* a part: under its subject */
		char n[12];
		long room;

		y = pane_y((long) i);
		if (p->kind == PANE_ALL && (long) i > pane_top)  /* a rule after Bookmarks, Recently read */
			line_((short) (wx + MARGIN), (short) (y - 4), (short) (wx + pw - MARGIN - 1), (short) (y - 4), 1);
		if ((long) i == lib.pane && !focus_left && ncolors < 16)
			frame_((short) (wx + 1), y, (short) (wx + pw - 2), (short) (y + lh - 1), 1);
		else if ((long) i == lib.pane)
		{
			fill(focus_left ? link_color() : grey(), (short) (wx + 1), y,
			     (short) (wx + pw - 2), (short) (y + lh - 1));
			color = 0;
		}
		if (lib.query[0] != '\0' && (long) i == lib.pane)
		{
			/* searching: the rows aren't the pane's */
			fill(pane_ground(), (short) (wx + 1), y, (short) (wx + pw - 2), (short) (y + lh - 1));
			frame_((short) (wx + 1), y, (short) (wx + pw - 2), (short) (y + lh - 1), 1);
			color = 1;
		}
		switch (p->kind)
		{
		case PANE_BOOKMARKS: glyph(LIB_BOOKMARK, gx, (short) (y + 2), 0, color, 0); break;
		case PANE_RECENT:    glyph(LIB_RECENT, gx, (short) (y + 2), 0, color, 0); break;
		case PANE_ALL:       glyph(LIB_GUIDE, gx, (short) (y + 2), 0, color, -1); break;
		default:
			fill_tint(p->tint, gx, (short) (y + 4), (short) (gx + cw - 2), (short) (y + lh - 5), color);
		}
		sprintf(n, "%lu", p->count);
		room = PANE_COLS - 3 - (long) strlen(n) - 1 - (p->level ? 2 : 0);
		put((short) (gx + 2 * cw), (short) (y + 3), p->text, room, color, 0);
		put_right((short) (wx + pw - MARGIN - cw / 2), (short) (y + 3), n, 8, color);
	}
	bar_place();
	if (!libbar_draw(cx, cy, cw_, ch_))      /* too low for one: the pane's ground */
		fill(pane_ground(), (short) (wx + pw), (short) (wy + header_h()), (short) (wx + pane_w() - 1),
		     (short) (wy + wh - 1));
}

/* Continue: the pages read last, as buttons; where each is */
static short chip_x[MAX_CHIPS], chip_w[MAX_CHIPS];
static long nchips_shown;

static void chips_layout(short wx, short ww)
{
	long i, n = chips();
	short x = (short) (wx + MARGIN + cw / 2 + 10 * cw);
	char t[160];

	nchips_shown = 0;
	for (i = 0; i < n && i < MAX_CHIPS; i++)
	{
		long len;

		lib_row_text(&lib, (unsigned long) i, t, sizeof(t));
		len = (long) strlen(t) - 2;
		if (len > CHIP_COLS)
			len = CHIP_COLS;
		chip_x[i] = x;
		chip_w[i] = (short) ((len + 2) * cw);
		if (x + chip_w[i] + 3 > wx + ww - MARGIN)
			break;
		x = (short) (x + chip_w[i] + cw);
		nchips_shown++;
	}
}

static void draw_chips(short wx, short ww, short y)
{
	short h = strip_h();
	long i;
	char t[160];

	fill(pane_ground(), wx, y, (short) (wx + ww - 1), (short) (y + h - 3));
	put((short) (wx + MARGIN + cw / 2), (short) (y + 6), "Continue", 9, 1, 1);
	chips_layout(wx, ww);
	for (i = 0; i < nchips_shown; i++)
	{
		short x = chip_x[i], w = chip_w[i], cy = (short) (y + 4), chh = (short) (ch + 4);
		int sel = i == lib.sel;

		lib_row_text(&lib, (unsigned long) i, t, sizeof(t));
		fill(sel ? link_color() : 0, x, cy, (short) (x + w - 1), (short) (cy + chh - 1));
		frame_(x, cy, (short) (x + w - 1), (short) (cy + chh - 1), 1);
		line_((short) (x + 2), (short) (cy + chh), (short) (x + w), (short) (cy + chh), 1);
		line_((short) (x + w), (short) (cy + 2), (short) (x + w), (short) (cy + chh), 1);
		put((short) (x + cw), (short) (cy + 2), t, w / cw - 2, sel ? 0 : 1, 0);
	}
	line_(wx, (short) (y + h - 2), (short) (wx + ww - 1), (short) (y + h - 2), 1);
}

/* one row of the list, at y */
static void draw_row_parts(long r, const lib_row *row, short rh, short lx, short lw, short y,
                           char *text, char *detail, short color, short dcolor, short acolor, int sel);

static void draw_row(long r, short lx, short lw, short y)
{
	const lib_row *row = &lib.rows[r];
	short rh = row_h(r), color = 1, dcolor = grey(), acolor = about_color();
	char text[160], detail[64];
	int sel = r == lib.sel;

	lib_row_text(&lib, (unsigned long) r, text, sizeof(text));
	lib_row_detail(&lib, (unsigned long) r, detail, sizeof(detail));
	if (sel && panes() && focus_left && ncolors < 16)
	{
		fill(0, lx, y, (short) (lx + lw - 1), (short) (y + rh - 1));
		frame_(lx, y, (short) (lx + lw - 1), (short) (y + rh - 1), 1);     /* the keys are elsewhere */
	} else if (sel)
	{
		fill(panes() && focus_left ? grey() : link_color(), lx, y, (short) (lx + lw - 1), (short) (y + rh - 1));
		color = dcolor = acolor = 0;
	} else
		fill(0, lx, y, (short) (lx + lw - 1), (short) (y + rh - 1));
	sel = sel && !(panes() && focus_left && ncolors < 16);
	white_ground = !sel;
	draw_row_parts(r, row, rh, lx, lw, y, text, detail, color, dcolor, acolor, sel);
	white_ground = 0;
}

static void draw_row_parts(long r, const lib_row *row, short rh, short lx, short lw, short y,
                           char *text, char *detail, short color, short dcolor, short acolor, int sel)
{
	short right = (short) (lx + lw - MARGIN - cw / 2);
	short gx = (short) (lx + MARGIN + cw), tx = (short) (lx + MARGIN + 3 * cw);
	long room, tl;

	if (row->kind == LIB_GROUP && (row->flags & LIB_SUB))
	{
		/* a part of a subject: under its heading's text, plain */
		short x = (short) (lx + MARGIN + cw / 2 + (panes() ? 0 : 4 * cw)), ty = (short) (y + 3);
		char n[12];

		room = (right - x) / cw;
		tl = put(x, ty, text, room, 1, 0);
		x = (short) (x + (tl + 1) * cw);
		sprintf(n, "%lu", row->ref);
		if (x + (long) strlen(n) * cw < right)
		{
			put(x, ty, n, 8, acolor, 0);
			x = (short) (x + ((long) strlen(n) + 1) * cw);
		}
		if (x < right)
			line_(x, (short) (y + rh / 2), right, (short) (y + rh / 2), grey());
		return;
	}
	if (row->kind == LIB_GROUP)
	{
		short x = (short) (lx + MARGIN + cw / 2), ty = (short) (y + 3);
		char n[12];

		if (row->flags & LIB_FOLDS)
		{
			triangle(x, y, (row->flags & LIB_FOLDED) != 0, color);
			x = (short) (x + 2 * cw);
		}
		if (row->tint > 0 || (row->flags & LIB_FOLDS && row->text != NULL && strcmp(row->text, "Bookmarks") != 0))
		{
			fill_tint(row->tint, x, (short) (y + 4), (short) (x + cw - 2), (short) (y + rh - 5), color);
			x = (short) (x + 2 * cw);
		} else if (row->flags & LIB_FOLDS)
		{
			glyph(LIB_BOOKMARK, x, (short) (y + 2), 0, color, 0);
			x = (short) (x + 2 * cw);
		}
		room = (right - x) / cw;
		tl = put(x, ty, text, room, color, 1);
		x = (short) (x + (tl + 1) * cw);
		if (row->flags & LIB_FOLDS)
		{
			sprintf(n, "%lu", row->ref);
			if (x + (long) strlen(n) * cw < right)
			{
				put(x, ty, n, 8, sel ? 0 : acolor, 0);
				x = (short) (x + ((long) strlen(n) + 1) * cw);
			}
		}
		if (x < right)
			line_(x, (short) (y + rh / 2), right, (short) (y + rh / 2), sel ? 0 : grey());
		return;
	}
	if (row->kind == LIB_GUIDE && !panes())
	{
		/* two lines: the title over its author and version */
		char about[128];
		long dl = (long) strlen(detail);

		glyph(LIB_GUIDE, gx, (short) (y + 1), (short) (2 * ch), sel ? 0 : 1, row->tint);
		tx = (short) (gx + 4 * cw);
		lib_row_about(&lib, (unsigned long) r, about, sizeof(about));
		room = (right - tx) / cw;
		tl = (long) strlen(text) - 2;
		if (dl > 0 && tl + dl + 3 > room)
			dl = 0;
		put(tx, (short) (y + 3), text, dl > 0 ? room - dl - 2 : room, color, 1);
		put(tx, (short) (y + 3 + ch), about, room, acolor, 0);
		if (dl > 0)
			put_right(right, (short) (y + 3), detail, dl, acolor);
		return;
	}
	if (row->kind != LIB_NOTE && row->kind != LIB_WAIT)
		glyph(row->kind, gx, (short) (y + (rh - ch) / 2), 0,
		      sel ? 0 : row->kind == LIB_ACTION ? link_color() : 1, row->tint);
	room = (right - tx) / cw;
	{
		long dl = (long) strlen(detail);

		tl = (long) strlen(text) - 2;
		if (dl > 0 && tl + dl + 3 > room)
			dl = 0;                        /* no room for both: the text */
		put(tx, (short) (y + (rh - ch) / 2), text, dl > 0 ? room - dl - 2 : room,
		    row->kind == LIB_ACTION && !sel ? link_color() : row->kind == LIB_WAIT ? dcolor : color,
		    row->kind == LIB_NOTE ? 4 : 0);
		if (dl > 0)
			put_right(right, (short) (y + (rh - ch) / 2), detail, dl, panes() ? acolor : dcolor);
	}
}

/* the search line */
static void draw_search(short wx, short wy, short ww)
{
	char text[160];
	short fx0, fx1;

	fill(0, wx, wy, (short) (wx + ww - 1), (short) (wy + header_h() - 1));

	/* the search field: a label (bold, the cheap way on its white
	   ground: put), then a framed line to type in */
	white_ground = 1;
	put((short) (wx + MARGIN + cw / 2), (short) (wy + 6), "Search", 6, 1, 1);
	white_ground = 0;
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
				vst_color(vdi_h, grey());
				text_at((short) (fx0 + cw / 2 + 2 * cw), (short) (wy + 6), hint, (long) sizeof(hint) - 1);
			}
		}
	}
	line_(wx, (short) (wy + header_h() - 2), (short) (wx + ww - 1), (short) (wy + header_h() - 2), 1);
}

static void draw_rect(short cx, short cy, short cw_, short ch_)
{
	short wx, wy, ww, wh, lx, ly, lw, lh;
	short clip[4];
	long r;
	short y;

	work_area(&wx, &wy, &ww, &wh);
	clip[0] = cx;
	clip[1] = cy;
	clip[2] = (short) (cx + cw_ - 1);
	clip[3] = (short) (cy + ch_ - 1);
	clip_on(clip);
	/* each part on its own ground, filled just before it is drawn (no
	   white over the whole window first: it flickered, slowly drawn),
	   and only the parts in the rectangle */
	if (cy < wy + header_h())
		draw_search(wx, wy, ww);

	if (strip_h() > 0)
	{
		fill(0, wx, (short) (wy + header_h()), (short) (wx + ww - 1), (short) (wy + header_h() + strip_h() - 1));
		draw_chips(wx, ww, (short) (wy + header_h()));
	}
	list_area(&lx, &ly, &lw, &lh);
	if (panes())
	{
		const lib_pane *p = lib.pane >= 0 ? &lib.panes[lib.pane] : NULL;
		short hx = (short) (lx + MARGIN + cw / 2), hy = (short) (wy + header_h() + 4);

		if (cx < wx + pane_w() + 4)
			draw_panes(wx, wy, wh, cx, cy, cw_, ch_);
		if (cy < ly)
		{
			char head[96];

			fill(0, lx, (short) (wy + header_h()), (short) (lx + lw - 1), (short) (ly - 1));
			/* the heading over the rows (a part: with its subject) */
			if (lib.query[0] != '\0')
				put(hx, hy, "Found", 8, 1, 1);
			else if (p != NULL)
			{
				if (p->kind == PANE_SUBJECT || p->kind == PANE_OTHER)
				{
					fill_tint(p->tint, hx, (short) (hy + 2), (short) (hx + cw - 2), (short) (hy + ch - 3), 1);
					hx = (short) (hx + 2 * cw);
				}
				if (p->kind == PANE_SUBJECT && p->level)
					lib_subject_text(p->subj, head, sizeof(head));
				else
					snprintf(head, sizeof(head), "%s", p->text);
				put(hx, hy, head, (lx + lw - MARGIN - hx) / cw, 1, 1 | 8);     /* bold, underlined */
			}
			line_(lx, (short) (ly - 3), (short) (lx + lw - 1), (short) (ly - 3), 1);
		}
		if (cy + ch_ > wy + wh - detail_h())
			draw_detail(wx, ww, (short) (wy + wh - detail_h()));
	}

	/* the rows, clipped to their area */
	{
		short lc[4];

		lc[0] = lx > cx ? lx : cx;
		lc[1] = ly > cy ? ly : cy;
		lc[2] = (short) (lx + lw - 1 < clip[2] ? lx + lw - 1 : clip[2]);
		lc[3] = (short) (ly + lh - 1 < clip[3] ? ly + lh - 1 : clip[3]);
		if (lc[0] <= lc[2] && lc[1] <= lc[3])
		{
			clip_on(lc);
			y = ly;
			for (r = lib.top; (unsigned long) r < lib.nrows && y < ly + lh; r++)
			{
				short rh = row_h(r);

				if (y < lc[3] + 1 && y + rh > lc[1])
					draw_row(r, lx, lw, y);
				y = (short) (y + rh);
			}
			if (y < ly + lh)               /* under the last row */
				fill(0, lx, y, (short) (lx + lw - 1), (short) (ly + lh - 1));
		}
	}
	vst_effects(vdi_h, 0);
	vst_color(vdi_h, 1);
	vswr_mode(vdi_h, MD_REPLACE);
	clip_off();
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
	apply_layout();
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

/*
 * A part of the window drawn again, not the whole: what changed (a slow
 * machine draws the window in a visible while).  R_ALL is redraw_all().
 */
#define R_HEAD   1                     /* the search line */
#define R_PANE   2                     /* the Subjects pane */
#define R_LIST   4                     /* Continue, the heading and the rows */
#define R_DETAIL 8                     /* what the row chosen is */

static void refresh(int what)
{
	short wx, wy, ww, wh, lx, ly, lw, lh, top;

	if (lib_h <= 0 || lib_icon)
	{
		redraw_all();
		return;
	}
	clamp();
	set_slider();
	update_info();
	work_area(&wx, &wy, &ww, &wh);
	list_area(&lx, &ly, &lw, &lh);
	top = (short) (wy + header_h());
	if (what & R_HEAD)
		redraw(wx, wy, ww, header_h());
	if ((what & R_PANE) && panes())
		redraw(wx, top, (short) (pane_w() + 4), (short) (wh - header_h()));
	if (what & R_LIST)
		redraw(lx, top, lw, (short) (ly + lh - top));
	if ((what & R_DETAIL) && panes())
		redraw(lx, (short) (ly + lh), lw, (short) (wy + wh - ly - lh));
}

/* the search field alone: a key typed, nothing else drawn in its turn
   (the sliders and the info line with the rows, in a later one) */
static void refresh_head(void)
{
	short wx, wy, ww, wh, fx0;

	if (lib_h <= 0 || lib_icon)
	{
		refresh(R_HEAD);
		return;
	}
	work_area(&wx, &wy, &ww, &wh);
	fx0 = (short) (wx + MARGIN + 8 * cw);
	redraw(fx0, wy, (short) (wx + ww - fx0), header_h());
}

/* the rows' part only, as it is (scrolled: the selection not followed) */
static void redraw_list(void)
{
	short wx, wy, ww, wh, lx, ly, lw, lh;

	work_area(&wx, &wy, &ww, &wh);
	list_area(&lx, &ly, &lw, &lh);
	redraw(lx, (short) (wy + header_h()), lw, (short) (ly + lh - wy - header_h()));
}

/* one row (a Continue button: the strip) */
static void refresh_row(long r)
{
	short wx, wy, ww, wh, lx, ly, lw, lh, y;
	long k;

	if (lib_h <= 0 || lib_icon || r < 0 || (unsigned long) r >= lib.nrows)
		return;
	work_area(&wx, &wy, &ww, &wh);
	if (r < first_row())
	{
		redraw(wx, (short) (wy + header_h()), ww, strip_h());
		return;
	}
	if (r < lib.top)
		return;
	list_area(&lx, &ly, &lw, &lh);
	for (y = ly, k = lib.top; k < r && y < ly + lh; k++)
		y = (short) (y + row_h(k));
	if (y < ly + lh)
		redraw(lx, y, lw, (short) (y + row_h(r) > ly + lh ? ly + lh - y : row_h(r)));
}

/* one line of the Subjects pane */
static void refresh_pane_line(long i)
{
	short wx, wy, ww, wh;

	if (lib_h <= 0 || lib_icon || !panes() || i < pane_top || i > pane_last())
		return;
	work_area(&wx, &wy, &ww, &wh);
	redraw(wx, pane_y(i), pane_w(), pane_line_h());
}

/* the selection moved: the two rows, unless the list scrolled */
static void sel_moved(long old_sel, long old_top)
{
	clamp();
	if (lib.top != old_top)
	{
		refresh(R_LIST | R_DETAIL);
		return;
	}
	set_slider();
	update_info();
	if (old_sel != lib.sel)
	{
		refresh_row(old_sel);
		refresh_row(lib.sel);
	}
	if (panes())
		refresh(R_DETAIL);
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
			alert(1, "[3][The system has no window left for EHypView.|Close a window and try again.][ OK ]");
			return;
		}
		wind_calc(WC_BORDER, LKIND, 0, 0, (short) ((want_layout == LIB_PANES ? 78 : 64) * cw + 2 * MARGIN),
			(short) (desk_h * 3 / 4), &x, &y, &w, &h);
		if (w > desk_w)
			w = desk_w;
		if (h > desk_h)
			h = desk_h;
		x = (short) (desk_x + desk_w - w - 8 * cw > desk_x ? desk_x + desk_w - w - 8 * cw : desk_x);
		y = (short) (desk_y + ch);
		/* its text on the 8-pixel grid: the VDI draws it far faster (the
		   guidelines' Speed; TOS 1.04: 65 ms a line on an ST otherwise) */
		x = wind_x8(LKIND, x, y, w, h);
		wind_set_str(lib_h, WF_NAME, lib_name);
		info_shown[0] = '\x01';            /* a new window: none yet */
		slider_shown[0] = slider_shown[1] = -1;
		update_info();
		wind_open(lib_h, x, y, w, h);
		if (is_acc)                    /* the desktop drops files on it */
			av_window(lib_h, 1);
		apply_layout();
		clamp();
		set_slider();
		update_info();
	}
	{
		unsigned long g, readable = 0;

		for (g = 0; idx != NULL && g < idx->nguides; g++)
			if (idx->guides[g].error == HYP_OK)
				readable++;
		sprintf(n, "%lu", readable);
	}
	log_event("library", n, NULL);
	log_event("library-layout", panes() ? "panes" : "outline", NULL);
	if (panes())
	{
		short x, y, w, h;
		char at[32];

		bar_place();
		libbar_rect(&x, &y, &w, &h);
		sprintf(at, "%d %d %d %d", x, y, w, h);
		log_event("library-bar", at, NULL);
	}
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
	rows_due = 0;
}

void libwin_font_changed(void)
{
	apply_layout();
	redraw_all();
}

void libwin_recent(const ehv_bookmarks *recent)
{
	lib_recent = recent;
	if (!lib_ready)
		return;
	lib_set_recent(&lib, recent);
	refresh(R_PANE | R_LIST | R_DETAIL);
}

void libwin_layout(int layout)
{
	want_layout = layout == LIB_PANES ? LIB_PANES : LIB_OUTLINE;
	if (!lib_ready)
		return;
	if (lib_h > 0 && !lib_icon && want_layout == LIB_PANES)
	{
		/* room for both panes, where the screen has it */
		short x, y, w, h, nw;

		wind_get(lib_h, WF_CURRXYWH, &x, &y, &w, &h);
		wind_calc(WC_BORDER, LKIND, 0, 0, (short) (78 * cw + 2 * MARGIN), 10, &nw, &nw, &nw, &nw);
		if (w < nw)
		{
			w = nw > desk_w ? desk_w : nw;
			if (x + w > desk_x + desk_w)
				x = (short) (desk_x + desk_w - w);
			wind_set(lib_h, WF_CURRXYWH, x, y, w, h);
		}
	}
	apply_layout();
	redraw_all();
}

void libwin_bookmarks(const ehv_bookmarks *bm)
{
	lib_bm = bm;
	if (!lib_ready)
		return;
	lib_set_bookmarks(&lib, bm);
	refresh(R_PANE | R_LIST | R_DETAIL);
}

void libwin_index_changed(void)
{
	if (!lib_ready)
		return;
	if (lib.idx != idx)
	{
		index_of = NULL;
		lib_set_index(&lib, idx);
		refresh(R_PANE | R_LIST | R_DETAIL);
	} else
	{
		update_info();
	}
}

/* a heading folded or unfolded (how: lib_fold's) */
static void fold(int how)
{
	int r = lib_fold(&lib, how);

	if (r <= 0)
		return;
	if (lib.sel >= 0 && lib.rows[lib.sel].kind == LIB_GROUP)
		log_event("fold", lib.rows[lib.sel].text, (lib.rows[lib.sel].flags & LIB_FOLDED) ? "folded" : "open");
	refresh(R_LIST);
}

/* the Index page of the hypertext chosen (Two panes) */
static void open_index(void)
{
	describe();
	if (!has_index(about_guide))
		return;
	log_event("library-index", about_guide->key, NULL);
	graf_mouse(BUSYBEE, NULL);
	viewer_show(about_guide->path, "Index", 0);
	graf_mouse(ARROW, NULL);
}

static void activate(void)
{
	const char *path, *node;
	unsigned short line;

	if (lib.sel >= 0 && (unsigned long) lib.sel < lib.nrows && lib.rows[lib.sel].kind == LIB_GROUP)
	{
		fold(-1);
		return;
	}
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
			refresh(R_LIST | R_DETAIL);
		}
		break;
	}
}

/* the search's EHV line due: 1 the names, 2 the full text (logged when
   what it found is drawn, libwin_idle) */
static int log_due;

/* a key typed answered at once: the text drawn now, the search and its
   rows in the next timer turns (libwin_idle) */
static void set_query(const char *q)
{
	int had = lib.query[0] != '\0';

	lib_set_query_later(&lib, q);
	if (q[0] != '\0')
		focus_left = 0;                /* the rows found have the keys */
	refresh_head();
	/* the pane's line chosen is shown otherwise while searching */
	rows_due |= R_LIST | R_DETAIL | (had != (q[0] != '\0') ? R_PANE : 0);
}

/* the rows of the text typed made now: for a key or a click that acts
   on them */
static void rows_now(void)
{
	if (!lib.query_stale)
		return;
	lib_rows_update(&lib);
	if (!lib.scanning)
		log_due = 1;
	rows_due |= R_LIST | R_DETAIL;
}

/* Two panes: the Subjects pane takes the keys, or gives them back */
static void focus(int left)
{
	if (!panes() || left == focus_left || (left && lib.query[0] != '\0'))
		return;
	focus_left = left;
	log_event("library-focus", left ? "subjects" : "rows", NULL);
	refresh_pane_line(lib.pane);
	refresh_row(lib.sel);
}

static void pane_log(long before)
{
	char n[16];

	if (lib.pane == before || lib.pane < 0)
		return;
	sprintf(n, "%lu", lib.panes[lib.pane].count);
	log_event("pane", lib.panes[lib.pane].text, n);
}

/* another line of the Subjects chosen: its two lines (all of them when
   the pane scrolled, or top_before is -1), and the rows */
static void pane_moved(long before, long top_before)
{
	if (lib.pane == before)
		return;
	pane_log(before);
	pane_clamp();
	if (pane_top == top_before)
	{
		refresh_pane_line(before);
		refresh_pane_line(lib.pane);
	} else
		refresh(R_PANE);
	refresh(R_LIST | R_DETAIL);
}

/* the bar: the Subjects scrolled to t first (the line chosen stays as
   it is), the lines and the bar's slider drawn again; 0 when they were
   there already */
static int pane_goto(long t)
{
	long old = pane_top, m = pane_max_top();
	short wx, wy, ww, wh, x, y, w, h;
	char n[12];

	if (t > m)
		t = m;
	if (t < 0)
		t = 0;
	if (t == old)
		return 0;
	pane_top = t;
	work_area(&wx, &wy, &ww, &wh);
	redraw(wx, (short) (wy + header_h()), pane_text_w(), (short) (wh - header_h()));
	bar_place();
	libbar_moved(old, &x, &y, &w, &h);
	redraw(x, y, w, h);
	sprintf(n, "%ld", t);
	log_event("pane-top", n, NULL);
	return 1;
}

static int pane_scroll(long n)
{
	return pane_goto(pane_top + n);
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
	long rows = visible_rows(), old_sel = lib.sel, old_top = lib.top;
	char q[LIB_QUERY_MAX];
	size_t n = strlen(lib.query);

	if (ctrl || ascii < 32 || ascii == 127)
	{
		if (scan != 0x0e)
		{
			rows_now();                    /* a key for the rows, not the text */
			old_sel = lib.sel;
			old_top = lib.top;
		}
	}
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
	if (panes() && focus_left)
	{
		long before = lib.pane, top_before = pane_top;

		switch (scan)
		{
		case 0x48:                         /* Up, Down: the Subjects */
			lib_pane_move(&lib, -1);
			pane_moved(before, top_before);
			return;
		case 0x50:
			lib_pane_move(&lib, 1);
			pane_moved(before, top_before);
			return;
		case 0x47:                         /* Clr/Home */
			lib_pane_select(&lib, shift ? (long) lib.npanes : 0);
			pane_moved(before, top_before);
			return;
		case 0x1c:                         /* Return, Right, Tab: to the rows */
		case 0x72:
		case 0x4d:
		case 0x0f:
			focus(0);
			return;
		}
	}
	switch (scan)
	{
	case 0x48:                             /* Up */
		lib_move(&lib, shift ? -rows : -1);
		sel_moved(old_sel, old_top);
		return;
	case 0x50:                             /* Down */
		lib_move(&lib, shift ? rows : 1);
		sel_moved(old_sel, old_top);
		return;
	case 0x47:                             /* Clr/Home */
		lib_move(&lib, shift ? (long) lib.nrows : -(long) lib.nrows);
		sel_moved(old_sel, old_top);
		return;
	case 0x4b:                             /* Left: the Subjects; in the Outline, fold */
		if (panes())
			focus(1);
		else
			fold(0);
		return;
	case 0x4d:                             /* Right: unfold */
		if (!panes())
			fold(1);
		return;
	case 0x0f:                             /* Tab: the Subjects */
		focus(1);
		return;
	case 0x1c:                             /* Return */
	case 0x72:                             /* Enter */
		activate();
		return;
	case 0x62:                             /* Help: this window's page of EHypView's guide */
		app_guide("The library");
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
	short x, y, w, h, lx, ly, lw, lh;
	long row;

	if (lib_icon)
		return;
	rows_now();
	work_area(&x, &y, &w, &h);
	if (mx < x || mx >= x + w || my < y + header_h() || my >= y + h)
		return;
	if (strip_h() > 0 && my < y + header_h() + strip_h())
	{
		/* a Continue button */
		long i;

		chips_layout(x, w);
		for (i = 0; i < nchips_shown; i++)
			if (mx >= chip_x[i] && mx < chip_x[i] + chip_w[i])
			{
				long os = lib.sel, ot = lib.top;

				lib_select(&lib, i);
				sel_moved(os, ot);
				activate();
			}
		return;
	}
	if (panes() && mx < x + pane_w() + 4)
	{
		/* a line of the Subjects pane, or its bar */
		long i, before = lib.pane, top_before = pane_top;

		if (mx >= x + pane_w())
			return;
		bar_place();
		if (libbar_hit(mx, my))
		{
			libbar_click(mx, my, pane_scroll, pane_goto);
			return;
		}
		for (i = pane_top; i < (long) lib.npanes && i <= pane_last(); i++)
			if (my >= pane_y(i) && my < pane_y(i) + pane_line_h())
				break;
		if (i >= (long) lib.npanes || i > pane_last())
			return;
		if (lib.query[0] != '\0')
		{
			/* the pane's own rows again: the window drawn once */
			lib_set_query(&lib, "");
			log_search();
			lib_pane_select(&lib, i);
			focus_left = 1;
			log_event("library-focus", "subjects", NULL);
			pane_log(before);
			redraw_all();
			return;
		}
		lib_pane_select(&lib, i);
		focus_left = 1;                    /* the Subjects take the keys */
		log_event("library-focus", "subjects", NULL);
		if (lib.pane != before)
			pane_moved(before, top_before);
		else
		{
			refresh_pane_line(lib.pane);
			refresh_row(lib.sel);
		}
		return;
	}
	if (panes() && my >= y + h - detail_h())
	{
		/* Open, Index */
		short bx, oy, iy, bw, bh;

		detail_buttons(&bx, &oy, &iy, &bw, &bh);
		if (mx < bx || mx >= bx + bw)
			return;
		if (my >= oy && my < oy + bh && can_open())
			activate();
		else if (my >= iy && my < iy + bh)
			open_index();
		return;
	}
	list_area(&lx, &ly, &lw, &lh);
	if (my < ly || my >= ly + lh)
		return;
	for (row = lib.top, h = ly; (unsigned long) row < lib.nrows; row++)
	{
		if (my < h + row_h(row))
			break;
		h = (short) (h + row_h(row));
	}
	{
		long os = lib.sel, ot = lib.top;

		if (lib_select(&lib, row) != row)
			return;
		if (panes() && focus_left)
		{
			focus_left = 0;            /* the rows take the keys */
			refresh_pane_line(lib.pane);
		}
		sel_moved(os, ot);
		if (os == lib.sel)
			refresh_row(lib.sel);
	}
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
			apply_layout();
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
		wind_set(lib_h, WF_CURRXYWH, wind_x8(LKIND, msg[4], msg[5], w, h), msg[5], w, h);
		apply_layout();
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
			if (lib.top < first_row())
				lib.top = first_row();
			/* scrolling moves the view, not the selection */
			{
				long sel = lib.sel;

				lib.sel = -1;
				clamp();
				lib.sel = sel;
			}
			set_slider();
			redraw_list();
		}
		break;
	case WM_VSLID:
		{
			long first = first_row(), max = max_top() - first, sel = lib.sel;

			lib.top = first + max * msg[4] / 1000;
			lib.sel = -1;
			clamp();
			lib.sel = sel;
			set_slider();
			redraw_list();
		}
		break;
	default:
		return 0;
	}
	return 1;
}

int libwin_busy(void)
{
	return lib_ready && (rows_due || lib.query_stale || lib.rows_stale || lib.scanning ||
			     (lib.fts != NULL && !ehv_fts_done(lib.fts)));
}

/* what the searches found drawn, at most this often while they go on
   (drawing it takes longer than finding it), and in a turn of its own:
   the next timer turn's, before any more work */
#define FOUND_DRAWN_EVERY 250
#define R_INFO 16                      /* the info line alone (rows_due) */

/* while the slice has room for another piece of the full-text search:
   up to 45 ms on an ST (a block's tables, then 1 KB unpacked), 10 on a
   68030; of the names search: a name, or 4 KB of them read */
static int fast_cpu(void)
{
	static int cpu = -1;

	if (cpu < 0)
		cpu = get_cookie(0x5F435055L) >= 20;	/* '_CPU' */
	return cpu;
}

static int piece_room(void)
{
	return bg_room(fast_cpu() ? 10 : 40);
}

static int name_room(void)
{
	return bg_room(fast_cpu() ? 5 : 20);
}

int libwin_idle(void)
{
	static unsigned long drawn;
	static int waiting;           /* found, not drawn yet */
	unsigned long before, after;
	int r;

	if (lib_ready && lib.query_stale)
	{
		/* the text typed: the search started, its rows made; drawn in
		   the next turn */
		aes_profile_tag("lib-model");
		rows_now();
		return libwin_busy() | 2;
	}
	if (lib_ready && lib.rows_stale && rows_due)
	{
		/* the names found made rows, when they are to be drawn: a turn
		   of its own, the drawing in the next */
		aes_profile_tag("lib-model");
		lib_rows_update(&lib);
		return libwin_busy() | 2;
	}
	if (lib_ready && rows_due)
	{
		int what = rows_due;

		aes_profile_tag("lib-rows");      /* for the AESGAP lines */
		rows_due = 0;
		if (what == R_INFO)
			update_info();
		else
			refresh(what & ~R_INFO);
		drawn = tos_ms();
		if (log_due == 1)
			log_search();
		else if (log_due == 2 && lib.fts != NULL)
		{
			char q[LIB_QUERY_MAX + 2], num[16];

			ehv_fts_hits(lib.fts, &after);
			snprintf(q, sizeof(q), "\"%s\"", lib.query);
			sprintf(num, "%lu", after);
			log_event("fulltext", q, num);
		}
		log_due = 0;
		return libwin_busy() | 2;
	}
	if (lib_ready && lib.scanning)
	{
		/* on for the slice, the index read with one open */
		r = lib_search_step(&lib, (unsigned int) -1, name_room);
		waiting |= r == 2;
		if (r != 0 && r != 2)
		{
			waiting = 0;
			rows_due |= R_LIST | R_DETAIL;
			log_due = 1;
			return 3;
		}
		if (waiting && tos_ms() - drawn >= FOUND_DRAWN_EVERY)
		{
			waiting = 0;
			rows_due |= R_LIST | R_DETAIL;
		}
		return 3;                         /* it had the slice */
	}
	if (!lib_ready || lib.fts == NULL || ehv_fts_done(lib.fts))
		return 0;
	ehv_fts_hits(lib.fts, &before);
	/* on for the slice (bgwork.h), a piece at a time (1 KB of a page,
	   or 16 of its lines), its hypertext opened once for them */
	r = lib_fulltext_step(&lib, (unsigned int) -1, piece_room);
	ehv_fts_hits(lib.fts, &after);
	waiting |= after != before;
	if (r != 0)
	{
		waiting = 0;
		rows_due |= R_LIST | R_DETAIL;
		log_due = 2;
		return 3;
	}
	if (tos_ms() - drawn >= FOUND_DRAWN_EVERY)
	{
		/* the rows found so far, else the pages counted in the info line */
		rows_due |= waiting ? R_LIST | R_DETAIL : R_INFO;
		waiting = 0;
	}
	return 3;                             /* it had the slice: no second call to open its file again */
}
