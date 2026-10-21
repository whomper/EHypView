/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * LAUNCH.PRG - starts a program with a command line, as a desktop does
 * for a file dropped on it: Hatari's --auto starts a program with none.
 * C:\LAUNCH.TXT holds the program's full path on its first line and the
 * command line on the second; the program starts in its own folder.
 * tests/run_gui.py and tests/run_colour.py start EHYPVIEW.PRG on a page
 * this way (it opens its library when started with nothing).
 */
#include <string.h>
#include "tos.h"

static char text[300];

int main(void)
{
	char tail[128], dir[160], *prog = text, *args = "", *p;
	long fh = Fopen("C:\\LAUNCH.TXT", 0), n, r;
	size_t len;

	if (fh < 0)
	{
		nf_debug("LAUNCH no C:\\LAUNCH.TXT\n");
		return 1;
	}
	n = Fread((short) fh, sizeof(text) - 1, text);
	Fclose((short) fh);
	text[n > 0 ? n : 0] = '\0';
	for (p = text; *p != '\0' && *p != '\r' && *p != '\n'; p++)
		;
	if (*p != '\0')
	{
		*p++ = '\0';
		while (*p == '\r' || *p == '\n')
			p++;
		args = p;
		for (; *p != '\0' && *p != '\r' && *p != '\n'; p++)
			;
		*p = '\0';
	}
	/* its folder: the drive, then the path */
	strncpy(dir, prog, sizeof(dir) - 1);
	dir[sizeof(dir) - 1] = '\0';
	p = strrchr(dir, '\\');
	if (p != NULL && dir[1] == ':')
	{
		p[p == dir + 2 ? 1 : 0] = '\0';
		Dsetdrv((short) ((dir[0] | 0x20) - 'a'));
		Dsetpath(dir + 2);
	}
	len = strlen(args) < sizeof(tail) - 2 ? strlen(args) : sizeof(tail) - 2;
	tail[0] = (char) len;
	memcpy(tail + 1, args, len);
	tail[len + 1] = '\0';
	nf_debug("LAUNCH started\n");
	r = trap1_wwlll(0x4b, 0, (long) prog, (long) tail, 0);     /* Pexec: load and go */
	nf_debug(r < 0 ? "LAUNCH failed\n" : "LAUNCH done\n");
	return 0;
}
