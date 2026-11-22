/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tos_stub.c - TOS stood in for under qemu-m68k -cpu cfv4e, for EHVCHECK
 * built as ColdFire code (the FireBee edition's core: the guidelines, 7.4).
 * Linux gives trap #1, #13 and #14 as SIGILL; this handler (in and out
 * through common's tests/coldfire/rt.S) answers them as plain TOS would:
 * drive C: is ROOT on the host (each name found whatever its case, as FAT
 * keeps them), files and folders through Linux's calls, Fsfirst and Fsnext
 * over a folder's names, Malloc from a pool, Fxattr and Dopendir unknown
 * (EINVFN: plat_tos.c then reads folders as TOS's Fsfirst does), Cconws to
 * stdout, Supexec's function called as it is (low memory a page of zeros:
 * no cookie jar, the 200 Hz count 0). Anything else ends the run, named.
 */
#include <stddef.h>
#include <string.h>

long sys_write(long fd, const void *p, long n);
long sys_read(long fd, void *p, long n);
long sys_open(const char *path, long flags, long mode);
long sys_close(long fd);
long sys_call4(long nr, long a, long b, long c, long d);
void sys_exit(long code) __attribute__((noreturn));
void sig_entry(void);
extern void (*sig_handler)(int, void *, void *);

typedef struct {
	unsigned long uc_flags, uc_link, ss_sp, ss_flags, ss_size;
	unsigned long version;
	unsigned long gregs[18];        /* d0-d7, a0-a6, sp, pc, sr */
} UCTX;

#ifndef ROOT
#error ROOT: the host folder that is drive C:
#endif

#define O_DIR   040000
#define O_CREAT 0100
#define O_TRUNC 01000
#define EFILNF  (-33)
#define EPTHNF  (-34)
#define ENMFIL  (-49)
#define EINVFN  (-32)

static void say(const char *s) { sys_write(2, s, (long) strlen(s)); }

static void say_hex(unsigned long v)
{
	char b[11];
	int i;

	b[0] = '0';
	b[1] = 'x';
	for (i = 0; i < 8; i++)
		b[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
	b[10] = 0;
	say(b);
}

static int up(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static int same_name(const char *a, const char *b)
{
	while (*a && up((unsigned char) *a) == up((unsigned char) *b))
		a++, b++;
	return *a == 0 && *b == 0;
}

/* a folder's names, one at a time (getdents64); 0 at the end */
static long dir_fd = -1;
static char dbuf[4096];
static long dlen, dpos;

static const char *dir_next(void)
{
	for (;;)
	{
		const char *name;
		unsigned short rec;

		if (dpos >= dlen)
		{
			dlen = sys_call4(220, dir_fd, (long) dbuf, sizeof(dbuf), 0);
			dpos = 0;
			if (dlen <= 0)
				return NULL;
		}
		rec = *(unsigned short *) (dbuf + dpos + 16);
		name = dbuf + dpos + 19;
		dpos += rec;
		if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
			return name;
	}
}

/* a TOS path (C:\FIXT\BASIC.HYP, or relative to C:\) to the host's: each
   part found as it is on the disk, whatever its case; the last part may
   not be there (to make it), then it stays as it was given. 0, or EPTHNF */
static int host_path(const char *tos, char *out, size_t cap)
{
	size_t n = strlen(ROOT);
	const char *p = tos;

	if (n + 2 >= cap)
		return EPTHNF;
	memcpy(out, ROOT, n);
	out[n] = 0;
	if (p[0] && p[1] == ':')
		p += 2;
	while (*p == '\\' || *p == '/')
		p++;
	while (*p)
	{
		char part[128];
		size_t k = 0;
		const char *name, *found = NULL;
		long fd;

		while (*p && *p != '\\' && *p != '/' && k < sizeof(part) - 1)
			part[k++] = *p++;
		part[k] = 0;
		while (*p == '\\' || *p == '/')
			p++;
		fd = sys_open(out, O_DIR, 0);
		if (fd >= 0)
		{
			long keep_fd = dir_fd, keep_len = dlen, keep_pos = dpos;

			dir_fd = fd;
			dlen = dpos = 0;
			while ((name = dir_next()) != NULL)
				if (same_name(part, name))
				{
					found = name;
					break;
				}
			if (found)
				strncpy(part, found, sizeof(part) - 1);
			sys_close(fd);
			dir_fd = keep_fd;
			dlen = keep_len;
			dpos = keep_pos;
		}
		if (!found && *p)
			return EPTHNF;
		if (n + 1 + strlen(part) + 1 >= cap)
			return EPTHNF;
		out[n++] = '/';
		strcpy(out + n, part);
		n += strlen(part);
	}
	return 0;
}

/* Fsfirst's DTA (reserved 21 bytes, attribute, time, date, length, name) */
typedef struct {
	char res[21];
	unsigned char attr;
	unsigned short time, date;
	unsigned long length;
	char name[14];
} DTA;

static DTA own_dta, *dta = &own_dta;
static char find_dir[512], find_mask[16];
static long find_fd = -1;

/* a TOS mask (*.*, *.HYP, NAME.EXT) against a name, as FAT names it */
static int mask_match(const char *m, const char *n)
{
	if (*m == 0)
		return *n == 0;
	if (*m == '*')
	{
		for (;; n++)
		{
			if (mask_match(m + 1, n))
				return 1;
			if (*n == 0)
				return 0;
		}
	}
	if (*m == '.' && m[1] == '*' && m[2] == 0 && *n == 0)
		return 1;                       /* "*.*" takes a name without a dot */
	if (*n == 0 || (*m != '?' && up((unsigned char) *m) != up((unsigned char) *n)))
		return 0;
	return mask_match(m + 1, n + 1);
}

static long find_next(void)
{
	const char *name;
	long keep_fd = dir_fd;

	dir_fd = find_fd;
	while ((name = dir_next()) != NULL)
	{
		char path[640];
		long fd;
		size_t i;

		if (!mask_match(find_mask, name) || strlen(name) > 12)
			continue;
		strcpy(path, find_dir);
		strcat(path, "/");
		strcat(path, name);
		memset(dta, 0, sizeof(*dta));
		fd = sys_open(path, O_DIR, 0);
		if (fd >= 0)
		{
			dta->attr = 0x10;
			sys_close(fd);
		} else if ((fd = sys_open(path, 0, 0)) >= 0)
		{
			dta->length = (unsigned long) sys_call4(19, fd, 0, 2, 0);       /* lseek to the end */
			sys_close(fd);
		} else
			continue;
		dta->time = 0x6000;             /* 12:00, 20 Nov 2026: the same each run */
		dta->date = (unsigned short) (((2026 - 1980) << 9) | (11 << 5) | 20);
		for (i = 0; name[i] && i < 13; i++)
			dta->name[i] = (char) up((unsigned char) name[i]);
		dir_fd = keep_fd;
		return 0;
	}
	dir_fd = keep_fd;
	return ENMFIL;
}

static long fsfirst(const char *tos)
{
	char t[512];
	char *slash;
	size_t n = strlen(tos);

	if (n >= sizeof(t))
		return EPTHNF;
	strcpy(t, tos);
	slash = strrchr(t, '\\');
	if (slash == NULL)
		slash = strchr(t, ':');
	strncpy(find_mask, slash ? slash + 1 : t, sizeof(find_mask) - 1);
	if (slash)
		slash[slash[0] == ':' ? 1 : 0] = 0;
	else
		t[0] = 0;
	if (host_path(t, find_dir, sizeof(find_dir)) != 0)
		return EPTHNF;
	if (find_fd >= 0)
		sys_close(find_fd);
	find_fd = sys_open(find_dir, O_DIR, 0);
	if (find_fd < 0)
		return EPTHNF;
	dlen = dpos = 0;
	{
		long r = find_next();

		return r == ENMFIL ? EFILNF : r;
	}
}

/* Malloc's pool */
static char pool[6L * 1024 * 1024] __attribute__((aligned(4)));
static long pool_used;

static long gemdos(unsigned long sp)
{
	unsigned short fn = *(unsigned short *) sp;
	char path[640];

	switch (fn)
	{
	case 0x09:                              /* Cconws */
		{
			const char *s = *(const char **) (sp + 2);

			sys_write(1, s, (long) strlen(s));
			return 0;
		}
	case 0x0e:                              /* Dsetdrv */
		return 1L << 2;
	case 0x19:                              /* Dgetdrv: C: */
		return 2;
	case 0x1a:                              /* Fsetdta */
		dta = *(DTA **) (sp + 2);
		return 0;
	case 0x2f:                              /* Fgetdta */
		return (long) dta;
	case 0x2a:                              /* Tgetdate */
		return ((2026 - 1980) << 9) | (11 << 5) | 20;
	case 0x2c:                              /* Tgettime */
		return 0x6000;
	case 0x3b:                              /* Dsetpath */
		return 0;
	case 0x47:                              /* Dgetpath: the root */
		**(char **) (sp + 2) = 0;
		return 0;
	case 0x39:                              /* Dcreate */
		if (host_path(*(const char **) (sp + 2), path, sizeof(path)) != 0)
			return EPTHNF;
		return sys_call4(39, (long) path, 0755, 0, 0) == 0 ? 0 : EPTHNF;
	case 0x3c:                              /* Fcreate */
		if (host_path(*(const char **) (sp + 2), path, sizeof(path)) != 0)
			return EPTHNF;
		{
			long fd = sys_open(path, 2 | O_CREAT | O_TRUNC, 0644);

			return fd >= 0 ? fd : EPTHNF;
		}
	case 0x3d:                              /* Fopen */
		if (host_path(*(const char **) (sp + 2), path, sizeof(path)) != 0)
			return EFILNF;
		{
			long fd = sys_open(path, *(short *) (sp + 6) & 3, 0);

			return fd >= 0 ? fd : EFILNF;
		}
	case 0x3e:                              /* Fclose */
		return sys_close(*(short *) (sp + 2));
	case 0x3f:                              /* Fread */
		return sys_read(*(short *) (sp + 2), *(void **) (sp + 8), *(long *) (sp + 4));
	case 0x40:                              /* Fwrite */
		return sys_write(*(short *) (sp + 2), *(void **) (sp + 8), *(long *) (sp + 4));
	case 0x41:                              /* Fdelete */
		if (host_path(*(const char **) (sp + 2), path, sizeof(path)) != 0)
			return EFILNF;
		return sys_call4(10, (long) path, 0, 0, 0) == 0 ? 0 : EFILNF;
	case 0x42:                              /* Fseek(offset, handle, mode) */
		return sys_call4(19, *(short *) (sp + 6), *(long *) (sp + 2), *(short *) (sp + 8), 0);
	case 0x43:                              /* Fattrib: an ordinary file */
		return 0;
	case 0x44:                              /* Mxalloc: not in plain TOS 1 */
		return EINVFN;
	case 0x48:                              /* Malloc */
		{
			long n = *(long *) (sp + 2);

			if (n == -1)
				return (long) sizeof(pool) - pool_used;
			n = (n + 3) & ~3L;
			if (n <= 0 || pool_used + n > (long) sizeof(pool))
				return 0;
			pool_used += n;
			return (long) (pool + pool_used - n);
		}
	case 0x49:                              /* Mfree */
	case 0x4a:                              /* Mshrink */
		return 0;
	case 0x4e:                              /* Fsfirst */
		return fsfirst(*(const char **) (sp + 2));
	case 0x4f:                              /* Fsnext */
		return find_fd >= 0 ? find_next() : ENMFIL;
	case 0x4c:                              /* Pterm */
		sys_exit(*(short *) (sp + 2));
	case 0x00:                              /* Pterm0 */
		sys_exit(0);
	case 0x56:                              /* Frename(0, from, to) */
		{
			char to[640];

			if (host_path(*(const char **) (sp + 4), path, sizeof(path)) != 0 ||
			    host_path(*(const char **) (sp + 8), to, sizeof(to)) != 0)
				return EFILNF;
			return sys_call4(38, (long) path, (long) to, 0, 0) == 0 ? 0 : EFILNF;
		}
	case 0x57:                              /* Fdatime: the same time each run */
		{
			unsigned short *dt = *(unsigned short **) (sp + 2);

			if (*(short *) (sp + 8) == 0)
			{
				dt[0] = 0x6000;
				dt[1] = (unsigned short) (((2026 - 1980) << 9) | (11 << 5) | 20);
			}
			return 0;
		}
	case 0x128:                             /* Dopendir, Fxattr: not in TOS */
	case 0x12c:
	case 0x119:                             /* Pdomain */
		return EINVFN;
	}
	say("tos_stub: GEMDOS ");
	say_hex(fn);
	say(" not stood in for\n");
	sys_exit(3);
}

static long bios(unsigned long sp)
{
	unsigned short fn = *(unsigned short *) sp;

	if (fn == 10)                           /* Drvmap: C: */
		return 1L << 2;
	if (fn == 11)                           /* Kbshift */
		return 0;
	if (fn == 1)                            /* Bconstat: a key waits */
		return -1;
	if (fn == 2)                            /* Bconin: Return ("Press a key.") */
		return 0x1c000dL;
	say("tos_stub: BIOS ");
	say_hex(fn);
	say(" not stood in for\n");
	sys_exit(3);
}

static long xbios(unsigned long sp)
{
	unsigned short fn = *(unsigned short *) sp;

	if (fn == 38)                           /* Supexec: called in user mode */
		return (*(long (**)(void)) (sp + 2))();
	say("tos_stub: XBIOS ");
	say_hex(fn);
	say(" not stood in for\n");
	sys_exit(3);
}

static void handler(int sig, void *info, void *ucv)
{
	UCTX *uc = ucv;
	unsigned long sp = uc->gregs[15], pc = uc->gregs[16];
	unsigned short op;

	(void) sig;
	(void) info;
	op = *(unsigned short *) pc;
	if (op != 0x4e41 && op != 0x4e4d && op != 0x4e4e)
	{
		op = *(unsigned short *) (pc - 2);
		pc -= 2;
	}
	switch (op)
	{
	case 0x4e41:
		uc->gregs[0] = (unsigned long) gemdos(sp);
		break;
	case 0x4e4d:
		uc->gregs[0] = (unsigned long) bios(sp);
		break;
	case 0x4e4e:
		uc->gregs[0] = (unsigned long) xbios(sp);
		break;
	default:
		say("tos_stub: SIGILL at ");
		say_hex(pc);
		say(", not a TOS trap\n");
		sys_exit(3);
	}
	uc->gregs[16] = pc + 2;
}

void tos_install(void)
{
	struct {
		void *handler;
		unsigned long flags;
		void *restorer;
		unsigned long mask[2];
	} sa;
	long mm[6];

	/* low memory: a page of zeros (no cookie jar at 0x5a0, the 200 Hz
	   count at 0x4ba 0), as Supexec's functions read it */
	mm[0] = 0;
	mm[1] = 4096;
	mm[2] = 3;                              /* PROT_READ | PROT_WRITE */
	mm[3] = 0x32;                           /* MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED */
	mm[4] = -1;
	mm[5] = 0;
	if (sys_call4(90, (long) mm, 0, 0, 0) != 0)
	{
		say("tos_stub: low memory not mapped\n");
		sys_exit(3);
	}
	memset(&sa, 0, sizeof(sa));
	sig_handler = handler;
	sa.handler = (void *) sig_entry;
	sa.flags = 4;                           /* SA_SIGINFO */
	if (sys_call4(174, 4, (long) &sa, 0, 8) != 0)   /* rt_sigaction(SIGILL) */
	{
		say("tos_stub: rt_sigaction failed\n");
		sys_exit(3);
	}
}
