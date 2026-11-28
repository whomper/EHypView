/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ASKHELP.PRG - asks EHypView for a page as the AERA programs' Help keys
 * do (common/ui/help.c): VA_START with the full path of a hypertext and
 * a page.  tests/run_quiet.py starts it after a boot with the accessory:
 * the page must come at once, before the accessory has read its index
 * (a full path needs none).  It says on the emulator's console (NatFeats)
 * when it asked.  tests/run_colour.py puts another request in
 * C:\ASKHELP.TXT (a line: path and page, and maybe a second: the seconds
 * to wait before ending, ten minutes if none), for the accessory in modes
 * the program won't run in.  C:\ASKPENS.TXT, when there, holds pens to set
 * first (a line each: the VDI colour and its red, green and blue, 0-1000),
 * as a program in front may set them while the accessory waits
 * (tests/run_colour.py's front-page case with changed pens).
 */
#include <string.h>
#include "tos.h"
#include "gem.h"

long nf_on = 1;                 /* a test program: NatFeats looked for (tos.h) */

#define VA_START 0x4711

static char request[128] = "C:\\GUIDES\\G000.HYP Main";
static char pens[512];

/* the next number in *p, or -1 */
static int number(char **p)
{
	int v = 0, any = 0;

	while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')
		(*p)++;
	for (; **p >= '0' && **p <= '9'; (*p)++, any = 1)
		v = v * 10 + (**p - '0');
	return any ? v : -1;
}

/* the pens ASKPENS.TXT names, set on the screen */
static void set_pens(void)
{
	long fh = Fopen("C:\\ASKPENS.TXT", 0), n;
	short d, h, work[57];
	char *p = pens;

	if (fh < 0)
		return;
	n = Fread((short) fh, sizeof(pens) - 1, pens);
	Fclose((short) fh);
	pens[n > 0 ? n : 0] = '\0';
	h = graf_handle(&d, &d, &d, &d);
	if ((h = v_opnvwk_(h, work)) <= 0)
		return;
	for (;;)
	{
		int i = number(&p), r = number(&p), g = number(&p), b = number(&p);
		short rgb[3];

		if (i < 0 || r < 0 || g < 0 || b < 0)
			break;
		rgb[0] = (short) r;
		rgb[1] = (short) g;
		rgb[2] = (short) b;
		vs_color(h, (short) i, rgb);
	}
	nf_debug("ASK pens set\n");                /* the workstation stays open: the pens stay */
}

int main(void)
{
	short msg[8], id, me = appl_init();
	long fh = Fopen("C:\\ASKHELP.TXT", 0);
	int wait = 600;                     /* seconds after asking */

	if (fh >= 0)
	{
		long n = Fread((short) fh, sizeof(request) - 1, request);
		int i;

		request[n > 0 ? n : 0] = '\0';
		for (i = 0; request[i] != '\0' && request[i] != '\r' && request[i] != '\n'; i++)
			;
		if (request[i] != '\0')
		{
			/* a second line: the seconds to wait (tests/run_gui.py's accnvdi:
			   end soon, the program in front gone) */
			char *p = &request[i];
			int w = 0, any = 0;

			while (*p == '\r' || *p == '\n')
				p++;
			for (; *p >= '0' && *p <= '9'; p++, any = 1)
				w = w * 10 + (*p - '0');
			if (any)
				wait = w;
			request[i] = '\0';
		}
		Fclose((short) fh);
	}
	evnt_timer_(1000);
	set_pens();
	id = appl_find("EHYPVIEW");
	if (id < 0)
	{
		nf_debug("ASK no viewer\n");
	} else
	{
		memset(msg, 0, sizeof(msg));
		msg[0] = VA_START;
		msg[1] = me;
		msg[3] = (short) ((unsigned long) request >> 16);
		msg[4] = (short) (unsigned long) request;
		appl_write(id, 16, msg);
		nf_debug("ASK sent\n");
	}
	/* the request stays readable, and the accessory's window open (TOS
	   closes it when the program in front ends): ten minutes, unless
	   ASKHELP.TXT says otherwise */
	{
		int i;

		for (i = 0; i < wait; i++)
			evnt_timer_(1000);
	}
	nf_debug("ASK done\n");
	appl_exit();
	return 0;
}
