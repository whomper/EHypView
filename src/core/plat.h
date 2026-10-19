/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the platform layer: files and folders.
 *
 * Everything in src/core reaches the disk through these functions, so
 * the same code runs on the Atari (atari/plat_tos.c, GEMDOS) and on a
 * development machine (src/core/plat_posix.c) for the tests.  The names
 * follow ECal's plat.h.
 *
 * Files are opened, used and closed within one call: an accessory must
 * not keep handles (or a DTA) across AES calls.  Each open and each look
 * at a folder costs real time on an Atari's disks (the AERA guidelines,
 * 6.5): a file is looked at once, read in big pieces, written in one go.
 */
#ifndef EHV_PLAT_H
#define EHV_PLAT_H

#define PF_READ  0
#define PF_WRITE 1                   /* create or truncate */
#define PF_APPEND 2                  /* write on at the end of a file there is */

typedef struct {
	int is_dir;
	unsigned long size;
	unsigned long mtime;             /* any value that changes when the file does */
	unsigned long dev, ino;          /* ino is 0 where the OS has no inodes */
} pf_info;

/* handle >= 0, or < 0 on error */
int  pf_open(const char *path, int mode);
long pf_read(int h, void *buf, long n);
long pf_write(int h, const void *buf, long n);
long pf_seek(int h, long off);       /* from the start; new position or < 0 */
void pf_close(int h);
/* an open file's size and date (no look at its folder); the position
   goes back to the start.  0 on success */
int  pf_finfo(int h, pf_info *info);

/* 0 on success, -1 if the path does not exist */
int  pf_stat(const char *path, pf_info *info);
int  pf_remove(const char *path);
int  pf_rename(const char *from, const char *to);

/*
 * The names in a folder, without "." and "..": returns the count and a
 * malloc'ed array of malloc'ed strings, or -1 if it cannot be read.
 * Free with pf_free_list().
 */
long pf_list_dir(const char *path, char ***names);
/* the same, with each name's pf_info in *infos (malloc'ed, as many as
   names), as the folder's listing gives them; *infos is NULL where the
   system's listing has none (MiNT, MagiC): then pf_stat() each */
long pf_list_dir_info(const char *path, char ***names, pf_info **infos);

/* drives present: bit n = drive A + n; 0 on systems without drives */
unsigned long pf_drives(void);

/* --- portable helpers (plat.c) --------------------------------------- */

void pf_free_list(char **names, long n);

/* read exactly n bytes at offset off; 0 on success */
int pf_read_at(int h, unsigned long off, void *buf, unsigned long n);

/* the whole file, NUL-terminated (not counted in *len); NULL on error.
   One open, no look at the folder unless the file isn't there */
char *pf_load(const char *path, unsigned long *len);
/* the same, with the file's size and date in *info (when not NULL) */
char *pf_load_info(const char *path, unsigned long *len, pf_info *info);

/* pf_look(): pf_stat() remembered for the rest of an event's work, so a
   file is looked up once (guidelines 6.5).  Off until pf_looks(1); the
   program calls pf_forget() before each event call, and pf_save()
   forgets them too */
void pf_looks(int on);
int  pf_look(const char *path, pf_info *info);
void pf_forget(void);

/* write a whole file safely: NAME.IN$ for NAME.INF (8.3, as TOS wants),
   then renamed over it; pf_load finishes a save cut short in between */
int pf_save(const char *path, const void *data, unsigned long len);

/* where pf_save() writes first, before renaming (malloc'ed): EHYPVIEW.IN$ */
char *pf_tmp_name(const char *path);

#ifndef EHV_TOS
/* plat_posix.c, for the tests: opens (pf_open) and looks (pf_stat,
   folder listings) so far */
extern unsigned long pf_opens, pf_lookups;
#endif

#endif
