/*
 * EHypView - file system helpers.
 *
 * Uses POSIX dirent/stat, which mintlib provides on the Atari both with
 * and without MiNT.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#ifdef __MINT__
#include <osbind.h>
#endif
#include "charset.h"
#include "fsys.h"

static char *dupstr(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = malloc(n);

	if (d != NULL)
		memcpy(d, s, n);
	return d;
}

int fs_stat(const char *path, fs_info *info)
{
	struct stat st;

	if (stat(path, &st) != 0)
		return -1;
	info->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
	info->size = (unsigned long) st.st_size;
	info->mtime = (unsigned long) st.st_mtime;
	info->dev = (unsigned long) st.st_dev;
	info->ino = (unsigned long) st.st_ino;
	return 0;
}

long fs_list_dir(const char *path, char ***names)
{
	DIR *dir;
	struct dirent *de;
	char **list = NULL;
	long n = 0, cap = 0;

	*names = NULL;
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
		if ((list[n] = dupstr(de->d_name)) == NULL)
			break;
		n++;
	}
	closedir(dir);
	*names = list;
	return n;
}

void fs_free_list(char **names, long n)
{
	long i;

	if (names == NULL)
		return;
	for (i = 0; i < n; i++)
		free(names[i]);
	free(names);
}

int fs_is_sep(char c)
{
	return c == '/' || c == '\\';
}

char fs_sep_for(const char *path)
{
	const char *p;

	for (p = path; *p != '\0'; p++)
		if (fs_is_sep(*p))
			return *p;
	/* "C:" alone is an Atari path */
	if (path[0] != '\0' && path[1] == ':')
		return '\\';
#ifdef __MINT__
	return '\\';
#else
	return '/';
#endif
}

const char *fs_basename(const char *path)
{
	const char *base = path, *p;

	for (p = path; *p != '\0'; p++)
		if (fs_is_sep(*p) || (*p == ':' && p == path + 1))
			base = p + 1;
	return base;
}

char *fs_join(const char *dir, const char *name)
{
	size_t dl = strlen(dir), nl = strlen(name);
	int need_sep = dl > 0 && !fs_is_sep(dir[dl - 1]) && !(dl == 2 && dir[1] == ':');
	char *s = malloc(dl + nl + 2);

	if (s == NULL)
		return NULL;
	memcpy(s, dir, dl);
	if (need_sep)
		s[dl++] = fs_sep_for(dir);
	memcpy(s + dl, name, nl + 1);
	return s;
}

char *fs_dirname(const char *path)
{
	const char *base = fs_basename(path);
	size_t n = (size_t) (base - path);
	char *s;

	/* drop the separator, but keep "C:\" and "/" intact */
	if (n > 1 && fs_is_sep(path[n - 1]) && !(n == 3 && path[1] == ':'))
		n--;
	s = malloc(n + 1);
	if (s == NULL)
		return NULL;
	memcpy(s, path, n);
	s[n] = '\0';
	return s;
}

int fs_has_dir(const char *path)
{
	return fs_basename(path) != path;
}

int fs_has_ext(const char *name, const char *ext)
{
	size_t nl = strlen(name), el = strlen(ext);

	return nl > el && atari_casecmp(name + nl - el, ext) == 0;
}

char *fs_find_nocase(const char *dir, const char *name)
{
	char **names;
	long n, i;
	char *found = NULL;
	fs_info info;

	/* the exact spelling is the common case: one stat, no listing */
	found = fs_join(dir, name);
	if (found != NULL && fs_stat(found, &info) == 0 && !info.is_dir)
		return found;
	free(found);
	found = NULL;

	n = fs_list_dir(dir, &names);
	for (i = 0; i < n; i++)
	{
		if (atari_casecmp(names[i], name) == 0)
		{
			found = fs_join(dir, names[i]);
			break;
		}
	}
	fs_free_list(names, n);
	return found;
}

unsigned int fs_expand_root(const char *pattern, char ***out)
{
	char **list;

	*out = NULL;
	if (pattern[0] == '*' && pattern[1] == ':')
	{
#ifdef __MINT__
		/* every drive from C: on; A: and B: are floppies */
		unsigned long map = (unsigned long) Drvmap();
		unsigned int n = 0;
		int d;

		list = calloc(32, sizeof(char *));
		if (list == NULL)
			return 0;
		for (d = 2; d < 32; d++)
		{
			if (map & (1UL << d))
			{
				char *s = dupstr(pattern);

				if (s == NULL)
					break;
				s[0] = (char) (d < 26 ? 'A' + d : '1' + d - 26);
				list[n++] = s;
			}
		}
		if (n == 0)
		{
			free(list);
			return 0;
		}
		*out = list;
		return n;
#else
		return 0;
#endif
	}
	list = calloc(1, sizeof(char *));
	if (list == NULL || (list[0] = dupstr(pattern)) == NULL)
	{
		free(list);
		return 0;
	}
	*out = list;
	return 1;
}
