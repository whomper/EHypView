/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - plat.h on POSIX systems, for the host tools and tests.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include "plat.h"

unsigned long pf_opens, pf_lookups;

int pf_open(const char *path, int mode)
{
	pf_opens++;
	if (mode == PF_WRITE)
		return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (mode == PF_APPEND)
		return open(path, O_WRONLY | O_APPEND);
	return open(path, O_RDONLY);
}

long pf_read(int h, void *buf, long n)
{
	return (long) read(h, buf, (size_t) n);
}

long pf_write(int h, const void *buf, long n)
{
	return (long) write(h, buf, (size_t) n);
}

long pf_seek(int h, long off)
{
	return (long) lseek(h, (off_t) off, SEEK_SET);
}

void pf_close(int h)
{
	close(h);
}

static void from_stat(const struct stat *st, pf_info *info)
{
	info->is_dir = S_ISDIR(st->st_mode) ? 1 : 0;
	info->size = (unsigned long) st->st_size;
	info->mtime = (unsigned long) st->st_mtime;
	info->dev = (unsigned long) st->st_dev;
	info->ino = (unsigned long) st->st_ino;
}

int pf_stat(const char *path, pf_info *info)
{
	struct stat st;

	pf_lookups++;
	if (stat(path, &st) != 0)
		return -1;
	from_stat(&st, info);
	return 0;
}

int pf_finfo(int h, pf_info *info)
{
	struct stat st;

	if (fstat(h, &st) != 0 || lseek(h, 0, SEEK_SET) != 0)
		return -1;
	from_stat(&st, info);
	return 0;
}

int pf_remove(const char *path)
{
	return remove(path);
}

int pf_rename(const char *from, const char *to)
{
	return rename(from, to);
}

long pf_list_dir(const char *path, char ***names)
{
	return pf_list_dir_info(path, names, NULL);
}

long pf_list_dir_info(const char *path, char ***names, pf_info **infos)
{
	DIR *dir;
	struct dirent *de;
	char **list = NULL;
	long n = 0, cap = 0;

	*names = NULL;
	if (infos != NULL)
		*infos = NULL;
	pf_lookups++;
	dir = opendir(path);
	if (dir == NULL)
		return -1;
	while ((de = readdir(dir)) != NULL)
	{
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;
		if (n == cap)
		{
			long ncap = cap ? cap * 2 : 32;
			char **p = realloc(list, (size_t) ncap * sizeof(char *));

			if (p == NULL)
				break;
			list = p;
			cap = ncap;
		}
		if ((list[n] = strdup(de->d_name)) == NULL)
			break;
		n++;
	}
	closedir(dir);
	*names = list;
	if (infos != NULL && n > 0)
	{
		/* as an Atari's listing gives them (the stat()s aren't counted:
		   the listing has them there) */
		long i;

		*infos = calloc((size_t) n, sizeof(pf_info));
		for (i = 0; *infos != NULL && i < n; i++)
		{
			char *full = malloc(strlen(path) + strlen(list[i]) + 2);
			struct stat st;

			if (full == NULL)
				break;
			sprintf(full, "%s/%s", path, list[i]);
			if (stat(full, &st) == 0)
				from_stat(&st, &(*infos)[i]);
			free(full);
		}
	}
	return n;
}

unsigned long pf_drives(void)
{
	return 0;
}
