/*
 * EHypView - find every hypertext below a list of folders.
 */
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "fsys.h"
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

	strset visited;
	ehv_scan_status st;
	char *current;
};

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

/*
 * Identify a directory: by device and inode where the OS has them (this
 * also catches symbolic link loops), else by its path ignoring case.
 */
static char *dir_key(const char *path, const fs_info *info)
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
static int push_dir(ehv_scan *s, const char *path, const fs_info *info, unsigned short depth)
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

/* the .REF partner of a .HYP in a sorted name list */
static const char *find_ref(char **names, long n, const char *hyp)
{
	size_t stem = strlen(hyp) - 4;
	long i;

	for (i = 0; i < n; i++)
	{
		const char *nm = names[i];

		if (strlen(nm) == stem + 4 && atari_ncasecmp(nm, hyp, stem) == 0 &&
			atari_casecmp(nm + stem, ".ref") == 0)
			return nm;
	}
	return NULL;
}

/* read one directory: queue subfolders and hypertexts */
static int read_dir(ehv_scan *s, dir_item *d)
{
	char **names;
	long n, i;
	unsigned long nsub = 0;
	char **subs;

	set_current(s, d->path);
	s->st.dirs++;
	n = fs_list_dir(d->path, &names);
	if (n <= 0)
	{
		fs_free_list(names, n);
		return 0;                    /* unreadable folders are skipped */
	}
	qsort(names, (size_t) n, sizeof(char *), cmp_names);

	subs = malloc((size_t) n * sizeof(char *));
	s->files = calloc((size_t) n, sizeof(file_item));
	if (subs == NULL || s->files == NULL)
		goto nomem;

	for (i = 0; i < n; i++)
	{
		char *full = fs_join(d->path, names[i]);
		fs_info info;

		if (full == NULL)
			goto nomem;
		if (fs_stat(full, &info) != 0)
		{
			free(full);
			continue;
		}
		if (info.is_dir)
		{
			if (d->depth < EHV_SCAN_MAX_DEPTH)
				subs[nsub++] = full;
			else
				free(full);
		} else if (fs_has_ext(names[i], ".hyp"))
		{
			file_item *f = &s->files[s->nfiles++];
			const char *ref = find_ref(names, n, names[i]);

			f->path = full;
			if (ref != NULL && (f->ref = fs_join(d->path, ref)) == NULL)
				goto nomem;
		} else
		{
			free(full);
		}
	}

	/* depth first, alphabetical: push in reverse */
	while (nsub > 0)
	{
		char *sub = subs[--nsub];
		fs_info info;
		int r = 0;

		if (fs_stat(sub, &info) == 0)
		{
			s->cur_root = d->root;
			r = push_dir(s, sub, &info, (unsigned short) (d->depth + 1));
		}
		free(sub);
		if (r < 0)
		{
			while (nsub > 0)
				free(subs[--nsub]);
			goto nomem;
		}
	}
	free(subs);
	fs_free_list(names, n);
	return 0;

nomem:
	if (subs != NULL)
		while (nsub > 0)
			free(subs[--nsub]);
	free(subs);
	fs_free_list(names, n);
	return -1;
}

static int same_ref(const ehv_guide *g, const char *ref, const fs_info *ri)
{
	if (ref == NULL || g->ref_path == NULL)
		return ref == NULL && g->ref_path == NULL;
	return strcmp(ref, g->ref_path) == 0 && ri->size == g->ref_size && ri->mtime == g->ref_mtime;
}

/* read a hypertext (and its .REF) from disk into the index */
static int read_guide(ehv_scan *s, const file_item *f, ehv_guide *g)
{
	int err;
	hyp_doc *doc = hyp_open(f->path, &err);
	long ng;
	unsigned short i;

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
	ng = ehv_index_add_guide(s->idx, g);
	if (ng < 0)
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
	for (i = 0; i < doc->num_entries; i++)
	{
		const hyp_entry *e = &doc->entries[i];

		if (!HYP_IS_TEXT(e->type) || e->name[0] == '\0')
			continue;
		if (ehv_index_add_sym(s->idx, (unsigned long) ng,
				e->type == HYP_NODE_POPUP ? EHV_SYM_POPUP : EHV_SYM_NODE,
				0, e->name, NULL, 0) < 0)
		{
			hyp_close(doc);
			return -1;
		}
	}
	hyp_close(doc);

	if (f->ref != NULL)
	{
		ref_db *db = ref_open(f->ref, NULL);
		unsigned long k;

		if (db != NULL)
		{
			for (k = 0; k < db->nsyms; k++)
			{
				const ref_symbol *rs = &db->syms[k];
				unsigned char kind = rs->kind == REF_ALIAS ? EHV_SYM_ALIAS : EHV_SYM_LABEL;

				if ((rs->kind != REF_ALIAS && rs->kind != REF_LABEL) || rs->node == NULL)
					continue;
				if (ehv_index_add_sym(s->idx, (unsigned long) ng, kind, rs->line, rs->name, rs->node, 1) < 0)
				{
					ref_close(db);
					return -1;
				}
			}
			ref_close(db);
		}
	}
	return 0;
}

static int index_file(ehv_scan *s, const file_item *f, unsigned short root)
{
	fs_info info, rinfo;
	ehv_guide g;

	set_current(s, f->path);
	if (fs_stat(f->path, &info) != 0)
		return 0;                    /* vanished meanwhile */
	memset(&rinfo, 0, sizeof(rinfo));
	if (f->ref != NULL && fs_stat(f->ref, &rinfo) != 0)
		memset(&rinfo, 0, sizeof(rinfo));

	if (s->prev != NULL)
	{
		long pg = ehv_index_find_path(s->prev, f->path);

		if (pg >= 0)
		{
			const ehv_guide *old = &s->prev->guides[pg];

			if (old->size == info.size && old->mtime == info.mtime && same_ref(old, f->ref, &rinfo))
			{
				long ng = ehv_index_copy_guide(s->idx, s->prev, (unsigned long) pg);

				if (ng < 0)
					return -1;
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
		fs_info info;

		if (fs_stat(paths[i], &info) == 0 && info.is_dir)
			rc = push_dir(s, paths[i], &info, 0);
	}
	inf_free_list(paths, n);
	return rc;
}

int ehv_scan_step(ehv_scan *s, unsigned int budget)
{
	while (budget-- > 0)
	{
		if (s->next_file < s->nfiles)
		{
			unsigned short root = s->cur_root;

			if (index_file(s, &s->files[s->next_file], root) < 0)
				return -1;
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
			/* files found in d belong to d's root */
			s->cur_root = d.root;
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
		set_current(s, NULL);
		return 1;
	}
	return 0;
}

ehv_index *ehv_scan_finish(ehv_scan *s, const ref_db *allref)
{
	ehv_index *idx = s->idx;

	s->idx = NULL;
	ehv_scan_abort(s);
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

unsigned int ehv_default_roots(const inf_file *inf, char ***roots)
{
	char **list = NULL;
	unsigned int n = 0, i;

	if (inf != NULL)
	{
		char **paths;
		unsigned int np = inf_split_list(inf_get(inf, "PATHS"), &paths);
		const char *ref = inf_get(inf, "REF");

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
