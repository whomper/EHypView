/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * about.c - EHypView's About box: the AERA programs' shared one
 * (common/ui/aboutbox.c, drawn through gemui.c), with how EHypView
 * stands: the hypertexts it knows and the font, then the memory.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "tos.h"
#include "gem.h"
#include "gemapp.h"
#include "aboutbox.h"
#include "aboutline.h"
#include "grect.h"
#include "iconify.h"
#include "icon.h"
#include "icon16.h"
#include "icon256.h"
#include "version.h"

/* line 2 of the status (the guidelines, 1.3): what EHypView holds, the
   hypertexts it knows and the font */
static void status_line(char *buf, size_t size)
{
	char fontdesc[48];
	size_t n;

	if (strcmp(font_name(), "System font") == 0)
		snprintf(fontdesc, sizeof(fontdesc), "System font %s", font_point() >= 16 ? "8x16" : "8x8");
	else
		snprintf(fontdesc, sizeof(fontdesc), "%s %d pt", font_name(), font_point());
	if (scanning())
		snprintf(buf, size, "%s", index_busy());
	else if (idx != NULL)
		snprintf(buf, size, "%lu hypertext%s", idx->nguides, idx->nguides == 1 ? "" : "s");
	else
		snprintf(buf, size, "No index");
	n = strlen(buf);
	snprintf(buf + n, size - n, "  \xF9  %s", fontdesc);
}

/* the status in the family's lines (the guidelines, 1.3): no connection
   (EHypView has no network), its own, then the memory (common/ui/
   aboutline.c) */
void about_box(void)
{
	static char status[100], mem[100];
	ABOUT a = { "EHypView", app_version, "Hypertexts for the Atari ST, TT and Falcon",
		    { status, mem, NULL }, { 0, 0, 0 }, icon_bits, icon16_px, icon16_rgb,
		    icon256_px, icon256_rgb };

	status_line(status, sizeof(status));
	about_mem_line(mem, (int) sizeof(mem));
	if (mem[0] == '\0')
		a.status[1] = NULL;
	log_event("dialog", "about", app_version);
	log_event("about-lines", status, mem);
	about_show(&a);
}

/* an iconified window: the icon (common/ui/iconify.c), nothing under it */
void app_icon_redraw(short h, short x, short y, short w, short ht)
{
	GRECT r;

	r.x = x;
	r.y = y;
	r.w = w;
	r.h = ht;
	icon_redraw(h, w > 0 ? &r : NULL, icon_bits, icon16_px, "");
}
