/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - memory for the pictures it copies to the screen.
 *
 * The program may get TT or Fast RAM from malloc (it is built to load
 * and allocate there), and a blitter can't read that RAM: on a Falcon
 * with a CT60 and TOS's VDI, pictures copied from it come out as
 * garbage.  So converted pictures live in ST RAM, in a few large GEMDOS
 * blocks shared out here (one block per picture would use up TOS's
 * small internal pool).
 *
 * The program takes ST RAM as it needs it.  An accessory can't (memory
 * it takes once it runs belongs to whichever program is in front), so it
 * reserves its block at start with stmem_reserve(), and that is all it
 * gets.  No AES or VDI here: tests/test_stmem.c checks it on the host.
 */
#ifndef EHV_STMEM_H
#define EHV_STMEM_H

/* the accessory, at start: an ST-RAM block of about `bytes`, its only one */
void stmem_reserve(long bytes);

/* n bytes of ST RAM, word-aligned; NULL when there are none */
void *st_alloc(long n);
void st_free(void *p);

/* the bytes in use, and the bytes taken from GEMDOS (tests) */
long stmem_used(void);
long stmem_taken(void);

/* nonzero when p is in ST RAM: below the ST's 16 MB */
int st_ram_addr(const void *p);

#endif
