/*
 * EHypView - turn links and help requests into "open this file at this
 * page".
 */
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "fsys.h"
#include "inf.h"
#include "ref.h"
#include "resolve.h"

static char *dupstr(const char *s)
{
	char *d;

	if (s == NULL)
		return NULL;
	d = malloc(strlen(s) + 1);
	if (d != NULL)
		strcpy(d, s);
	return d;
}

static char *dupn(const char *s, size_t n)
{
	char *d = malloc(n + 1);

	if (d != NULL)
	{
		memcpy(d, s, n);
		d[n] = '\0';
	}
	return d;
}

static void target_init(ehv_target *t)
{
	memset(t, 0, sizeof(*t));
	t->guide = -1;
}

void ehv_target_free(ehv_target *t)
{
	free(t->path);
	free(t->node);
	target_init(t);
}

static int set_str(char **dst, const char *src)
{
	free(*dst);
	*dst = dupstr(src);
	return src != NULL && *dst == NULL ? -1 : 0;
}

/* "X:" drive prefix length (2) or 0 */
static size_t drive_len(const char *p)
{
	return p[0] != '\0' && p[1] == ':' ? 2 : 0;
}

/* ---------------------------------------------------------------- */
/* finding the file                                                  */

/* name with ".hyp" added when it has no extension */
static char *with_ext(const char *file)
{
	const char *base = fs_basename(file);
	char *s;

	if (strchr(base, '.') != NULL)
		return dupstr(file);
	s = malloc(strlen(file) + 5);
	if (s != NULL)
	{
		strcpy(s, file);
		strcat(s, ".hyp");
	}
	return s;
}

static int is_file(const char *path)
{
	fs_info info;

	return fs_stat(path, &info) == 0 && !info.is_dir;
}

/* look for name (no folder) next to the open hypertext */
static char *in_current_dir(const ehv_context *ctx, const char *name)
{
	char *dir, *p;

	if (ctx->current == NULL)
		return NULL;
	dir = fs_dirname(ctx->current);
	if (dir == NULL)
		return NULL;
	p = fs_find_nocase(dir[0] != '\0' ? dir : ".", name);
	free(dir);
	return p;
}

/* index entry for a name, preferring readable files */
static long index_lookup(const ehv_context *ctx, const char *file)
{
	unsigned long hits[EHV_MAX_HITS], n, i;

	if (ctx->idx == NULL)
		return -1;
	n = ehv_index_find_guides(ctx->idx, file, hits, EHV_MAX_HITS);
	if (n > EHV_MAX_HITS)
		n = EHV_MAX_HITS;
	for (i = 0; i < n; i++)
		if (ctx->idx->guides[hits[i]].error == HYP_OK)
			return (long) hits[i];
	return n > 0 ? (long) hits[0] : -1;
}

/*
 * Locate a hypertext.  `anywhere` means the request said "*:\FILE":
 * ignore the folder, use the index first.  Sets t->path and t->guide,
 * or t->status = EHV_NO_FILE with suggestions.
 */
static int locate(const ehv_context *ctx, const char *file, int anywhere, ehv_target *t)
{
	char *name = with_ext(file);
	const char *base;
	char *found = NULL;
	long g = -1;

	if (name == NULL)
		return EHV_ERR_MEMORY;
	base = fs_basename(name);

	if (!anywhere && fs_has_dir(name) && is_file(name))
		found = dupstr(name);
	if (found == NULL && anywhere)
		g = index_lookup(ctx, base);
	if (found == NULL && g < 0)
		found = in_current_dir(ctx, base);
	if (found == NULL && g < 0)
		g = index_lookup(ctx, base);

	if (found == NULL && g >= 0)
		found = dupstr(ctx->idx->guides[g].path);
	if (found == NULL)
	{
		free(name);
		if (g >= 0)
			return EHV_ERR_MEMORY;
		t->status = EHV_NO_FILE;
		if (ctx->idx != NULL)
			t->nsuggest = (unsigned int) ehv_index_suggest_guides(ctx->idx, file, t->suggest, EHV_MAX_HITS);
		return EHV_NO_FILE;
	}
	free(name);
	free(t->path);
	t->path = found;
	t->guide = g >= 0 ? g : ctx->idx != NULL ? ehv_index_find_path(ctx->idx, found) : -1;
	return EHV_FOUND;
}

/* ---------------------------------------------------------------- */
/* finding the page inside a located file                            */

/* apply an index symbol: pages by their own name, aliases/labels by
   the page they belong to */
static int use_sym(const ehv_index *idx, unsigned long i, ehv_target *t)
{
	const ehv_sym *s = &idx->syms[i];

	if (set_str(&t->node, s->node) < 0)
		return EHV_ERR_MEMORY;
	t->line = s->kind == EHV_SYM_LABEL ? s->line : 0;
	return EHV_FOUND;
}

/* look the page up in a file that is not in the index */
static int node_from_disk(const char *path, const char *node, ehv_target *t)
{
	hyp_doc *doc = hyp_open(path, NULL);
	int rc = EHV_NO_NODE;

	if (doc == NULL)
	{
		/* let the viewer report the damaged file */
		return set_str(&t->node, node) < 0 ? EHV_ERR_MEMORY : EHV_FOUND;
	}
	{
		unsigned short n = hyp_find_node(doc, node);

		if (n != HYP_NO_NODE)
			rc = set_str(&t->node, doc->entries[n].name) < 0 ? EHV_ERR_MEMORY : EHV_FOUND;
	}
	hyp_close(doc);
	if (rc != EHV_NO_NODE)
		return rc;

	/* aliases and labels live in the .REF file next to it */
	{
		const char *base = fs_basename(path);
		size_t stem = strlen(base);
		char *dir = fs_dirname(path), *refname, *refpath = NULL;

		if (fs_has_ext(base, ".hyp"))
			stem -= 4;
		refname = malloc(stem + 5);
		if (refname != NULL && dir != NULL)
		{
			memcpy(refname, base, stem);
			strcpy(refname + stem, ".ref");
			refpath = fs_find_nocase(dir[0] != '\0' ? dir : ".", refname);
		}
		free(refname);
		free(dir);
		if (refpath != NULL)
		{
			ref_db *db = ref_open(refpath, NULL);
			unsigned long hit;

			free(refpath);
			if (db != NULL)
			{
				if (ref_find(db, node, &hit, 1) > 0 && db->syms[hit].node != NULL)
				{
					rc = set_str(&t->node, db->syms[hit].node) < 0 ? EHV_ERR_MEMORY : EHV_FOUND;
					if (db->syms[hit].kind == REF_LABEL)
						t->line = db->syms[hit].line;
				}
				ref_close(db);
			}
		}
	}
	return rc;
}

static int find_node(const ehv_context *ctx, const char *node, ehv_target *t)
{
	int rc;

	free(t->node);
	t->node = NULL;
	t->line = 0;
	if (node == NULL || node[0] == '\0')
		return t->status = EHV_FOUND;

	if (t->guide >= 0)
	{
		unsigned long hit;

		if (ehv_index_find_symbol(ctx->idx, node, t->guide, &hit, 1) > 0)
			return t->status = use_sym(ctx->idx, hit, t);
		rc = EHV_NO_NODE;
	} else
	{
		rc = node_from_disk(t->path, node, t);
		if (rc != EHV_NO_NODE)
			return t->status = rc;
	}

	/*
	 * "FILE.HYP/Main" is what HCP writes for a link to a whole file, so
	 * when there is no page called Main it means the default page.
	 */
	if (strcmp(node, "Main") == 0)
		return t->status = EHV_FOUND;

	if (t->guide >= 0)
		t->nsuggest = (unsigned int) ehv_index_suggest(ctx->idx, node, t->guide, t->suggest, EHV_MAX_HITS);
	return t->status = EHV_NO_NODE;
}

/* ---------------------------------------------------------------- */

/*
 * Split "FILE.HYP/node".  A node name may itself contain slashes, so the
 * split is right after the ".hyp" component when there is one.  Without
 * an extension: the last separator if the file allows folders in links
 * (ST-Guide flag), otherwise the first one after a drive prefix.
 * Returns 0 when there is no separator at all.
 */
static int split_xref(const char *ref, int folders, char **file, char **node)
{
	const char *p, *sep = NULL;
	size_t d = drive_len(ref);

	*file = *node = NULL;
	for (p = ref + d; *p != '\0'; p++)
	{
		if (fs_is_sep(*p) && p - ref >= 4 && atari_ncasecmp(p - 4, ".hyp", 4) == 0)
		{
			sep = p;
			break;
		}
	}
	if (sep == NULL)
	{
		p = ref + d;
		if (fs_is_sep(*p))
			p++;                     /* the "\" of "C:\" is not a split */
		for (; *p != '\0'; p++)
		{
			if (fs_is_sep(*p))
			{
				sep = p;
				if (!folders)
					break;
			}
		}
	}
	if (sep == NULL)
		return 0;
	*file = dupn(ref, (size_t) (sep - ref));
	*node = dupstr(sep + 1);
	return *file != NULL && *node != NULL ? 1 : -1;
}

int ehv_resolve_xref(const ehv_context *ctx, const char *xref, ehv_target *t)
{
	char *file, *node;
	int folders = ctx->doc != NULL && (ctx->doc->stg_flags & 0x0001);
	int r;

	target_init(t);
	r = split_xref(xref, folders, &file, &node);
	if (r < 0)
	{
		free(file);
		free(node);
		return t->status = EHV_ERR_MEMORY;
	}
	if (r == 0)
	{
		/* old compilers wrote links to whole files without "/node" */
		if (fs_has_ext(xref, ".hyp"))
		{
			if (locate(ctx, xref, 0, t) != EHV_FOUND)
				return t->status;
			return find_node(ctx, NULL, t);
		}
		return ehv_resolve_name(ctx, xref, t);
	}
	r = locate(ctx, file, 0, t);
	if (r == EHV_FOUND)
		r = find_node(ctx, node, t);
	else
		t->status = r;
	free(file);
	free(node);
	return t->status;
}

int ehv_resolve_name(const ehv_context *ctx, const char *name, ehv_target *t)
{
	unsigned long hits[EHV_MAX_HITS + 1], n, i;
	long cur = -1;

	target_init(t);
	if (name == NULL || name[0] == '\0')
		return t->status = EHV_NOT_FOUND;

	/* the open hypertext first */
	if (ctx->idx != NULL && ctx->current != NULL)
		cur = ehv_index_find_path(ctx->idx, ctx->current);
	if (ctx->doc != NULL && ctx->current != NULL)
	{
		unsigned short nn = hyp_find_node(ctx->doc, name);

		if (nn != HYP_NO_NODE)
		{
			if (set_str(&t->path, ctx->current) < 0 || set_str(&t->node, ctx->doc->entries[nn].name) < 0)
				return t->status = EHV_ERR_MEMORY;
			t->guide = cur;
			return t->status = EHV_FOUND;
		}
	}
	if (cur >= 0 && ehv_index_find_symbol(ctx->idx, name, cur, hits, 1) > 0)
	{
		if (set_str(&t->path, ctx->idx->guides[cur].path) < 0)
			return t->status = EHV_ERR_MEMORY;
		t->guide = cur;
		return t->status = use_sym(ctx->idx, hits[0], t);
	}

	if (ctx->idx == NULL)
		return t->status = EHV_NOT_FOUND;

	n = ehv_index_find_symbol(ctx->idx, name, -1, hits, EHV_MAX_HITS + 1);
	if (n == 0)
	{
		t->nsuggest = (unsigned int) ehv_index_suggest(ctx->idx, name, -1, t->suggest, EHV_MAX_HITS);
		return t->status = EHV_NOT_FOUND;
	}
	if (n > EHV_MAX_HITS + 1)
		n = EHV_MAX_HITS + 1;
	{
		const ehv_sym *s = &ctx->idx->syms[hits[0]];

		if (set_str(&t->path, ctx->idx->guides[s->guide].path) < 0)
			return t->status = EHV_ERR_MEMORY;
		t->guide = (long) s->guide;
	}
	for (i = 1; i < n; i++)
		t->alt[t->nalt++] = hits[i];
	return t->status = use_sym(ctx->idx, hits[0], t);
}

/* ---------------------------------------------------------------- */
/* help requests                                                     */

unsigned int ehv_split_args(const char *args, char ***out)
{
	char **list = NULL;
	unsigned int n = 0;
	const char *p = args;

	*out = NULL;
	if (args == NULL)
		return 0;
	for (;;)
	{
		char *arg, *o;
		char **nl;

		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '\0')
			break;
		arg = malloc(strlen(p) + 1);
		if (arg == NULL)
			break;
		o = arg;
		if (*p == '\'' || *p == '"')
		{
			char q = *p++;

			while (*p != '\0')
			{
				if (*p == q)
				{
					if (p[1] == q)
					{
						*o++ = q;     /* doubled quote */
						p += 2;
						continue;
					}
					p++;
					break;
				}
				*o++ = *p++;
			}
		} else
		{
			while (*p != '\0' && *p != ' ' && *p != '\t')
				*o++ = *p++;
		}
		*o = '\0';
		nl = realloc(list, (n + 1) * sizeof(char *));
		if (nl == NULL)
		{
			free(arg);
			break;
		}
		list = nl;
		list[n++] = arg;
	}
	*out = list;
	return n;
}

/* arguments from `first` on, joined by blanks */
static char *join(char **args, unsigned int n, unsigned int first)
{
	size_t len = 1;
	unsigned int i;
	char *s;

	if (first >= n)
		return NULL;
	for (i = first; i < n; i++)
		len += strlen(args[i]) + 1;
	s = malloc(len);
	if (s == NULL)
		return NULL;
	s[0] = '\0';
	for (i = first; i < n; i++)
	{
		if (i > first)
			strcat(s, " ");
		strcat(s, args[i]);
	}
	return s;
}

/* 'X:\FILE.REF node': find node through that REF file */
static int via_ref(const ehv_context *ctx, const char *refpath, const char *node, ehv_target *t)
{
	ref_db *db;
	unsigned long hit;
	char file[256];
	char *dir, *found = NULL;

	db = is_file(refpath) ? ref_open(refpath, NULL) : NULL;
	if (db == NULL || node == NULL || ref_find(db, node, &hit, 1) == 0 || db->syms[hit].node == NULL)
	{
		ref_close(db);
		return node != NULL ? ehv_resolve_name(ctx, node, t) : (t->status = EHV_NOT_FOUND);
	}
	ref_module_filename(&db->modules[db->syms[hit].module], file, sizeof(file));

	/* the hypertext normally sits next to its REF file */
	dir = fs_dirname(refpath);
	if (dir != NULL && !fs_has_dir(file))
		found = fs_find_nocase(dir[0] != '\0' ? dir : ".", file);
	free(dir);
	if (found != NULL)
	{
		free(t->path);
		t->path = found;
		t->guide = ctx->idx != NULL ? ehv_index_find_path(ctx->idx, found) : -1;
	} else if (locate(ctx, file, 1, t) != EHV_FOUND)
	{
		ref_close(db);
		return t->status;
	}
	if (set_str(&t->node, db->syms[hit].node) < 0)
	{
		ref_close(db);
		return t->status = EHV_ERR_MEMORY;
	}
	t->line = db->syms[hit].kind == REF_LABEL ? db->syms[hit].line : 0;
	ref_close(db);
	return t->status = EHV_FOUND;
}

int ehv_resolve_request(const ehv_context *ctx, const char *args, ehv_target *t)
{
	char **argv;
	unsigned int argc = ehv_split_args(args, &argv);
	char *node;
	int rc;

	target_init(t);
	if (argc == 0)
	{
		inf_free_list(argv, argc);
		return t->status = EHV_NOT_FOUND;
	}
	node = join(argv, argc, 1);

	if (fs_has_ext(argv[0], ".hyp"))
	{
		const char *file = argv[0];
		int anywhere = file[0] == '*' && file[1] == ':';

		if (anywhere)
			file += 2;
		rc = locate(ctx, file, anywhere, t);
		if (rc == EHV_FOUND)
			rc = find_node(ctx, node, t);
		else
			t->status = rc;
	} else if (fs_has_ext(argv[0], ".ref"))
	{
		rc = via_ref(ctx, argv[0], node, t);
	} else
	{
		/* a bare name (may contain blanks when sent unquoted) */
		char *name = join(argv, argc, 0);

		rc = name != NULL ? ehv_resolve_name(ctx, name, t) : (t->status = EHV_ERR_MEMORY);
		free(name);
	}
	free(node);
	inf_free_list(argv, argc);
	return rc;
}
