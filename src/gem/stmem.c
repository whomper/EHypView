/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - ST RAM for the pictures it copies to the screen (stmem.h).
 *
 * A few areas (GEMDOS blocks), each a row of blocks with a header: the
 * block's size and whether it is in use.  st_alloc() takes the first free
 * block that fits, joining free neighbours on the way, and splits it;
 * st_free() marks a block free.  Pictures are few and come and go with
 * pages, so a walk is quick enough.
 *
 * Where GEMDOS has no block left (a 1 MB machine, the program's heap
 * having taken the rest), the heap's own memory does when it is ST RAM,
 * as it all is without TT or Fast RAM.
 */
#include <stddef.h>
#include <stdlib.h>
#include "stmem.h"

#ifdef EHV_TOS
#include "tos.h"
#endif

#define MAX_AREAS 6                    /* GEMDOS blocks: TOS's pool is small */
#define FIRST     (128L * 1024)        /* the program's first area */
#define HDR       ((long) sizeof(blk))
#define ALIGN     ((long) sizeof(long))   /* 4 on the Atari; a header's longs elsewhere */

typedef struct {
	long size;                         /* the whole block, header too */
	long used;
} blk;

typedef struct {
	char *base;
	long size;
} area;

static area areas[MAX_AREAS];
static int nareas, fixed;
static long used, taken;

int st_ram_addr(const void *p)
{
	return (unsigned long) p < 0x01000000UL;
}

/* a block of ST RAM from GEMDOS, or NULL */
static void *take(long n)
{
#ifdef EHV_TOS
	void *p = Mxalloc(n, 0);           /* ST RAM only */

	if ((long) p == -32)               /* TOS 1.x: no Mxalloc, and no other RAM */
		p = Malloc(n);
	return (long) p > 0 ? p : NULL;
#else
	return malloc((size_t) n);
#endif
}

static int add_area(long n)
{
	char *p;

	n &= ~(ALIGN - 1);
	if (nareas >= MAX_AREAS || n < 2 * HDR || (p = take(n)) == NULL)
		return 0;
	areas[nareas].base = p;
	areas[nareas].size = n;
	nareas++;
	((blk *) p)->size = n;
	((blk *) p)->used = 0;
	taken += n;
	return 1;
}

void stmem_reserve(long bytes)
{
	if (nareas == 0)
		add_area(bytes);
	fixed = 1;
}

/* a free block of need bytes in area a, split off; NULL if none */
static void *fit(area *a, long need)
{
	char *p = a->base, *end = a->base + a->size;

	while (p < end)
	{
		blk *b = (blk *) p;

		if (!b->used)
		{
			/* join the free blocks after it */
			while (p + b->size < end && !((blk *) (p + b->size))->used)
				b->size += ((blk *) (p + b->size))->size;
			if (b->size >= need)
			{
				if (b->size - need >= 2 * HDR)
				{
					blk *rest = (blk *) (p + need);

					rest->size = b->size - need;
					rest->used = 0;
					b->size = need;
				}
				b->used = 1;
				used += b->size;
				return p + HDR;
			}
		}
		p += b->size;
	}
	return NULL;
}

/* n bytes from the heap, if they are ST RAM */
static void *from_heap(long n)
{
	void *p = malloc((size_t) n);

	if (p != NULL && !st_ram_addr(p))
	{
		free(p);
		p = NULL;
	}
	return p;
}

/* the area p is in, or NULL: from the heap */
static area *area_of(const void *p)
{
	int i;

	for (i = 0; i < nareas; i++)
		if ((const char *) p > areas[i].base && (const char *) p < areas[i].base + areas[i].size)
			return &areas[i];
	return NULL;
}

void *st_alloc(long n)
{
	long need, grow;
	int i;
	void *p;

	if (n <= 0)
		return NULL;
	need = HDR + ((n + ALIGN - 1) & ~(ALIGN - 1));
	for (i = 0; i < nareas; i++)
		if ((p = fit(&areas[i], need)) != NULL)
			return p;
	if (fixed)
		return from_heap(n);           /* the accessory: its own heap */
	/* a new area: as big as all before it, at least the first size */
	grow = taken > FIRST ? taken : FIRST;
	if (grow < need)
		grow = need;
	if (!add_area(grow) && !add_area(need))
		return from_heap(n);
	return fit(&areas[nareas - 1], need);
}

void st_free(void *p)
{
	blk *b;

	if (p == NULL)
		return;
	if (area_of(p) == NULL)
	{
		free(p);                       /* from_heap() */
		return;
	}
	b = (blk *) ((char *) p - HDR);
	if (b->used)
	{
		b->used = 0;
		used -= b->size;
	}
}

long stmem_used(void)
{
	return used;
}

long stmem_taken(void)
{
	return taken;
}
