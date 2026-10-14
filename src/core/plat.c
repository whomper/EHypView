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

char *pf_load(const char *path, unsigned long *len)
{
	pf_info info;
	char *buf;
	int h;

	if (len != NULL)
		*len = 0;
	if (pf_stat(path, &info) != 0 || info.is_dir)
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
	char *tmp = malloc(strlen(path) + 5);
	const unsigned char *p = data;
	unsigned long left = len;
	int h, ok = 1;

	if (tmp == NULL)
		return -1;
	strcpy(tmp, path);
	strcat(tmp, ".tmp");
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
