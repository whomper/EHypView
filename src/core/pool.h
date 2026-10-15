/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - string pool.
 *
 * The index holds tens of thousands of short strings (page names).  A
 * malloc per string costs 8-16 bytes of overhead each and is slow under
 * TOS, so they are packed into large chunks and freed all at once.
 */
#ifndef EHV_POOL_H
#define EHV_POOL_H

#include <stddef.h>

typedef struct pool_chunk pool_chunk;

typedef struct {
	pool_chunk *head;
} ehv_pool;

/* copy s (NULL stays NULL) / the first n bytes of s; NULL when out of memory */
char *pool_strdup(ehv_pool *pool, const char *s);
char *pool_strndup(ehv_pool *pool, const char *s, size_t n);

void pool_free(ehv_pool *pool);

#endif
