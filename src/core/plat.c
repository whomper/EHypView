/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - portable helpers on top of plat.h.
 */
#include <stdlib.h>
#include <string.h>
#include "plat.h"

void pf_free_list(char **names, long n)
{
	long i;

	if (names == NULL)
		return;
	for (i = 0; i < n; i++)
		free(names[i]);
	free(names);
}

static int read_full(int h, unsigned char *buf, unsigned long n)
{
	while (n > 0)
	{
		/* TOS reads at most 2 GB at once; keep requests modest */
		long chunk = n > 0x10000UL ? 0x10000L : (long) n;
		long r = pf_read(h, buf, chunk);

		if (r <= 0)
			return -1;
		buf += r;
		n -= (unsigned long) r;
	}
	return 0;
}

int pf_read_at(int h, unsigned long off, void *buf, unsigned long n)
{
	if (pf_seek(h, (long) off) != (long) off)
		return -1;
	return read_full(h, buf, n);
}

/* where pf_save writes first: the last letter of the extension '$'
   (EHYPVIEW.IN$; '~' when it is '$' already), NAME.$$$ without one. An
   8.3 name, as TOS's FAT wants: there EHYPVIEW.INF.tmp is EHYPVIEW.INF
   itself, so the old file went and the new one with it */
static char *tmp_name(const char *path)
{
	const char *base = path, *dot = NULL, *p;
	size_t n = strlen(path);
	char *out = malloc(n + 5);

	if (out == NULL)
		return NULL;
	for (p = path; *p != '\0'; p++)
	{
		if (*p == '\\' || *p == '/' || *p == ':')
		{
			base = p + 1;
			dot = NULL;
		} else if (*p == '.' && p > base)
			dot = p;
	}
	strcpy(out, path);
	if (dot == NULL)
		strcat(out, ".$$$");
	else if (strlen(dot + 1) < 3)
		strcat(out, "$");
	else
		out[n - 1] = out[n - 1] == '$' ? '~' : '$';
	return out;
}

char *pf_load(const char *path, unsigned long *len)
{
	pf_info info;
	char *buf;
	int h;

	if (len != NULL)
		*len = 0;
	if (pf_stat(path, &info) != 0)
	{
		/* a save cut short after the old file went: the new one is whole */
		char *tmp = tmp_name(path);
		int ok = tmp != NULL && pf_stat(tmp, &info) == 0 && pf_rename(tmp, path) == 0;

		free(tmp);
		if (!ok || pf_stat(path, &info) != 0)
			return NULL;
	}
	if (info.is_dir)
		return NULL;
	h = pf_open(path, PF_READ);
	if (h < 0)
		return NULL;
	buf = malloc(info.size + 1);
	if (buf == NULL || read_full(h, (unsigned char *) buf, info.size) != 0)
	{
		free(buf);
		pf_close(h);
		return NULL;
	}
	pf_close(h);
	buf[info.size] = '\0';
	if (len != NULL)
		*len = info.size;
	return buf;
}

int pf_save(const char *path, const void *data, unsigned long len)
{
	char *tmp = tmp_name(path);
	const unsigned char *p = data;
	unsigned long left = len;
	int h, ok = 1;

	if (tmp == NULL)
		return -1;
	h = pf_open(tmp, PF_WRITE);
	if (h < 0)
	{
		free(tmp);
		return -1;
	}
	while (ok && left > 0)
	{
		long chunk = left > 0x10000UL ? 0x10000L : (long) left;

		if (pf_write(h, p, chunk) != chunk)
			ok = 0;
		p += chunk;
		left -= (unsigned long) chunk;
	}
	pf_close(h);
	if (ok)
	{
		/* TOS cannot rename over an existing file */
		pf_remove(path);
		ok = pf_rename(tmp, path) == 0;
	}
	if (!ok)
		pf_remove(tmp);
	free(tmp);
	return ok ? 0 : -1;
}
