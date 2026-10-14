/*
 * EHypView - plat.h on GEMDOS: TOS, EmuTOS, MagiC and MiNT.
 *
 * Copyright (C) the EHypView authors.  Free software under the GNU
 * General Public License, version 2 or (at your option) any later
 * version; see COPYING.  Follows ECal's atari/plat_tos.c.
 *
 * Folders are read with Dopendir/Dreaddir where the system has them
 * (MiNT, MagiC: long and lower-case names), else with Fsfirst/Fsnext
 * (TOS, EmuTOS: 8+3 names).  File details come from Fxattr, or from
 * Fsfirst's DTA.  Every call saves and restores the caller's DTA.
 */
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "../src/core/plat.h"

#define EINVFN    -32
#define EFILNF    -33
#define ENMFILES  -49

#define FA_RDONLY 0x01
#define FA_HIDDEN 0x02
#define FA_SYSTEM 0x04
#define FA_LABEL  0x08
#define FA_DIR    0x10
#define FA_ALL    (FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_DIR)

#define PATH_MAX_LEN 512

typedef struct {
	u16 mode;
	long index;
	u16 dev, rdev, nlink, uid, gid;
	long size, blksize, nblocks;
	u16 mtime, mdate, atime, adate, ctime, cdate;
	u16 attr, reserved2;
	long reserved3[2];
} XATTR;

static inline long trap13_w(short fn)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %1,-(%%sp)\n\ttrap #13\n\taddq.l #2,%%sp"
		: "=r"(r) : "r"(fn) : TRAP_CLOBBER);
	return r;
}

#define Drvmap()              trap13_w(10)
#define Dopendir(n, f)        trap1_wlw(0x128, (long)(n), (f))
#define Dreaddir(l, h, b)     trap1_wwll(0x129, (l), (long)(h), (long)(b))
#define Dclosedir(h)          trap1_wl(0x12a, (long)(h))
#define Fxattr(f, n, x)       trap1_wwll(0x12c, (f), (long)(n), (long)(x))

int pf_open(const char *path, int mode)
{
	if (mode == PF_WRITE)
		return (int) Fcreate(path, 0);
	return (int) Fopen(path, 0);
}

long pf_read(int h, void *buf, long n) { return Fread((short) h, n, buf); }
long pf_write(int h, const void *buf, long n) { return Fwrite((short) h, n, buf); }
long pf_seek(int h, long off) { return Fseek(off, (short) h, 0); }
void pf_close(int h) { Fclose((short) h); }
int pf_remove(const char *path) { return (int) Fdelete(path); }
int pf_rename(const char *from, const char *to) { return (int) Frename(from, to); }

unsigned long pf_drives(void)
{
	return (unsigned long) Drvmap();
}

/* copy path without trailing separators (but keep "C:\") */
static int clean_path(const char *path, char *buf)
{
	size_t n = strlen(path);

	if (n == 0 || n >= PATH_MAX_LEN)
		return -1;
	memcpy(buf, path, n + 1);
	while (n > 1 && (buf[n - 1] == '\\' || buf[n - 1] == '/') && !(n == 3 && buf[1] == ':'))
		buf[--n] = '\0';
	return 0;
}

static int is_root(const char *p)
{
	return p[1] == ':' && (p[2] == '\0' || (p[3] == '\0' && (p[2] == '\\' || p[2] == '/')));
}

static int drive_present(char letter)
{
	int d = (letter >= 'a' && letter <= 'z') ? letter - 'a' : letter - 'A';

	return d >= 0 && d < 26 && (pf_drives() & (1UL << d)) != 0;
}

int pf_stat(const char *path, pf_info *info)
{
	char p[PATH_MAX_LEN];
	XATTR x;
	long r;

	if (clean_path(path, p) != 0)
		return -1;
	memset(info, 0, sizeof(*info));
	/*
	 * Folders are told apart by their path: inode numbers on FAT under
	 * MiNT are made up and not reliable enough to detect loops.
	 */
	info->ino = 0;

	if (is_root(p))
	{
		if (!drive_present(p[0]))
			return -1;
		info->is_dir = 1;
		return 0;
	}

	r = Fxattr(0, p, &x);
	if (r == 0)
	{
		info->is_dir = (x.mode & 0xF000) == 0x4000;
		info->size = (unsigned long) x.size;
		info->mtime = ((unsigned long) x.mdate << 16) | x.mtime;
		info->dev = x.dev;
		return 0;
	}
	if (r != EINVFN)
		return -1;

	{
		DTA dta;
		void *old = Fgetdta();

		Fsetdta(&dta);
		r = Fsfirst(p, FA_ALL);
		Fsetdta(old);
		if (r != 0)
			return -1;
		info->is_dir = (dta.d_attrib & FA_DIR) != 0;
		info->size = dta.d_length;
		info->mtime = ((unsigned long) dta.d_date << 16) | dta.d_time;
		return 0;
	}
}

/* add a copy of name to a growing list; -1 when out of memory */
static int add_name(char ***list, long *n, long *cap, const char *name)
{
	if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
		return 0;
	if (*n == *cap)
	{
		long ncap = *cap ? *cap * 2 : 32;
		char **p = realloc(*list, (size_t) ncap * sizeof(char *));

		if (p == NULL)
			return -1;
		*list = p;
		*cap = ncap;
	}
	if (((*list)[*n] = strdup(name)) == NULL)
		return -1;
	(*n)++;
	return 0;
}

long pf_list_dir(const char *path, char ***names)
{
	char p[PATH_MAX_LEN + 8];
	char **list = NULL;
	long n = 0, cap = 0, h, r;

	*names = NULL;
	if (clean_path(path, p) != 0)
		return -1;

	/* MiNT and MagiC: real (long) names */
	h = Dopendir(p, 0);
	if (h < 0 && h > -1000 && h != EINVFN)
		return -1;              /* the folder does not exist */
	if (h != EINVFN)
	{
		static char buf[4 + 256];

		while ((r = Dreaddir((short) sizeof(buf), h, buf)) == 0)
		{
			buf[sizeof(buf) - 1] = '\0';
			if (add_name(&list, &n, &cap, buf + 4) < 0)
				break;
		}
		Dclosedir(h);
		*names = list;
		return n;
	}

	/* TOS: 8+3 names */
	{
		DTA dta;
		void *old = Fgetdta();
		size_t len = strlen(p);

		if (len > 0 && p[len - 1] != '\\' && p[len - 1] != '/')
			strcat(p, "\\");
		strcat(p, "*.*");
		Fsetdta(&dta);
		r = Fsfirst(p, FA_ALL);
		if (r != 0 && r != ENMFILES && r != EFILNF)
		{
			Fsetdta(old);
			return -1;
		}
		for (; r == 0; r = Fsnext())
		{
			if (dta.d_attrib & FA_LABEL)
				continue;
			if (add_name(&list, &n, &cap, dta.d_fname) < 0)
				break;
		}
		Fsetdta(old);
	}
	*names = list;
	return n;
}
