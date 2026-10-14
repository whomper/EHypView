/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * about.c - the About box, drawn as EMail's, ECal's, EContact's and
 * Claude ST's are: the icon, large, in colour when the screen has 16
 * colours or more, the name and version, who made it and how EHypView
 * stands (the hypertexts it knows, the font, TT RAM).
 *
 * Adapted from ECal's ui/about.c (github.com/whomper/ECal).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "tos.h"
#include "gem.h"
#include "gemapp.h"
#include "icon.h"
#include "icon16.h"
#include "version.h"

static short dx, dy, dw, dh;

static void line(short x1, short y1, short x2, short y2)
{
	short pxy[4];

	pxy[0] = x1;
	pxy[1] = y1;
	pxy[2] = x2;
	pxy[3] = y2;
	vswr_mode(vdi_h, MD_REPLACE);
	vsl_color(vdi_h, 1);
	vsl_type(vdi_h, 1);
	v_pline(vdi_h, 2, pxy);
}

static void rect_fill(short x1, short y1, short x2, short y2, short color)
{
	fill(color, x1, y1, x2, y2);
}

static void box(short x, short y, short w, short h)
{
	rect_fill((short) (x + 3), (short) (y + 3), (short) (x + w + 2), (short) (y + h + 2), 1);  /* shadow */
	rect_fill(x, y, (short) (x + w - 1), (short) (y + h - 1), 0);
	line(x, y, (short) (x + w - 1), y);
	line((short) (x + w - 1), y, (short) (x + w - 1), (short) (y + h - 1));
	line((short) (x + w - 1), (short) (y + h - 1), x, (short) (y + h - 1));
	line(x, (short) (y + h - 1), x, y);
	line((short) (x + 2), (short) (y + 2), (short) (x + w - 3), (short) (y + 2));
	line((short) (x + w - 3), (short) (y + 2), (short) (x + w - 3), (short) (y + h - 3));
	line((short) (x + w - 3), (short) (y + h - 3), (short) (x + 2), (short) (y + h - 3));
	line((short) (x + 2), (short) (y + h - 3), (short) (x + 2), (short) (y + 2));
}

/*
 * The 32x32 icon scaled by sx, sy (2x on square pixels; 2x1 in ST
 * medium, whose pixels are twice as tall as wide).  The blit's source
 * is in ST RAM, which every VDI and blitter can read.
 */
#define BIG_WORDS (64 * 4)
static unsigned short big_fallback[BIG_WORDS];

static unsigned short *st_buffer(void)
{
	static unsigned short *buf;

	if (buf == NULL)
	{
		long p = (long) Mxalloc(sizeof(big_fallback), 0);

		buf = p > 0 ? (unsigned short *) p : big_fallback;
	}
	return buf;
}

static void draw_icon(short x, short y, short sx, short sy)
{
	MFDB src, dst;
	short pxy[8], colors[2], r, c, k, wpl = (short) (2 * sx);
	short w = (short) (ICON_W * sx), h = (short) (ICON_H * sy);
	unsigned short *big = st_buffer();

	memset(big, 0, sizeof(big_fallback));
	for (r = 0; r < ICON_H; r++)
		for (c = 0; c < ICON_W; c++)
		{
			if (!(icon_bits[r * 2 + c / 16] & (0x8000 >> (c & 15))))
				continue;
			for (k = 0; k < sx; k++)
			{
				short bx = (short) (c * sx + k), yy;

				for (yy = 0; yy < sy; yy++)
					big[(r * sy + yy) * wpl + bx / 16] |= (unsigned short) (0x8000 >> (bx & 15));
			}
		}
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = big;
	src.fd_w = w;
	src.fd_h = h;
	src.fd_wdwidth = wpl;
	src.fd_nplanes = 1;
	pxy[0] = 0;
	pxy[1] = 0;
	pxy[2] = (short) (w - 1);
	pxy[3] = (short) (h - 1);
	pxy[4] = x;
	pxy[5] = y;
	pxy[6] = (short) (x + w - 1);
	pxy[7] = (short) (y + h - 1);
	colors[0] = 1;
	colors[1] = 0;
	vrt_cpyfm(vdi_h, 2, pxy, &src, &dst, colors);      /* transparent */
}

/* the 16-colour icon, as runs of VDI fills; pens 8..15 are lent the
 * icon's colours while the box is open */
static void draw_icon16(short x, short y, short sx, short sy)
{
	short r, c;

	for (r = 0; r < ICON_H; r++)
	{
		for (c = 0; c < ICON_W; )
		{
			unsigned char b = icon16_px[r * 16 + c / 2];
			short pen = (short) ((c & 1) ? (b & 15) : (b >> 4)), e = (short) (c + 1);

			while (e < ICON_W)
			{
				unsigned char b2 = icon16_px[r * 16 + e / 2];

				if (((e & 1) ? (b2 & 15) : (b2 >> 4)) != pen)
					break;
				e++;
			}
			if (pen)
				rect_fill((short) (x + c * sx), (short) (y + r * sy),
					(short) (x + e * sx - 1), (short) (y + r * sy + sy - 1), pen);
			c = e;
		}
	}
}

static void center(short y, const char *s, short effects)
{
	short n = (short) strlen(s);

	vswr_mode(vdi_h, MD_TRANS);
	vst_color(vdi_h, 1);
	vst_effects(vdi_h, effects);
	text_at((short) (dx + (dw - n * cw) / 2), y, s, n);
	vst_effects(vdi_h, 0);
}

static void button(short x, short y, short w, short h, const char *label)
{
	short n = (short) strlen(label);

	rect_fill(x, y, (short) (x + w - 1), (short) (y + h - 1), 0);
	line(x, y, (short) (x + w - 1), y);
	line((short) (x + w - 1), y, (short) (x + w - 1), (short) (y + h - 1));
	line((short) (x + w - 1), (short) (y + h - 1), x, (short) (y + h - 1));
	line(x, (short) (y + h - 1), x, y);
	/* the default button's thick GEM border */
	line((short) (x - 1), (short) (y - 1), (short) (x + w), (short) (y - 1));
	line((short) (x + w), (short) (y - 1), (short) (x + w), (short) (y + h));
	line((short) (x + w), (short) (y + h), (short) (x - 1), (short) (y + h));
	line((short) (x - 1), (short) (y + h), (short) (x - 1), (short) (y - 1));
	vswr_mode(vdi_h, MD_TRANS);
	vst_color(vdi_h, 1);
	vst_effects(vdi_h, 1);
	text_at((short) (x + (w - n * cw) / 2), (short) (y + (h - ch) / 2), label, n);
	vst_effects(vdi_h, 0);
}

static void wait_up(void)
{
	short mx, my, mb, ks;

	do
	{
		graf_mkstate(&mx, &my, &mb, &ks);
		if (mb & 3)
			evnt_timer_(10);
	} while (mb & 3);
}

/* "5 hypertexts  -  System font 8x16  -  TT RAM" */
static void status_line(char *buf, size_t size)
{
	char fontdesc[48];
	size_t n;

	if (strcmp(font_name(), "System font") == 0)
		snprintf(fontdesc, sizeof(fontdesc), "System font %s", font_point() >= 16 ? "8x16" : "8x8");
	else
		snprintf(fontdesc, sizeof(fontdesc), "%s %d pt", font_name(), font_point());
	if (scanning())
		snprintf(buf, size, "Indexing: %lu found", scan_found());
	else if (idx != NULL)
		snprintf(buf, size, "%lu hypertext%s", idx->nguides, idx->nguides == 1 ? "" : "s");
	else
		snprintf(buf, size, "No index");
	n = strlen(buf);
	snprintf(buf + n, size - n, "  \xF9  %s", fontdesc);
	{
		/* TT/Fast RAM starts at 16 MB: where TOS loaded the program and
		   where its memory came from */
		void *m = malloc(16);
		int code = (unsigned long) about_box >= 0x01000000UL, heap = (unsigned long) m >= 0x01000000UL;

		free(m);
		if (code || heap)
		{
			n = strlen(buf);
			snprintf(buf + n, size - n, "%s", code && heap ? "  \xF9  TT RAM" : "  \xF9  TT RAM in part");
		}
	}
}

void about_box(void)
{
	char stat[100], v[40];
	short sx, sy, ih, lh;
	short m[8], y, done = 0, i;
	short saved_rgb[8][3], colour = (short) (planes >= 4);
	short bw, bh, by;
	AEVENT e;

	font_sys_push();
	sx = 2;
	sy = (short) (sys_ch >= 16 ? 2 : 1);
	ih = (short) (ICON_H * sy);
	lh = (short) (ch + (ch >= 16 ? 2 : 1));
	bw = (short) (10 * cw);
	bh = (short) (ch + 6);
	status_line(stat, sizeof(stat));

	dw = (short) (50 * cw);
	if ((short) strlen(stat) + 4 > 50)             /* the status line fits whole */
		dw = (short) (((short) strlen(stat) + 4) * cw);
	if (dw > desk_w - 2 * cw)
		dw = (short) (desk_w - 2 * cw);
	dh = (short) (lh + ih + lh / 2 + lh + (lh + lh / 2) + (lh + lh / 2) + lh / 4 + lh +
		(lh + lh / 2) + (lh + lh / 2) + bh + lh);
	if (dh > desk_h - 4)
		dh = (short) (desk_h - 4);
	dx = (short) (desk_x + (desk_w - dw) / 2);
	dy = (short) (desk_y + (desk_h - dh) / 2);
	by = (short) (dy + dh - bh - lh);

	wind_update(BEG_UPDATE);
	wind_update(3);                                /* BEG_MCTRL */
	form_dial(FMD_START, dx, dy, (short) (dw + 3), (short) (dh + 3));
	graf_mouse(M_OFF, NULL);
	{
		static const short none[4] = { 0, 0, 0, 0 };

		vs_clip(vdi_h, 0, none);
	}
	box(dx, dy, dw, dh);

	y = (short) (dy + lh);
	if (colour)
	{
		for (i = 0; i < 8; i++)
		{
			vq_color(vdi_h, (short) (8 + i), saved_rgb[i]);
			vs_color(vdi_h, (short) (8 + i), icon16_rgb[i]);
		}
		draw_icon16((short) (dx + (dw - ICON_W * sx) / 2), y, sx, sy);
	} else
	{
		draw_icon((short) (dx + (dw - ICON_W * sx) / 2), y, sx, sy);
	}
	y = (short) (y + ih + lh / 2);
	center(y, "EHypView", 1);
	{
		/* underlined, like EMail's and Claude ST's names */
		short n = 8, x = (short) (dx + (dw - n * cw) / 2);

		line(x, (short) (y + ch - 1), (short) (x + n * cw - 1), (short) (y + ch - 1));
	}
	y = (short) (y + lh);
	snprintf(v, sizeof(v), "Version %s", app_version);
	center(y, v, 0);
	y = (short) (y + lh + lh / 2);
	center(y, "A hypertext viewer for the Atari TT and Falcon", 0);
	y = (short) (y + lh + lh / 2);
	line((short) (dx + 4 * cw), (short) (y - lh / 4), (short) (dx + dw - 4 * cw), (short) (y - lh / 4));
	y = (short) (y + lh / 4);
	center(y, "Created by Erez Yaary", 1);
	y = (short) (y + lh);
	center(y, "\xBD 2026 Erez Yaary", 0);
	y = (short) (y + lh + lh / 2);
	center(y, stat, 0);
	button((short) (dx + (dw - bw) / 2), by, bw, bh, "OK");
	graf_mouse(M_ON, NULL);
	log_event("dialog", "about", app_version);
	wait_up();

	/* Return, Esc, Undo, Help, Space or a click closes it */
	while (!done)
	{
		evnt_multi_(MU_KEYBD | MU_BUTTON, 0x101, 3, 0, 0, m, &e);
		if (e.which & MU_KEYBD)
		{
			short sc = KEY_SCAN(e.kreturn), as = KEY_ASCII(e.kreturn);

			if (as == 0x0d || as == 0x1b || as == ' ' || sc == 0x61 || sc == 0x62)
				done = 1;
		}
		if (e.which & MU_BUTTON)
		{
			wait_up();
			done = 1;
		}
	}
	if (colour)
		for (i = 0; i < 8; i++)
			vs_color(vdi_h, (short) (8 + i), saved_rgb[i]);
	wind_update(2);                                /* END_MCTRL */
	wind_update(END_UPDATE);
	form_dial(FMD_FINISH, dx, dy, (short) (dw + 3), (short) (dh + 3));
	font_sys_pop();
}
