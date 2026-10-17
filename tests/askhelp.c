/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ASKHELP.PRG - asks EHypView for a page as the AERA programs' Help keys
 * do (common/ui/help.c): VA_START with the full path of a hypertext and
 * a page.  tests/run_quiet.py starts it after a boot with the accessory:
 * the page must come at once, before the accessory has read its index
 * (a full path needs none).  It says on the emulator's console (NatFeats)
 * when it asked.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"

#define VA_START 0x4711

static char request[] = "C:\\GUIDES\\G000.HYP Main";

int main(void)
{
	short msg[8], id, me = appl_init();

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
