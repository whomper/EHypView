/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * HELDRUN.PRG - the last of the programs the desktop starts one after
 * another beside the accessory: HELDFAST.PRG (started at boot, Hatari's
 * --auto), then HELDSLOW.PRG (no fast-load bit: TOS clears its memory),
 * each filling all it is given and naming the next (tests/heldfill.c);
 * then this one: ACOPEN.PRG opens the accessory (its library) and
 * ASKHELP.PRG asks it for a page (a viewer).
 * tests/hatari/small_acc.py --held checks that the accessory, its memory
 * never the programs', still shows both (guidelines 2.2).
 */
#include "tos.h"

long nf_on = 1;                 /* a test program: NatFeats looked for (tos.h) */

static void run(const char *path)
{
	long r = trap1_wwlll(0x4b, 0, (long) path, (long) "", 0L);   /* Pexec: load and go */

	nf_debug(r < 0 ? "HELDRUN failed\n" : "HELDRUN ran\n");
}

int main(void)
{
	run("C:\\ACOPEN.PRG");
	run("C:\\ASKHELP.PRG");
	nf_debug("HELDRUN done\n");
	for (;;)
		Cconws("");
	return 0;
}
