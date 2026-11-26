/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * HELDFILL.PRG - a program in front that uses all the memory it is given:
 * it takes the largest free block, fills it with a pattern and ends,
 * naming the next program for the desktop to start (shel_write), as a user
 * starts one after another.  On plain TOS an accessory's memory must lie
 * outside it (guidelines 2.2: common's acc_core), the first program's
 * ending included.  Built twice (Makefile): HELDFAST.PRG with the
 * fast-load bit, started at boot, then HELDSLOW.PRG without it, whose
 * memory TOS clears as it loads; HELDRUN.PRG comes last.  It says on the
 * emulator's console (NatFeats) what it filled.
 */
#include "tos.h"
#include "gem.h"

#ifndef NEXT
#define NEXT "C:\\HELDRUN.PRG"
#endif

long nf_on = 1;                 /* a test program: NatFeats looked for (tos.h) */

static void say(const char *what, unsigned long v)
{
	char line[64], t[12];
	int n = 0, o = 0;

	while (*what)
		line[o++] = *what++;
	do t[n++] = (char) ('0' + v % 10); while ((v /= 10) != 0);
	while (n)
		line[o++] = t[--n];
	line[o++] = '\n';
	line[o] = '\0';
	nf_debug(line);
}

int main(void)
{
	short me = appl_init();
	long n;
	unsigned long *p, *e;

	/* events for a while first, as any program does: on TOS the
	   accessories start (and take their memory) only then, while this
	   one is the current program (the fault of 26 Nov 2026: their blocks
	   went back to TOS with it) */
	if (me >= 0)
		evnt_timer_(3000);
	n = (long) Malloc(-1L);

	if (n < 1024 || (long) (p = Malloc(n)) <= 0)
		nf_debug("HELDFILL none\n");
	else
	{
		for (e = p + n / 4; p < e;)
			*p++ = 0xA5A5A5A5UL;
		Mfree((void *) (e - n / 4));
		say("HELDFILL filled ", (unsigned long) n);
	}
	if (me >= 0)
	{
		shel_write(1, 1, 0, NEXT, "\0");       /* started by the desktop when this one ends */
		appl_exit();
	}
	return 0;
}
