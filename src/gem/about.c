/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * about.c - EHypView's About box: the AERA programs' shared one
 * (common/ui/aboutbox.c, drawn through gemui.c), with how EHypView
 * stands (the hypertexts it knows, the font, TT RAM).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "tos.h"
#include "gem.h"
#include "gemapp.h"
#include "aboutbox.h"
#include "grect.h"
#include "iconify.h"
#include "icon.h"
#include "icon16.h"
#include "version.h"

static void status_line(char *buf, size_t size)
{
	char fontdesc[48];
	const char *ram = about_ram();
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
	snprintf(buf + n, size - n, "  \xF9  %s%s%s", fontdesc, ram[0] ? "  \xF9  " : "", ram);
}

void about_box(void)
{
	static char status[100];
	ABOUT a = { "EHypView", app_version, "A hypertext viewer for the Atari TT and Falcon",
		    { status, NULL, NULL }, { 0, 0, 0 }, icon_bits, icon16_px, icon16_rgb };

	status_line(status, sizeof(status));
	log_event("dialog", "about", app_version);
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
