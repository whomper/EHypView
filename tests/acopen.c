/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ACOPEN.PRG - opens an accessory as its Desk entry does: AC_OPEN to the
 * one named in C:\ACOPEN.TXT (its AES name, e.g. EHYPFB), then waits a few
 * seconds and ends. tests/hatari/firebee_test.py starts it after a boot with
 * an accessory of the other machine's (the guidelines, 7.4): its alert must
 * come at the opening, and the boot must have reached the desktop.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"

long nf_on = 1;                 /* a test program: NatFeats looked for (tos.h) */

#define AC_OPEN 40

int main(void)
{
	short msg[8], id, me = appl_init();
	char name[9] = "EHYPVIEW";
	long fh = Fopen("C:\\ACOPEN.TXT", 0);
	int i;

	if (fh >= 0)
	{
		long n = Fread((short) fh, 8, name);

		name[n > 0 ? n : 0] = '\0';
		for (i = 0; name[i] != '\0' && name[i] != '\r' && name[i] != '\n'; i++)
			;
		name[i] = '\0';
		Fclose((short) fh);
	}
	for (i = (int) strlen(name); i < 8; i++)
		name[i] = ' ';                  /* (appl_find's eight characters) */
	name[8] = '\0';
	evnt_timer_(1000);
	id = appl_find(name);
	if (id < 0)
		nf_debug("ACOPEN none\n");
	else
	{
		memset(msg, 0, sizeof(msg));
		msg[0] = AC_OPEN;
		msg[1] = me;
		appl_write(id, 16, msg);
		nf_debug("ACOPEN sent\n");
	}
	for (i = 0; i < 4; i++)
		evnt_timer_(1000);
	nf_debug("ACOPEN done\n");
	appl_exit();
	return 0;
}
