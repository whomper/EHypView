/*
 * EHypView - the platform layer: files and folders.
 *
 * Everything in src/core reaches the disk through these functions, so
 * the same code runs on the Atari (atari/plat_tos.c, GEMDOS) and on a
 * development machine (src/core/plat_posix.c) for the tests.  The names
 * follow ECal's plat.h.
 *
 * Files are opened, used and closed within one call: an accessory must
 * not keep handles (or a DTA) across AES calls.
 */
#ifndef EHV_PLAT_H
#define EHV_PLAT_H

#define PF_READ  0
#define PF_WRITE 1                   /* create or truncate */

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

/* drives present: bit n = drive A + n; 0 on systems without drives */
unsigned long pf_drives(void);

/* --- portable helpers (plat.c) --------------------------------------- */

void pf_free_list(char **names, long n);

/* read exactly n bytes at offset off; 0 on success */
int pf_read_at(int h, unsigned long off, void *buf, unsigned long n);

/* the whole file, NUL-terminated (not counted in *len); NULL on error */
char *pf_load(const char *path, unsigned long *len);

/* write a whole file safely: "path.tmp", then rename over path */
int pf_save(const char *path, const void *data, unsigned long len);

#endif
