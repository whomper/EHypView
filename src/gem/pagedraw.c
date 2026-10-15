/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - a page laid out and drawn on the character grid of the text
 * font (see pagedraw.h).  Moved here from ehypview.c so that a pop-up
 * draws its page the same way as the viewer window.
 */
#include <stdlib.h>
#include <string.h>
#include "gem.h"
#include "gemapp.h"
#include "pagedraw.h"

/*
 * Convert a HYP image (planar, standard format) for the screen.  Images
 * with as many planes as the screen are shown in colour; anything else
 * becomes black and white through the dither mask (bit n set: pixel
 * value n is black; without a mask, everything but 0 is black).
 */
static void load_image(hyp_doc *doc, const hyp_gfx *g, gimage *im)
{
	unsigned char *data;
	unsigned long size, plane, wdw, p;
	unsigned short w, h, np, present, onoff;
	unsigned char *std;
	MFDB src;
	int i;

	memset(im, 0, sizeof(*im));
	if (hyp_load(doc, g->image, &data, &size) != HYP_OK)
		return;
	if (size < 8)
	{
		free(data);
		return;
	}
	w = (unsigned short) ((data[0] << 8) | data[1]);
	h = (unsigned short) ((data[2] << 8) | data[3]);
	np = data[4];
	present = data[5];
	onoff = data[6];
	wdw = (w + 15UL) / 16;
	plane = wdw * 2 * h;
	if (w == 0 || h == 0 || np == 0 || np > 8)
	{
		free(data);
		return;
	}
	im->w = (short) w;
	im->h = (short) h;
	im->planes = (short) np;

	/* all planes, filling in the ones that are not stored */
	std = malloc(plane * np);
	if (std == NULL)
	{
		free(data);
		return;
	}
	p = 8;
	for (i = 0; i < np; i++)
	{
		unsigned char *dst = std + plane * (unsigned long) i;

		if (present & (1 << i))
		{
			if (p + plane > size)
			{
				free(std);
				free(data);
				return;
			}
			memcpy(dst, data + p, plane);
			p += plane;
		} else
		{
			memset(dst, (onoff & (1 << i)) ? 0xff : 0x00, plane);
		}
	}
	free(data);

	if (np != planes)
	{
		/* black and white */
		unsigned short mask = g->has_dither ? g->dithermask : 0xfffe;
		unsigned char *mono = calloc(1, plane);
		unsigned long b;

		if (mono == NULL)
		{
			free(std);
			return;
		}
		for (b = 0; b < plane; b++)
		{
			int bit;

			for (bit = 0; bit < 8; bit++)
			{
				unsigned short v = 0;

				for (i = 0; i < np; i++)
					if (std[plane * (unsigned long) i + b] & (0x80 >> bit))
						v |= (unsigned short) (1 << i);
				if (mask & (1U << (v & 15)))
					mono[b] |= (unsigned char) (0x80 >> bit);
			}
		}
		free(std);
		std = mono;
		np = 1;
		im->mono = 1;
	} else if (np == 1)
	{
		im->mono = 1;
	}

	src.fd_addr = std;
	src.fd_w = (short) (wdw * 16);
	src.fd_h = (short) h;
	src.fd_wdwidth = (short) wdw;
	src.fd_stand = 1;
	src.fd_nplanes = (short) np;
	src.fd_r1 = src.fd_r2 = src.fd_r3 = 0;
	im->form = src;
	im->form.fd_stand = 0;
	im->form.fd_addr = malloc(plane * np);
	if (im->form.fd_addr == NULL)
	{
		free(std);
		im->w = 0;
		return;
	}
	vr_trnfm(vdi_h, &src, &im->form);
	free(std);
}

void layout_free(playout *L)
{
	unsigned short i;

	for (i = 0; L->images != NULL && i < L->nimages; i++)
		free(L->images[i].form.fd_addr);
	free(L->images);
	free(L->line_y);
	free(L->gfx_y);
	memset(L, 0, sizeof(*L));
}

void layout_make(playout *L, hyp_doc *doc, const hyp_page *pg)
{
	unsigned short i;
	long y = 0;
	long l, n;

	layout_free(L);
	if (pg == NULL)
		return;
	n = pg->nlines + 1;
	L->line_y = malloc((size_t) n * sizeof(long));
	L->gfx_y = malloc((size_t) n * sizeof(long));
	L->images = calloc(pg->ngfx ? pg->ngfx : 1, sizeof(gimage));
	if (L->line_y == NULL || L->gfx_y == NULL || L->images == NULL)
	{
		layout_free(L);
		return;
	}
	L->pg = pg;
	L->nimages = pg->ngfx;
	for (i = 0; i < pg->ngfx; i++)
		if (pg->gfx[i].type == HYP_GFX_IMAGE)
			load_image(doc, &pg->gfx[i], &L->images[i]);

	/* an @limage pushes the text of its line down (as ST-Guide does) */
	for (l = 0; l < n; l++)
	{
		L->gfx_y[l] = y;
		for (i = 0; i < pg->ngfx; i++)
		{
			const hyp_gfx *g = &pg->gfx[i];

			if (g->type == HYP_GFX_IMAGE && g->limage && (g->y == l || (l == n - 1 && g->y >= l)) &&
				L->images[i].h > 0)
			{
				y += ((L->images[i].h + ch - 1) / ch) * ch;
				if (L->images[i].h % ch == 0)
					y += ch;
			}
		}
		L->line_y[l] = y;
		y += ch;
	}
	L->height = L->line_y[n - 1];
}

/* draw one graphic; oy is the page origin, top/bottom the redrawn rows */
static void draw_gfx(const playout *L, const hyp_gfx *g, const gimage *im, short ox, long oy,
                     long top, long bottom, long cols)
{
	short pxy[8];
	long x = ox + (long) (g->x > 0 ? g->x - 1 : 0) * cw;
	long y = oy + L->gfx_y[g->y < L->pg->nlines ? g->y : L->pg->nlines];
	long h = g->type == HYP_GFX_IMAGE ? (im ? im->h : 0) : (long) g->h * ch;

	/* pages can be taller than screen coordinates reach: skip what is
	   not in the redrawn rows before converting to short (heights are
	   never negative; only a line's width can be) */
	if (y > bottom || y + h < top)
		return;

	vswr_mode(vdi_h, MD_TRANS);
	vsl_color(vdi_h, 1);
	vsf_color(vdi_h, 1);
	switch (g->type)
	{
	case HYP_GFX_IMAGE:
		if (im == NULL || im->w <= 0)
			return;
		if (g->x == 0)
		{
			/* centred on the page */
			long tx = (cols * cw - im->w) / 2;

			x = ox + (tx > 0 ? tx : 0);
		}
		{
			MFDB screen;

			memset(&screen, 0, sizeof(screen));
			pxy[0] = 0;
			pxy[1] = 0;
			pxy[2] = (short) (im->w - 1);
			pxy[3] = (short) (im->h - 1);
			pxy[4] = (short) x;
			pxy[5] = (short) y;
			pxy[6] = (short) (x + im->w - 1);
			pxy[7] = (short) (y + im->h - 1);
			if (im->mono)
			{
				short colors[2];

				colors[0] = 1;
				colors[1] = 0;
				vrt_cpyfm(vdi_h, MD_REPLACE, pxy, (MFDB *) &im->form, &screen, colors);
			} else
			{
				vro_cpyfm(vdi_h, 3, pxy, (MFDB *) &im->form, &screen);
			}
		}
		break;
	case HYP_GFX_LINE:
		{
			long w = (long) g->w * cw, h = (long) g->h * ch;

			pxy[0] = (short) x;
			pxy[1] = (short) y;
			pxy[2] = (short) (x + w + (w < 0 ? 1 : w > 0 ? -1 : 0));
			pxy[3] = (short) (y + h - (h ? 1 : 0));
			vsl_type(vdi_h, g->style);
			vsl_ends(vdi_h, (g->arrows & HYP_ARROW_START) ? 1 : 0, (g->arrows & HYP_ARROW_END) ? 1 : 0);
			v_pline(vdi_h, 2, pxy);
			vsl_ends(vdi_h, 0, 0);
			vsl_type(vdi_h, 1);
		}
		break;
	case HYP_GFX_BOX:
	case HYP_GFX_RBOX:
		pxy[0] = (short) x;
		pxy[1] = (short) y;
		pxy[2] = (short) (x + (long) g->w * cw - 1);
		pxy[3] = (short) (y + (long) g->h * ch - 1);
		if (g->pattern == 0)
			vsf_interior(vdi_h, FIS_HOLLOW);
		else if (g->pattern > 36)
			vsf_interior(vdi_h, FIS_SOLID);
		else if (g->pattern > 24)
		{
			vsf_interior(vdi_h, FIS_HATCH);
			vsf_style(vdi_h, (short) (g->pattern - 24));
		} else
		{
			vsf_interior(vdi_h, FIS_PATTERN);
			vsf_style(vdi_h, g->pattern);
		}
		vsf_perimeter(vdi_h, 1);
		if (g->type == HYP_GFX_BOX)
			v_bar(vdi_h, pxy);
		else if (g->pattern != 0)
			v_rfbox(vdi_h, pxy);
		else
			v_rbox(vdi_h, pxy);
		vsf_perimeter(vdi_h, 0);
		break;
	}
}

void layout_draw(const playout *L, short ox, long oy, long top,
                 short cx, short cy, short cw_, short ch_, long sel, long cols)
{
	const hyp_page *pg = L->pg;
	long l;

	if (pg == NULL)
		return;
	/* graphics first, text on top (all drawn transparently) */
	{
		unsigned short i;

		for (i = 0; i < pg->ngfx; i++)
			draw_gfx(L, &pg->gfx[i], &L->images[i], ox, oy, cy, (long) cy + ch_, cols);
	}

	vswr_mode(vdi_h, MD_TRANS);
	for (l = top; l < pg->nlines; l++)
	{
		const hyp_line *ln = &pg->lines[l];
		long y = oy + L->line_y[l];
		long x = ox;
		unsigned short r;

		if (y >= cy + ch_)
			break;
		if (y + ch <= cy)
			continue;
		for (r = 0; r < ln->nruns; r++)
		{
			const hyp_run *run = &pg->runs[ln->first_run + r];
			long w = (long) run->len * cw;
			short fx = run->attr & 0x3f;

			if (x + w > cx && x < cx + cw_)
			{
				if ((long) (ln->first_run + r) == sel)
				{
					/* the selected link: reverse video */
					fill(link_color(), (short) x, (short) y, (short) (x + w - 1), (short) (y + ch - 1));
					vswr_mode(vdi_h, MD_TRANS);
					vst_color(vdi_h, 0);
				} else if (run->kind == HYP_RUN_LINK)
				{
					vst_color(vdi_h, link_color());
					fx |= 8 | (ncolors >= 16 ? 0 : 1);   /* underlined (and bold in mono) */
				} else
				{
					vst_color(vdi_h, 1);
				}
				vst_effects(vdi_h, fx);
				text_at((short) x, (short) y, run->text, run->len);
			}
			x += w;
		}
	}
	vst_effects(vdi_h, 0);
	vst_color(vdi_h, 1);
}

long layout_line_at(const playout *L, long py, long top)
{
	long line;

	if (L->pg == NULL || L->line_y == NULL)
		return -1;
	for (line = top < 0 ? 0 : top; line < L->pg->nlines; line++)
		if (py >= L->line_y[line] && py < L->line_y[line] + ch)
			return line;
	return -1;
}

long layout_width(const playout *L, long cols)
{
	long w = cols * cw;
	unsigned short i;

	for (i = 0; L->pg != NULL && i < L->pg->ngfx; i++)
	{
		const hyp_gfx *g = &L->pg->gfx[i];

		if (g->type == HYP_GFX_IMAGE && L->images[i].w > 0)
		{
			long right = (g->x > 0 ? (long) (g->x - 1) * cw : 0) + L->images[i].w;

			if (right > w)
				w = right;
		}
	}
	return w;
}

long layout_height(const playout *L)
{
	long h = L->height;
	unsigned short i;

	for (i = 0; L->pg != NULL && i < L->pg->ngfx; i++)
	{
		const hyp_gfx *g = &L->pg->gfx[i];
		long y = L->gfx_y[g->y < L->pg->nlines ? g->y : L->pg->nlines];
		long bottom = y + (g->type == HYP_GFX_IMAGE ? L->images[i].h : (long) (g->h > 0 ? g->h : 0) * ch);

		if (bottom > h)
			h = bottom;
	}
	return h;
}
