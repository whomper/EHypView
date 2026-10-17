/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ASKHELP.PRG - asks EHypView for a page as the AERA programs' Help keys
 * do (common/ui/help.c): VA_START with the full path of a hypertext and
 * a page.  tests/run_quiet.py starts it after a boot with the accessory:
 * the page must come at once, before the accessory has read its index
 * (a full path needs none).  It says on the emulator's console (NatFeats)
 * when it asked.  tests/run_colour.py puts another request in
 * C:\ASKHELP.TXT (a line: path and page), for the accessory in modes the
 * program won't run in.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"

#define VA_START 0x4711

static char request[128] = "C:\\GUIDES\\G000.HYP Main";

int main(void)
{
	short msg[8], id, me = appl_init();
	long fh = Fopen("C:\\ASKHELP.TXT", 0);

	if (fh >= 0)
	{
		long n = Fread((short) fh, sizeof(request) - 1, request);
		int i;

		request[n > 0 ? n : 0] = '\0';
		for (i = 0; request[i] != '\0'; i++)
			if (request[i] == '\r' || request[i] == '\n')
				request[i] = '\0';
		Fclose((short) fh);
	}
	evnt_timer_(1000);
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
	evnt_timer_(30000);                 /* the request stays readable */
	appl_exit();
	return 0;
}
