/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView tests for the pictures' ST RAM (src/gem/stmem.c), on the host
 * with malloc standing in for GEMDOS: blocks don't overlap and keep their
 * bytes, freed ones are joined and used again, the program's areas grow
 * a few at a time, and the accessory's one block is all it gets.
 *
 *   test_stmem
 */
#include <stdio.h>
#include <string.h>
#include "stmem.h"

static int failures, checks;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define N 64

static unsigned long seed = 12345;

static unsigned long rnd(void)
{
	seed = seed * 1103515245UL + 12345UL;
	return (seed >> 16) & 0x7fff;
}

int main(void)
{
	char *p[N];
	long size[N];
	int i, round, ok;
	long taken;

	/* the program: areas taken as needed */
	CHECK(st_alloc(0) == NULL && st_alloc(-5) == NULL);
	CHECK(stmem_used() == 0 && stmem_taken() == 0);
	p[0] = st_alloc(1);
	CHECK(p[0] != NULL && ((unsigned long) p[0] & 3) == 0);
	CHECK(stmem_taken() == 128L * 1024);                   /* the first area */
	st_free(p[0]);
	st_free(p[0]);                                         /* twice: no harm */
	st_free(NULL);
	CHECK(stmem_used() == 0);

	/* many blocks of many sizes, each filled with its own byte; freed
	   and taken again in random order, every byte checked */
	memset(p, 0, sizeof(p));
	ok = 1;
	for (round = 0; round < 4000; round++)
	{
		i = (int) (rnd() % N);
		if (p[i] != NULL)
		{
			long k;

			for (k = 0; k < size[i]; k++)
				if (p[i][k] != (char) i)
					ok = 0;
			st_free(p[i]);
			p[i] = NULL;
		} else
		{
			size[i] = (long) (rnd() % 9000) + 1;
			p[i] = st_alloc(size[i]);
			if (p[i] != NULL)
				memset(p[i], i, (size_t) size[i]);
		}
	}
	CHECK(ok);
	for (i = 0; i < N; i++)
		if (p[i] != NULL)
		{
			int j;

			/* no two blocks overlap */
			for (j = 0; j < N; j++)
				if (j != i && p[j] != NULL)
					CHECK(p[i] + size[i] <= p[j] || p[j] + size[j] <= p[i]);
		}
	taken = stmem_taken();
	CHECK(taken <= 512L * 1024);                           /* a few areas, not one per picture */
	for (i = 0; i < N; i++)
		st_free(p[i]);
	CHECK(stmem_used() == 0);

	/* everything freed is joined: one big block fits again, no new area */
	p[0] = st_alloc(100L * 1024);
	CHECK(p[0] != NULL && stmem_taken() == taken);
	st_free(p[0]);

	/* bigger than any area: one made for it */
	p[0] = st_alloc(600L * 1024);
	CHECK(p[0] != NULL && stmem_taken() >= taken + 600L * 1024);
	st_free(p[0]);

	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
