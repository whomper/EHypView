/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - bookmarks (see bookmark.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bookmark.h"
#include "charset.h"

void bm_init(ehv_bookmarks *bm)
{
	memset(bm, 0, sizeof(*bm));
}

static void drop(ehv_bookmark *b)
{
	free(b->path);
	free(b->node);
	free(b->title);
	memset(b, 0, sizeof(*b));
}

void bm_free(ehv_bookmarks *bm)
{
	unsigned int i;

	for (i = 0; i < bm->n; i++)
		drop(&bm->b[i]);
	bm->n = 0;
}

int bm_find(const ehv_bookmarks *bm, const char *path, const char *node)
{
	unsigned int i;

	for (i = 0; i < bm->n; i++)
		if (atari_casecmp(bm->b[i].path, path) == 0 && strcmp(bm->b[i].node, node) == 0)
			return (int) i;
	return -1;
}

static char *copy(const char *s, size_t max)
{
	size_t n = strlen(s);
	char *c;

	if (n > max)
		n = max;
	c = malloc(n + 1);
	if (c != NULL)
	{
		memcpy(c, s, n);
		c[n] = '\0';
	}
	return c;
}

/* a field may not hold the separator or a line end */
static int clean(const char *s)
{
	return strpbrk(s, "|\r\n") == NULL;
}

int bm_add(ehv_bookmarks *bm, const char *path, const char *node, const char *title)
{
	int i = bm_find(bm, path, node);
	ehv_bookmark *b;

	if (i >= 0)
		return i;
	if (bm->n >= BM_MAX || path[0] == '\0' || !clean(path) || !clean(node))
		return -1;
	b = &bm->b[bm->n];
	b->path = copy(path, strlen(path));
	b->node = copy(node, strlen(node));
	b->title = copy(title[0] != '\0' ? title : node, BM_TITLE_MAX);
	if (b->path == NULL || b->node == NULL || b->title == NULL)
	{
		drop(b);
		return -2;
	}
	/* a '|' in a title would split it when read again */
	for (i = 0; b->title[i] != '\0'; i++)
		if (b->title[i] == '|' || b->title[i] == '\r' || b->title[i] == '\n')
			b->title[i] = ' ';
	return (int) bm->n++;
}

int bm_remove(ehv_bookmarks *bm, unsigned int i)
{
	if (i >= bm->n)
		return -1;
	drop(&bm->b[i]);
	memmove(&bm->b[i], &bm->b[i + 1], (bm->n - i - 1) * sizeof(bm->b[0]));
	bm->n--;
	memset(&bm->b[bm->n], 0, sizeof(bm->b[0]));
	return 0;
}

void bm_key(unsigned int i, char *buf, unsigned long size)
{
	snprintf(buf, size, "BOOKMARK%u", i + 1);
}

void bm_read(ehv_bookmarks *bm, const inf_file *inf)
{
	unsigned int i;

	bm_free(bm);
	for (i = 0; inf != NULL && i < BM_MAX; i++)
	{
		char key[16], *v, *a, *b;
		const char *s;

		bm_key(i, key, sizeof(key));
		s = inf_get(inf, key);
		if (s == NULL || s[0] == '\0' || (v = copy(s, strlen(s))) == NULL)
			continue;
		a = strchr(v, '|');
		b = a != NULL ? strchr(a + 1, '|') : NULL;
		if (b != NULL)
		{
			*a = *b = '\0';
			bm_add(bm, v, a + 1, b + 1);
		}
		free(v);
	}
}

unsigned long bm_value(const ehv_bookmarks *bm, unsigned int i, char *buf, unsigned long size)
{
	int n;

	if (size == 0)
		return 0;
	if (i >= bm->n)
	{
		buf[0] = '\0';
		return 0;
	}
	n = snprintf(buf, size, "%s|%s|%s", bm->b[i].path, bm->b[i].node, bm->b[i].title);
	return n < 0 || (unsigned long) n >= size ? 0 : (unsigned long) n;
}
