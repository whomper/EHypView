/*
 * EHypView - file system helpers.
 *
 * Paths are handled as plain strings in either Atari ("C:\GUIDES\X.HYP")
 * or Unix ("/c/guides/x.hyp") form; both separators are accepted
 * everywhere and new components are joined with the separator the base
 * path already uses.
 *
 * Directories are always read completely and closed again before the
 * caller sees the names: an accessory must not keep directory handles
 * (or a DTA) across AES calls.
 */
#ifndef EHV_FSYS_H
#define EHV_FSYS_H

typedef struct {
	int is_dir;
	unsigned long size;
	unsigned long mtime;
	unsigned long dev, ino;      /* ino is 0 where the OS has no inodes */
} fs_info;

/* 0 on success, -1 if the path does not exist */
int fs_stat(const char *path, fs_info *info);

/*
 * Read the names in a directory (without "." and "..").  Returns the
 * number of names and stores a malloc'ed array of malloc'ed strings in
 * *names, or returns -1 if the directory cannot be read.
 */
long fs_list_dir(const char *path, char ***names);
void fs_free_list(char **names, long n);

int fs_is_sep(char c);

/* separator to use when extending `path` */
char fs_sep_for(const char *path);

/* last path component (points into path) */
const char *fs_basename(const char *path);

/* malloc'ed "dir<sep>name" */
char *fs_join(const char *dir, const char *name);

/* malloc'ed directory part of path ("" if none) */
char *fs_dirname(const char *path);

/* nonzero if path has a directory part or drive ("X:") */
int fs_has_dir(const char *path);

/* nonzero if name ends with ext (".hyp"), ignoring case */
int fs_has_ext(const char *name, const char *ext);

/*
 * Find `name` in directory `dir`, ignoring case.  Returns a malloc'ed
 * full path using the on-disk spelling, or NULL.
 */
char *fs_find_nocase(const char *dir, const char *name);

/*
 * Expand a root pattern.  "*:\X" stands for X on every hard disk drive
 * (C: to Z:) on the Atari; on other systems it expands to nothing.
 * Anything else is returned unchanged.  Returns the number of paths in
 * the malloc'ed array *out.
 */
unsigned int fs_expand_root(const char *pattern, char ***out);

#endif
