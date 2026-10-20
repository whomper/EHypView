/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - find every hypertext below a list of folders.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "fsys.h"
#include "plat.h"
#include "hypfile.h"
#include "scan.h"

/* ---------------------------------------------------------------- */
/* set of visited directories                                        */

typedef struct {
	char **slots;
	unsigned long cap, n;
} strset;

static unsigned long hash(const char *s)
{
	unsigned long h = 2166136261UL;

	while (*s != '\0')
	{
		h ^= (unsigned char) *s++;
		h = (h * 16777619UL) & 0xFFFFFFFFUL;
	}
	return h;
}

/* 1 = added, 0 = already present, -1 = out of memory */
static int strset_add(strset *set, const char *key)
{
	unsigned long i;

	if (set->n * 2 >= set->cap)
	{
		unsigned long ncap = set->cap ? set->cap * 2 : 64, k;
		char **ns = calloc(ncap, sizeof(char *));

		if (ns == NULL)
			return -1;
		for (k = 0; k < set->cap; k++)
		{
			if (set->slots[k] != NULL)
			{
				i = hash(set->slots[k]) & (ncap - 1);
				while (ns[i] != NULL)
					i = (i + 1) & (ncap - 1);
				ns[i] = set->slots[k];
			}
		}
		free(set->slots);
		set->slots = ns;
		set->cap = ncap;
	}
	i = hash(key) & (set->cap - 1);
	while (set->slots[i] != NULL)
	{
		if (strcmp(set->slots[i], key) == 0)
			return 0;
		i = (i + 1) & (set->cap - 1);
	}
	set->slots[i] = malloc(strlen(key) + 1);
	if (set->slots[i] == NULL)
		return -1;
	strcpy(set->slots[i], key);
	set->n++;
	return 1;
}

static void strset_free(strset *set)
{
	unsigned long i;

	for (i = 0; i < set->cap; i++)
		free(set->slots[i]);
	free(set->slots);
	set->slots = NULL;
	set->cap = set->n = 0;
}

/* ---------------------------------------------------------------- */

typedef struct {
	char *path;
	unsigned short depth;
	unsigned short root;
} dir_item;

typedef struct {
	char *path;
	char *ref;               /* matching .REF in the same folder */
	pf_info info, rinfo;     /* as found: the .HYP, its .REF */
} file_item;

struct ehv_scan {
	ehv_index *idx;
	const ehv_index *prev;

	char **roots;
	unsigned int nroots, next_root;

	dir_item *dirs;
	unsigned long ndirs, cap_dirs;
	unsigned short cur_root;

	file_item *files;
	unsigned long nfiles, next_file;

	/* the folder being read, ENTRIES entries per step: its names and,
	   where the system's listing gives them, their sizes and dates */
	int listing;
	dir_item dir;
	char **names, **subs;
	pf_info *infos, *subinfo;
	long nnames, next_name;
	unsigned long nsub;
	int cached;               /* names and infos belong to `seen` */

	/* what the checking pass looked at, for the building one after it:
	   the folders are read once a job (the AERA guidelines, 6.5) */
	struct seen_dir {
		char *path;
		char **names;
		pf_info *infos;
		long n;
	} *seen;
	unsigned long nseen, cap_seen;
	struct seen_root {
		char *path;
		int found;
		pf_info info;
	} *roots_seen;
	unsigned long nroots_seen, cap_roots_seen;

	strset visited;
	ehv_scan_status st;
	char *current;

	/*
	 * With a previous index the scan may first only check (checking):
	 * each hypertext found must be the next one of `prev`, unchanged.
	 * At the first difference it starts again, building.  So when
	 * nothing changed nothing is copied, read or sorted.
	 */
	int checking;
	unsigned long nchecked;
	int changed;              /* a guide read again, gone, moved or new */

	/* the hypertext being read, NAMES names per step */
	int part;                 /* PART_* */
	long ng;                  /* its guide in idx */
	unsigned long next;       /* the next name to take */
	hyp_doc *doc;
	ref_db *db;
	const file_item *file;
};

#define NAMES 16              /* names added per step */

static void end_listing(ehv_scan *s);

enum { PART_NONE, PART_HYP, PART_REF };

static char *dupstr(const char *s)
{
	char *d = malloc(strlen(s) + 1);

	if (d != NULL)
		strcpy(d, s);
	return d;
}

static void set_current(ehv_scan *s, const char *path)
{
	free(s->current);
	s->current = path ? dupstr(path) : NULL;
	s->st.current = s->current;
}

ehv_scan *ehv_scan_begin_check(const char *const *roots, unsigned int nroots, const ehv_index *previous)
{
	ehv_scan *s = ehv_scan_begin(roots, nroots, previous);

	if (s != NULL && previous != NULL)
		s->checking = 1;
	return s;
}

ehv_scan *ehv_scan_begin(const char *const *roots, unsigned int nroots, const ehv_index *previous)
{
	ehv_scan *s = calloc(1, sizeof(*s));
	unsigned int i;

	if (s == NULL)
		return NULL;
	s->idx = ehv_index_new();
	s->roots = calloc(nroots ? nroots : 1, sizeof(char *));
	if (s->idx == NULL || s->roots == NULL)
		goto fail;
	for (i = 0; i < nroots; i++)
		if ((s->roots[i] = dupstr(roots[i])) == NULL)
			goto fail;
	s->nroots = nroots;
	s->prev = previous;
	return s;

fail:
	ehv_scan_abort(s);
	return NULL;
}

static void free_files(ehv_scan *s)
{
	unsigned long i;

	for (i = 0; i < s->nfiles; i++)
	{
		free(s->files[i].path);
		free(s->files[i].ref);
	}
	free(s->files);
	s->files = NULL;
	s->nfiles = s->next_file = 0;
}

void ehv_scan_abort(ehv_scan *s)
{
	unsigned long i;

	if (s == NULL)
		return;
	hyp_close(s->doc);
	ref_close(s->db);
	end_listing(s);
	for (i = 0; i < s->nseen; i++)
	{
		free(s->seen[i].path);
		pf_free_list(s->seen[i].names, s->seen[i].n);
		free(s->seen[i].infos);
	}
	free(s->seen);
	for (i = 0; i < s->nroots_seen; i++)
		free(s->roots_seen[i].path);
	free(s->roots_seen);
	ehv_index_free(s->idx);
	if (s->roots != NULL)
		for (i = 0; i < s->nroots; i++)
			free(s->roots[i]);
	free(s->roots);
	for (i = 0; i < s->ndirs; i++)
		free(s->dirs[i].path);
	free(s->dirs);
	free_files(s);
	strset_free(&s->visited);
	free(s->current);
	free(s);
}

const ehv_scan_status *ehv_scan_status_get(const ehv_scan *s)
{
	return &s->st;
}

/* from the start again, building the index (it changed) */
static void restart(ehv_scan *s)
{
	unsigned long i;

	for (i = 0; i < s->ndirs; i++)
		free(s->dirs[i].path);
	s->ndirs = 0;
	end_listing(s);
	free_files(s);
	strset_free(&s->visited);
	s->next_root = 0;
	s->checking = 0;
	s->changed = 1;
	memset(&s->st, 0, sizeof(s->st));
	set_current(s, NULL);
	/* room for what prev had, and a little more: growing the arrays
	   later would copy them, thousands of names at once */
	ehv_index_reserve(s->idx, s->prev->nguides + s->prev->nguides / 8 + 8);
}

/*
 * Identify a directory: by device and inode where the OS has them (this
 * also catches symbolic link loops), else by its path ignoring case.
 */
static char *dir_key(const char *path, const pf_info *info)
{
	char *key;

	if (info->ino != 0)
	{
		key = malloc(48);
		if (key != NULL)
			sprintf(key, "%lx:%lx", info->dev, info->ino);
	} else
	{
		size_t n = strlen(path), i;

		while (n > 1 && fs_is_sep(path[n - 1]))
			n--;
		key = malloc(n + 1);
		if (key != NULL)
		{
			for (i = 0; i < n; i++)
				key[i] = fs_is_sep(path[i]) ? '\\' : (char) atari_tolower((unsigned char) path[i]);
			key[n] = '\0';
		}
	}
	return key;
}

/* queue a directory unless it was seen before; -1 = out of memory */
static int push_dir(ehv_scan *s, const char *path, const pf_info *info, unsigned short depth)
{
	char *key = dir_key(path, info);
	int r;

	if (key == NULL)
		return -1;
	r = strset_add(&s->visited, key);
	free(key);
	if (r <= 0)
		return r;
	if (s->ndirs == s->cap_dirs)
	{
		unsigned long ncap = s->cap_dirs ? s->cap_dirs * 2 : 32;
		dir_item *p = realloc(s->dirs, ncap * sizeof(dir_item));

		if (p == NULL)
			return -1;
		s->dirs = p;
		s->cap_dirs = ncap;
	}
	s->dirs[s->ndirs].path = dupstr(path);
	if (s->dirs[s->ndirs].path == NULL)
		return -1;
	s->dirs[s->ndirs].depth = depth;
	s->dirs[s->ndirs].root = s->cur_root;
	s->ndirs++;
	return 0;
}

static int cmp_names(const void *a, const void *b)
{
	const char *x = *(const char *const *) a, *y = *(const char *const *) b;
	int c = atari_casecmp(x, y);

	return c != 0 ? c : strcmp(x, y);
}

/* the .REF partner of a .HYP in a sorted name list: the first name
   that is the .HYP's with ".ref" ignoring case; its place, or -1 */
static long find_ref(char **names, long n, const char *hyp)
{
	size_t stem = strlen(hyp) - 4;
	char key[260];
	long lo = 0, hi = n;

	if (stem + 5 > sizeof(key))
		return -1;
	memcpy(key, hyp, stem);
	strcpy(key + stem, ".ref");
	while (lo < hi)
	{
		long mid = lo + (hi - lo) / 2;

		if (atari_casecmp(names[mid], key) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < n && atari_casecmp(names[lo], key) == 0 ? lo : -1;
}

#define ENTRIES 4            /* folder entries looked at per step */

static void end_listing(ehv_scan *s)
{
	while (s->nsub > 0)
		free(s->subs[--s->nsub]);
	free(s->subs);
	free(s->subinfo);
	if (!s->cached)
	{
		pf_free_list(s->names, s->nnames);
		free(s->infos);
	}
	free(s->dir.path);
	s->subs = NULL;
	s->subinfo = NULL;
	s->names = NULL;
	s->infos = NULL;
	s->cached = 0;
	s->nnames = s->next_name = 0;
	s->dir.path = NULL;
	s->listing = 0;
}

/* names and infos sorted together, by name */
typedef struct {
	char *name;
	pf_info info;
} named;

static int cmp_named(const void *a, const void *b)
{
	return cmp_names(&(*(const named *const *) a)->name, &(*(const named *const *) b)->name);
}

/* sorted as pointers: the C library's qsort is quick with those */
static int sort_listing(char **names, pf_info *infos, long n)
{
	named *t, **p;
	long i;

	if (infos == NULL)
	{
		qsort(names, (size_t) n, sizeof(char *), cmp_names);
		return 0;
	}
	t = malloc((size_t) n * sizeof(named));
	p = malloc((size_t) n * sizeof(named *));
	if (t == NULL || p == NULL)
	{
		free(t);
		free(p);
		return -1;
	}
	for (i = 0; i < n; i++)
	{
		t[i].name = names[i];
		t[i].info = infos[i];
		p[i] = &t[i];
	}
	qsort(p, (size_t) n, sizeof(named *), cmp_named);
	for (i = 0; i < n; i++)
	{
		names[i] = p[i]->name;
		infos[i] = p[i]->info;
	}
	free(p);
	free(t);
	return 0;
}

/* keep a folder's listing for the building pass (checking: it may come) */
static void keep_listing(ehv_scan *s)
{
	if (s->nseen == s->cap_seen)
	{
		unsigned long ncap = s->cap_seen ? s->cap_seen * 2 : 8;
		struct seen_dir *p = realloc(s->seen, ncap * sizeof(*p));

		if (p == NULL)
			return;                  /* read again, then */
		s->seen = p;
		s->cap_seen = ncap;
	}
	if ((s->seen[s->nseen].path = dupstr(s->dir.path)) == NULL)
		return;
	s->seen[s->nseen].names = s->names;
	s->seen[s->nseen].infos = s->infos;
	s->seen[s->nseen].n = s->nnames;
	s->nseen++;
	s->cached = 1;
}

/* start reading a folder: its names now, its entries in the steps after */
static int read_dir(ehv_scan *s, dir_item *d)
{
	long n = -1;
	unsigned long i;

	set_current(s, d->path);
	s->st.dirs++;
	s->dir = *d;                     /* the path is the scan's now */
	d->path = NULL;
	s->infos = NULL;
	for (i = 0; i < s->nseen; i++)
		if (strcmp(s->seen[i].path, s->dir.path) == 0)
		{
			/* read by the checking pass */
			s->names = s->seen[i].names;
			s->infos = s->seen[i].infos;
			n = s->seen[i].n;
			s->cached = 1;
			break;
		}
	if (i == s->nseen)
	{
		n = pf_list_dir_info(s->dir.path, &s->names, &s->infos);
		if (n > 0 && sort_listing(s->names, s->infos, n) < 0)
		{
			free(s->infos);
			s->infos = NULL;
			qsort(s->names, (size_t) n, sizeof(char *), cmp_names);
		}
		if (n > 0 && s->checking)
		{
			s->nnames = n;
			keep_listing(s);
		}
	}
	if (n <= 0)
	{
		if (!s->cached)
		{
			pf_free_list(s->names, n);
			free(s->infos);
		}
		s->names = NULL;
		s->infos = NULL;
		s->cached = 0;
		free(s->dir.path);
		s->dir.path = NULL;
		return 0;                    /* unreadable folders are skipped */
	}
	s->nnames = n;
	s->subs = malloc((size_t) n * sizeof(char *));
	s->subinfo = malloc((size_t) n * sizeof(pf_info));
	s->files = calloc((size_t) n, sizeof(file_item));
	if (s->subs == NULL || s->subinfo == NULL || s->files == NULL)
		return -1;
	s->next_name = 0;
	s->nsub = 0;
	s->listing = 1;
	return 0;
}

/* some more entries of the folder being read: subfolders and hypertexts */
static int read_entries(ehv_scan *s)
{
	int k;

	for (k = 0; k < ENTRIES && s->next_name < s->nnames; k++)
	{
		long at = s->next_name++;
		const char *name = s->names[at];
		char *full = fs_join(s->dir.path, name);
		pf_info info;

		if (full == NULL)
			return -1;
		/* as the listing gave it, else looked up */
		if (s->infos != NULL)
			info = s->infos[at];
		else if (pf_stat(full, &info) != 0)
		{
			free(full);
			continue;
		}
		if (info.is_dir)
		{
			if (s->dir.depth < EHV_SCAN_MAX_DEPTH)
			{
				s->subinfo[s->nsub] = info;
				s->subs[s->nsub++] = full;
			} else
				free(full);
		} else if (fs_has_ext(name, ".hyp"))
		{
			file_item *f = &s->files[s->nfiles++];
			long ref = find_ref(s->names, s->nnames, name);

			f->path = full;
			f->info = info;
			if (ref >= 0)
			{
				if ((f->ref = fs_join(s->dir.path, s->names[ref])) == NULL)
					return -1;
				if (s->infos != NULL)
					f->rinfo = s->infos[ref];
				else if (pf_stat(f->ref, &f->rinfo) != 0)
					memset(&f->rinfo, 0, sizeof(f->rinfo));
			}
		} else
		{
			free(full);
		}
	}
	if (s->next_name < s->nnames)
		return 0;

	/* depth first, alphabetical: push in reverse */
	while (s->nsub > 0)
	{
		char *sub = s->subs[--s->nsub];
		int r;

		s->cur_root = s->dir.root;
		r = push_dir(s, sub, &s->subinfo[s->nsub], (unsigned short) (s->dir.depth + 1));
		free(sub);
		if (r < 0)
			return -1;
	}
	/* files found here belong to its root */
	s->cur_root = s->dir.root;
	end_listing(s);
	return 0;
}

static int same_ref(const ehv_guide *g, const char *ref, const pf_info *ri)
{
	if (ref == NULL || g->ref_path == NULL)
		return ref == NULL && g->ref_path == NULL;
	return strcmp(ref, g->ref_path) == 0 && ri->size == g->ref_size && ri->mtime == g->ref_mtime;
}

/* read a hypertext from disk into the index: its guide now, its names
   (and those of its .REF) in the steps after */
static int read_guide(ehv_scan *s, const file_item *f, ehv_guide *g)
{
	int err;
	hyp_doc *doc = hyp_open(f->path, &err);

	s->changed = 1;
	g->error = err;
	if (doc != NULL)
	{
		g->database = doc->database;
		g->subject = doc->subject;
		g->author = doc->author;
		g->version = doc->version;
		g->hostname = doc->hostname;
		g->default_node = doc->default_node;
		g->help_node = doc->help_node;
		g->charset = doc->charset;
		g->language = doc->language;
		g->os = doc->os;
		g->width = doc->width;
		g->stg_flags = doc->stg_flags;
	}
	s->ng = ehv_index_add_guide(s->idx, g);
	if (s->ng < 0)
	{
		hyp_close(doc);
		return -1;
	}
	if (doc == NULL)
	{
		s->st.failed++;
		return 0;
	}
	s->st.opened++;
	s->doc = doc;
	s->file = f;
	s->next = 0;
	s->part = PART_HYP;
	return 0;
}

/* some more names of the hypertext being read */
static int more_names(ehv_scan *s)
{
	unsigned int n;

	for (n = 0; n < NAMES; n++)
	{
		if (s->part == PART_HYP)
		{
			const hyp_entry *e;

			if (s->next >= s->doc->num_entries)
			{
				hyp_close(s->doc);
				s->doc = NULL;
				s->next = 0;
				s->part = s->file->ref != NULL ? PART_REF : PART_NONE;
				return 0;
			}
			e = &s->doc->entries[s->next++];
			if (!HYP_IS_TEXT(e->type) || e->name[0] == '\0')
				continue;
			if (ehv_index_add_sym(s->idx, (unsigned long) s->ng,
					e->type == HYP_NODE_POPUP ? EHV_SYM_POPUP : EHV_SYM_NODE,
					0, e->name, NULL, 0) < 0)
				return -1;
		} else if (s->part == PART_REF)
		{
			const ref_symbol *rs;

			if (s->db == NULL)
			{
				s->db = ref_open(s->file->ref, NULL);    /* a step of its own */
				if (s->db == NULL)
					s->part = PART_NONE;
				return 0;
			}
			if (s->next >= s->db->nsyms)
			{
				ref_close(s->db);
				s->db = NULL;
				s->part = PART_NONE;
				return 0;
			}
			rs = &s->db->syms[s->next++];
			if ((rs->kind != REF_ALIAS && rs->kind != REF_LABEL) || rs->node == NULL)
				continue;
			if (ehv_index_add_sym(s->idx, (unsigned long) s->ng,
					rs->kind == REF_ALIAS ? EHV_SYM_ALIAS : EHV_SYM_LABEL,
					rs->line, rs->name, rs->node, 1) < 0)
				return -1;
		} else
			return 0;
	}
	return 0;
}

static void restart(ehv_scan *s);

/* 1: f is the next hypertext of prev, unchanged */
static int same_as_next(const ehv_scan *s, const file_item *f, const pf_info *info,
                        const pf_info *rinfo, unsigned short root)
{
	const ehv_guide *old;

	if (s->nchecked >= s->prev->nguides)
		return 0;
	old = &s->prev->guides[s->nchecked];
	return strcmp(old->path, f->path) == 0 && old->size == info->size && old->mtime == info->mtime &&
	       old->root == root && same_ref(old, f->ref, rinfo);
}

/* a hypertext found: 0 = taken (its names may follow in more steps),
   1 = the scan starts again, -1 = out of memory */
static int index_file(ehv_scan *s, const file_item *f, unsigned short root)
{
	pf_info info, rinfo;
	ehv_guide g;

	set_current(s, f->path);
	info = f->info;                  /* as the folder was read */
	rinfo = f->rinfo;

	if (s->checking)
	{
		if (same_as_next(s, f, &info, &rinfo, root))
		{
			if (s->prev->guides[s->nchecked].error != HYP_OK)
				s->st.failed++;
			s->nchecked++;
			s->st.reused++;
			return 0;
		}
		restart(s);                  /* something changed: build it */
		return 1;
	}

	if (s->prev != NULL)
	{
		long pg = ehv_index_find_path(s->prev, f->path);

		if (pg >= 0)
		{
			const ehv_guide *old = &s->prev->guides[pg];

			if (old->size == info.size && old->mtime == info.mtime && same_ref(old, f->ref, &rinfo))
			{
				/* the guide; its names stay in prev's blocks, merged
				   from there when the names are sorted */
				long ng = ehv_index_add_guide(s->idx, old);

				if (ng < 0 || ehv_index_take_names(s->idx, s->prev, (unsigned long) pg, (unsigned long) ng) < 0)
					return -1;
				if (ng != pg || old->root != root)
					s->changed = 1;      /* in another place in the list */
				s->idx->guides[ng].root = root;
				s->st.reused++;
				if (old->error != HYP_OK)
					s->st.failed++;
				return 0;
			}
		}
	}

	memset(&g, 0, sizeof(g));
	g.path = f->path;
	g.size = info.size;
	g.mtime = info.mtime;
	g.root = root;
	if (f->ref != NULL)
	{
		g.ref_path = f->ref;
		g.ref_size = rinfo.size;
		g.ref_mtime = rinfo.mtime;
	}
	return read_guide(s, f, &g) < 0 ? -1 : 0;
}

/* a root's folder looked up, once a job: the building pass after a
   checking one takes what it found */
static int root_stat(ehv_scan *s, const char *path, pf_info *info)
{
	unsigned long i;
	int r;

	for (i = 0; i < s->nroots_seen; i++)
		if (strcmp(s->roots_seen[i].path, path) == 0)
		{
			*info = s->roots_seen[i].info;
			return s->roots_seen[i].found ? 0 : -1;
		}
	r = pf_stat(path, info);
	if (s->nroots_seen == s->cap_roots_seen)
	{
		unsigned long ncap = s->cap_roots_seen ? s->cap_roots_seen * 2 : 8;
		struct seen_root *p = realloc(s->roots_seen, ncap * sizeof(*p));

		if (p == NULL)
			return r;
		s->roots_seen = p;
		s->cap_roots_seen = ncap;
	}
	if ((s->roots_seen[s->nroots_seen].path = dupstr(path)) != NULL)
	{
		s->roots_seen[s->nroots_seen].found = r == 0;
		s->roots_seen[s->nroots_seen].info = *info;
		s->nroots_seen++;
	}
	return r;
}

/* start the next root: queue every existing folder it expands to */
static int next_root(ehv_scan *s)
{
	char **paths;
	unsigned int n, i;
	int rc = 0;

	s->cur_root = (unsigned short) s->next_root;
	n = fs_expand_root(s->roots[s->next_root++], &paths);
	/* reverse, so the first one is scanned first */
	for (i = n; i-- > 0 && rc == 0; )
	{
		pf_info info;

		if (root_stat(s, paths[i], &info) == 0 && info.is_dir)
			rc = push_dir(s, paths[i], &info, 0);
	}
	inf_free_list(paths, n);
	return rc;
}

int ehv_scan_step(ehv_scan *s, unsigned int budget)
{
	while (budget-- > 0)
	{
		if (ehv_index_build_busy(s->idx))
		{
			/* the names gathered put away first (index.h) */
			if (ehv_index_build_step(s->idx) < 0)
				return -1;
			continue;
		}
		if (s->part != PART_NONE)
		{
			if (more_names(s) < 0)
				return -1;
			if (s->part == PART_NONE)
				s->next_file++;
			continue;
		}
		if (s->listing)
		{
			if (read_entries(s) < 0)
				return -1;
			continue;
		}
		if (s->next_file < s->nfiles)
		{
			unsigned short root = s->cur_root;
			int r = index_file(s, &s->files[s->next_file], root);

			if (r < 0)
				return -1;
			if (r == 0 && s->part == PART_NONE)
				s->next_file++;
			continue;
		}
		free_files(s);
		if (s->ndirs > 0)
		{
			dir_item d = s->dirs[--s->ndirs];
			int r;

			s->cur_root = d.root;
			r = read_dir(s, &d);
			free(d.path);
			if (r < 0)
				return -1;
			continue;
		}
		if (s->next_root < s->nroots)
		{
			if (next_root(s) < 0)
				return -1;
			continue;
		}
		if (s->checking && s->nchecked != s->prev->nguides)
		{
			restart(s);              /* some are gone */
			continue;
		}
		set_current(s, NULL);
		return 1;
	}
	return 0;
}

int ehv_index_build_busy_scan(const ehv_scan *s)
{
	return ehv_index_build_busy(s->idx);
}

int ehv_scan_build_to(ehv_scan *s, const char *cache)
{
	return ehv_index_build_to(s->idx, cache);
}

int ehv_scan_next_is_big(const ehv_scan *s)
{
	if (ehv_index_build_busy(s->idx))
		return 1;                             /* writing names to NAME.RUN */
	if (s->part == PART_REF)
		return s->db == NULL;                 /* reading the .REF */
	if (s->part != PART_NONE || s->listing)
		return 0;
	if (s->next_file < s->nfiles)
		return !s->checking;                  /* opening a hypertext */
	return 1;                                 /* reading a folder, or a root */
}

int ehv_scan_changed(const ehv_scan *s)
{
	if (s->checking)
		return 0;                    /* all checked: as before */
	return s->changed || s->prev == NULL || s->idx->nguides != s->prev->nguides;
}

ehv_index *ehv_scan_take(ehv_scan *s)
{
	ehv_index *idx = s->idx;

	s->idx = NULL;
	ehv_scan_abort(s);
	return idx;
}

ehv_index *ehv_scan_finish(ehv_scan *s, const ref_db *allref)
{
	ehv_index *idx = ehv_scan_take(s);

	if (allref != NULL && ehv_index_merge_ref(idx, allref) < 0)
	{
		ehv_index_free(idx);
		return NULL;
	}
	if (ehv_index_finish(idx) < 0)
	{
		ehv_index_free(idx);
		return NULL;
	}
	return idx;
}

ehv_index *ehv_scan_all(const char *const *roots, unsigned int nroots,
                        const ehv_index *previous, const ref_db *allref)
{
	ehv_scan *s = ehv_scan_begin(roots, nroots, previous);
	int r;

	if (s == NULL)
		return NULL;
	while ((r = ehv_scan_step(s, 64)) == 0)
		;
	if (r < 0)
	{
		ehv_scan_abort(s);
		return NULL;
	}
	return ehv_scan_finish(s, allref);
}

/* ---------------------------------------------------------------- */

/* compare folder names ignoring case and a trailing separator */
static int same_folder(const char *a, const char *b)
{
	size_t la = strlen(a), lb = strlen(b);

	while (la > 1 && fs_is_sep(a[la - 1]))
		la--;
	while (lb > 1 && fs_is_sep(b[lb - 1]))
		lb--;
	return la == lb && atari_ncasecmp(a, b, la) == 0;
}

static int add_root(char ***list, unsigned int *n, const char *path)
{
	unsigned int i;
	char **p;

	if (path == NULL || path[0] == '\0')
		return 0;
	for (i = 0; i < *n; i++)
		if (same_folder((*list)[i], path))
			return 0;
	p = realloc(*list, (*n + 1) * sizeof(char *));
	if (p == NULL)
		return -1;
	*list = p;
	if ((p[*n] = dupstr(path)) == NULL)
		return -1;
	(*n)++;
	return 0;
}

unsigned int ehv_default_roots(const inf_file *inf, const char *own, char ***roots)
{
	char **list = NULL, **paths;
	unsigned int n = 0, i, np = inf_split_list(own, &paths);

	for (i = 0; i < np; i++)
		add_root(&list, &n, paths[i]);
	inf_free_list(paths, np);
	if (inf != NULL)
	{
		const char *ref = inf_get(inf, "REF");

		np = inf_split_list(inf_get(inf, "PATHS"), &paths);
		for (i = 0; i < np; i++)
			add_root(&list, &n, paths[i]);
		inf_free_list(paths, np);
		add_root(&list, &n, inf_get(inf, "HYPFOLD"));
		if (ref != NULL && fs_has_dir(ref))
		{
			char *dir = fs_dirname(ref);

			add_root(&list, &n, dir);
			free(dir);
		}
	}
	add_root(&list, &n, "*:\\GUIDES");
	add_root(&list, &n, "*:\\GEMSYS\\GUIDES");
	*roots = list;
	return n;
}
