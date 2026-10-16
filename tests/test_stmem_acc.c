/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView tests for the pictures' ST RAM (src/gem/stmem.c) as the
 * accessory has it: one block reserved at start, and nothing more.
 *
 *   test_stmem_acc
 */
#include <stdio.h>
#include "stmem.h"

static int failures, checks;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

int main(void)
{
	void *a, *b, *c, *d;

	stmem_reserve(32L * 1024);
	CHECK(stmem_taken() == 32L * 1024);
	a = st_alloc(10000);
	b = st_alloc(10000);
	c = st_alloc(10000);
	CHECK(a != NULL && b != NULL && c != NULL);
	CHECK(st_alloc(10000) == NULL);                        /* full: no more GEMDOS blocks */
	CHECK(stmem_taken() == 32L * 1024);
	st_free(b);
	d = st_alloc(9000);                                    /* the gap is used again */
	CHECK(d == b);
	st_free(a);
	st_free(c);
	st_free(d);
	CHECK(stmem_used() == 0);
	CHECK(st_alloc(32L * 1024 - 64) != NULL);              /* all joined into one again */
	CHECK(st_alloc(32L * 1024 * 2) == NULL);
	stmem_reserve(64L * 1024);                             /* only the first counts */
	CHECK(stmem_taken() == 32L * 1024);

	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
