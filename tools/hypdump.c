/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * hypdump - inspect ST-Guide hypertexts with the EHypView core library.
 *
 *   hypdump info  FILE            header and extended headers
 *   hypdump list  FILE            index table
 *   hypdump text  FILE [NODE...]  pages as plain UTF-8 text
 *   hypdump links FILE [NODE...]  links, cross references and graphics
 *   hypdump check FILE...         decode every object, report problems
 *   hypdump scan [-c CACHE] [-r ALL.REF] ROOT...
 *                                 find every hypertext below the folders
 *   hypdump resolve [-c CACHE] [-r ALL.REF] [-f OPEN.HYP] [ROOT...]
 *                   xref|name|request ARG
 *                                 show where a link or help request leads
 *
 * Runs on the build host; used by the test suite.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hypfile.h"
#include "page.h"
#include "charset.h"
#include "index.h"
#include "scan.h"
#include "resolve.h"

static void put_atari(const char *s, unsigned long len)
{
	unsigned long i;
	char buf[4];

	for (i = 0; i < len; i++)
	{
		size_t n = atari_to_utf8((unsigned char) s[i], buf);

		fwrite(buf, 1, n, stdout);
	}
}

static void put_str(const char *s)
{
	if (s != NULL)
		put_atari(s, strlen(s));
}

static const char *type_name(unsigned char t)
{
	switch (t)
	{
	case HYP_NODE_INTERNAL:    return "node";
	case HYP_NODE_POPUP:       return "popup";
	case HYP_NODE_EXTERNAL:    return "external";
	case HYP_NODE_IMAGE:       return "image";
	case HYP_NODE_SYSTEM:      return "system";
	case HYP_NODE_REXX_SCRIPT: return "rexx-script";
	case HYP_NODE_REXX_CMD:    return "rexx-cmd";
	case HYP_NODE_QUIT:        return "quit";
	case HYP_NODE_CLOSE:       return "close";
	}
	return "unknown";
}

static const char *os_name(unsigned char os)
{
	static const char *names[] = { "unknown", "Amiga", "Atari", "Mac", "Windows", "Unix", "macOS" };

	return os < sizeof(names) / sizeof(names[0]) ? names[os] : "unknown";
}

static hyp_doc *open_or_die(const char *path)
{
	int err;
	hyp_doc *doc = hyp_open(path, &err);

	if (doc == NULL)
	{
		fprintf(stderr, "hypdump: %s: %s\n", path, hyp_strerror(err));
		exit(1);
	}
	return doc;
}

static void field(const char *label, const char *value)
{
	if (value == NULL)
		return;
	printf("%-10s ", label);
	put_str(value);
	putchar('\n');
}

static int cmd_info(const char *path)
{
	hyp_doc *doc = open_or_die(path);

	printf("%-10s %s\n", "OS", os_name(doc->os));
	printf("%-10s %u\n", "Compiler", doc->compiler);
	printf("%-10s %u\n", "Entries", doc->num_entries);
	field("Database", doc->database);
	field("Default", doc->default_node);
	field("Hostname", doc->hostname);
	field("Options", doc->options);
	field("Author", doc->author);
	field("Version", doc->version);
	field("Help", doc->help_node);
	field("Subject", doc->subject);
	field("Charset", doc->charset);
	field("Language", doc->language);
	printf("%-10s %u\n", "Width", doc->width);
	printf("%-10s $%04x\n", "Flags", doc->stg_flags);
	hyp_close(doc);
	return 0;
}

static int cmd_list(const char *path)
{
	hyp_doc *doc = open_or_die(path);
	unsigned short i;

	printf("%5s %-11s %8s %7s %7s  %s\n", "#", "type", "offset", "packed", "size", "name");
	for (i = 0; i < doc->num_entries; i++)
	{
		const hyp_entry *e = &doc->entries[i];

		printf("%5u %-11s %8lu %7lu %7lu  ", i, type_name(e->type), e->offset, e->packed, e->size);
		put_str(e->name);
		putchar('\n');
	}
	hyp_close(doc);
	return 0;
}

/* collect the node numbers named on the command line, or all text nodes */
static unsigned short *select_nodes(hyp_doc *doc, int argc, char **argv, unsigned short *count)
{
	unsigned short *nodes = malloc(sizeof(unsigned short) * (doc->num_entries + (unsigned) argc + 1));
	unsigned short n = 0, i;
	int a;

	if (nodes == NULL)
		exit(1);
	if (argc == 0)
	{
		for (i = 0; i < doc->num_entries; i++)
			if (HYP_IS_TEXT(doc->entries[i].type))
				nodes[n++] = i;
	} else
	{
		for (a = 0; a < argc; a++)
		{
			i = hyp_find_node(doc, argv[a]);
			if (i == HYP_NO_NODE)
			{
				fprintf(stderr, "hypdump: no page '%s'\n", argv[a]);
				exit(1);
			}
			nodes[n++] = i;
		}
	}
	*count = n;
	return nodes;
}

static hyp_page *load_or_die(hyp_doc *doc, unsigned short num)
{
	int err;
	hyp_page *pg = hyp_page_load(doc, num, &err);

	if (pg == NULL)
	{
		fprintf(stderr, "hypdump: page %u: %s\n", num, hyp_strerror(err));
		exit(1);
	}
	return pg;
}

static int cmd_text(const char *path, int argc, char **argv)
{
	hyp_doc *doc = open_or_die(path);
	unsigned short count, k, i;
	unsigned short *nodes = select_nodes(doc, argc, argv, &count);

	for (k = 0; k < count; k++)
	{
		hyp_page *pg = load_or_die(doc, nodes[k]);

		put_str(hyp_page_title(doc, pg));
		putchar('\n');
		for (i = 0; i < pg->nxrefs; i++)
		{
			const hyp_xref *x = &pg->xrefs[i];

			fputs("See also: ", stdout);
			put_str(x->text[0] != '\0' ? x->text : hyp_link_default_text(doc, x->target));
			putchar('\n');
		}
		for (i = 0; i < pg->nlines; i++)
		{
			const hyp_line *l = &pg->lines[i];
			unsigned short r;

			for (r = 0; r < l->nruns; r++)
			{
				const hyp_run *run = &pg->runs[l->first_run + r];

				put_atari(run->text, run->len);
			}
			putchar('\n');
		}
		hyp_page_free(pg);
	}
	free(nodes);
	hyp_close(doc);
	return 0;
}

static int cmd_links(const char *path, int argc, char **argv)
{
	hyp_doc *doc = open_or_die(path);
	unsigned short count, k, i;
	unsigned short *nodes = select_nodes(doc, argc, argv, &count);

	for (k = 0; k < count; k++)
	{
		hyp_page *pg = load_or_die(doc, nodes[k]);

		printf("[");
		put_str(doc->entries[nodes[k]].name);
		printf("] lines=%u width=%u\n", pg->nlines, pg->width);
		for (i = 0; i < pg->nlines; i++)
		{
			const hyp_line *l = &pg->lines[i];
			unsigned short r;

			for (r = 0; r < l->nruns; r++)
			{
				const hyp_run *run = &pg->runs[l->first_run + r];
				const hyp_entry *t;

				if (run->kind != HYP_RUN_LINK)
					continue;
				t = run->target < doc->num_entries ? &doc->entries[run->target] : NULL;
				printf("  line %u: %s \"", i, run->link_kind == HYP_LINK_ALINK ? "alink" : "link");
				put_atari(run->text, run->len);
				printf("\" -> %s ", t ? type_name(t->type) : "INVALID");
				put_str(t ? t->name : "");
				if (run->target_line)
					printf(" @%u", run->target_line);
				putchar('\n');
			}
		}
		for (i = 0; i < pg->nxrefs; i++)
		{
			printf("  xref -> ");
			put_str(hyp_link_default_text(doc, pg->xrefs[i].target));
			printf(" \"");
			put_str(pg->xrefs[i].text);
			printf("\"\n");
		}
		for (i = 0; i < pg->ngfx; i++)
		{
			const hyp_gfx *g = &pg->gfx[i];
			static const char *names[] = { "image", "line", "box", "rbox" };

			printf("  %s x=%u y=%u w=%d h=%d", names[g->type - HYP_GFX_IMAGE], g->x, g->y, g->w, g->h);
			if (g->type == HYP_GFX_IMAGE)
			{
				printf(" image=%u%s", g->image, g->limage ? " limage" : "");
				if (g->has_dither)
					printf(" dither=$%04x", g->dithermask);
			} else if (g->type == HYP_GFX_LINE)
			{
				printf(" style=%u arrows=%u", g->style, g->arrows);
			} else
			{
				printf(" pattern=%u", g->pattern);
			}
			putchar('\n');
		}
		hyp_page_free(pg);
	}
	free(nodes);
	hyp_close(doc);
	return 0;
}

static int cmd_check(int argc, char **argv)
{
	int a, bad = 0;

	for (a = 0; a < argc; a++)
	{
		int err;
		hyp_doc *doc = hyp_open(argv[a], &err);
		unsigned short i;
		int file_bad = 0;

		if (doc == NULL)
		{
			printf("%s: %s\n", argv[a], hyp_strerror(err));
			bad++;
			continue;
		}
		for (i = 0; i < doc->num_entries; i++)
		{
			unsigned char *data;
			unsigned long size;

			if (doc->entries[i].type != HYP_NODE_IMAGE && !HYP_IS_TEXT(doc->entries[i].type))
				continue;
			err = hyp_load(doc, i, &data, &size);
			if (err == HYP_OK && HYP_IS_TEXT(doc->entries[i].type))
			{
				hyp_page *pg;

				free(data);
				pg = hyp_page_load(doc, i, &err);
				hyp_page_free(pg);
			} else
			{
				free(data);
			}
			if (err != HYP_OK)
			{
				printf("%s: entry %u: %s\n", argv[a], i, hyp_strerror(err));
				file_bad = 1;
			}
		}
		if (!file_bad)
			printf("%s: ok (%u entries)\n", argv[a], doc->num_entries);
		bad += file_bad;
		hyp_close(doc);
	}
	return bad ? 1 : 0;
}

static void usage(void)
{
	fputs("usage: hypdump info|list FILE\n"
		  "       hypdump text|links FILE [NODE...]\n"
		  "       hypdump check FILE...\n"
		  "       hypdump scan [-c CACHE] [-r ALL.REF] ROOT...\n"
		  "       hypdump resolve [-c CACHE] [-r ALL.REF] [-f OPEN.HYP] [ROOT...] xref|name|request ARG\n",
		  stderr);
	exit(2);
}

typedef struct {
	const char *cache, *allref, *current;
	char **roots;
	int nroots;
} index_opts;

/* parse -c/-r/-f; the remaining arguments are roots */
static void parse_index_opts(int argc, char **argv, index_opts *o)
{
	int i;

	memset(o, 0, sizeof(*o));
	o->roots = argv;
	for (i = 0; i < argc; i++)
	{
		if (argv[i][0] == '-' && argv[i][1] != '\0' && argv[i][2] == '\0' && i + 1 < argc)
		{
			switch (argv[i][1])
			{
			case 'c': o->cache = argv[++i]; continue;
			case 'r': o->allref = argv[++i]; continue;
			case 'f': o->current = argv[++i]; continue;
			default: usage();
			}
		}
		o->roots[o->nroots++] = argv[i];
	}
}

/*
 * The index: rescan the roots (reusing the cache) when roots are given,
 * else just load the cache.  The cache is updated after a scan.
 */
static ehv_index *get_index(const index_opts *o, int verbose)
{
	ehv_index *prev = o->cache ? ehv_index_load(o->cache) : NULL;
	ref_db *allref = NULL;
	ehv_index *idx;

	if (o->allref != NULL && (allref = ref_open(o->allref, NULL)) == NULL)
		fprintf(stderr, "hypdump: cannot read %s\n", o->allref);
	if (o->nroots == 0)
	{
		if (prev != NULL && allref != NULL)
		{
			/* merge ALL.REF into a copy of the cached index */
			unsigned long g;

			idx = ehv_index_new();
			for (g = 0; idx != NULL && g < prev->nguides; g++)
			{
				long ng = ehv_index_add_guide(idx, &prev->guides[g]);

				if (ng >= 0)
					ehv_index_take_names(idx, prev, g, (unsigned long) ng);
			}
			if (idx != NULL)
			{
				ehv_index_merge_ref(idx, allref);
				ehv_index_finish(idx);
			}
			ehv_index_free(prev);
			prev = idx;
		}
		ref_close(allref);
		return prev;
	}
	{
		ehv_scan *s = ehv_scan_begin((const char *const *) o->roots, (unsigned int) o->nroots, prev);
		const ehv_scan_status *st;
		int r;

		if (s == NULL)
			exit(1);
		while ((r = ehv_scan_step(s, 64)) == 0)
			;
		st = ehv_scan_status_get(s);
		if (verbose)
			printf("%lu folders, %lu hypertexts read, %lu from cache, %lu damaged\n",
				st->dirs, st->opened, st->reused, st->failed);
		idx = r == 1 ? ehv_scan_finish(s, allref) : NULL;
		if (r != 1)
			ehv_scan_abort(s);
	}
	ehv_index_free(prev);
	ref_close(allref);
	if (idx != NULL && o->cache != NULL && ehv_index_save(idx, o->cache) != 0)
		fprintf(stderr, "hypdump: cannot write %s\n", o->cache);
	return idx;
}

static int cmd_scan(int argc, char **argv)
{
	index_opts o;
	ehv_index *idx;
	unsigned long g, i;

	parse_index_opts(argc, argv, &o);
	idx = get_index(&o, 1);
	if (idx == NULL)
	{
		fputs("hypdump: no index\n", stderr);
		return 1;
	}
	for (g = 0; g < idx->nguides; g++)
	{
		const ehv_guide *gd = &idx->guides[g];

		printf("%s\n", gd->path);
		if (gd->error != HYP_OK)
		{
			printf("    ERROR: %s\n", hyp_strerror(gd->error));
			continue;
		}
		printf("    ");
		put_str(gd->database ? gd->database : gd->key);
		if (gd->subject)
		{
			printf("  [");
			put_str(gd->subject);
			printf("]");
		}
		printf("  %lu names%s\n", gd->nsyms, gd->ref_path ? ", own REF" : "");
	}
	for (i = 0; i < idx->norphans; i++)
		printf("ALL.REF lists a missing hypertext: %s\n", idx->orphans[i]);
	printf("%lu hypertexts, %lu names\n", idx->nguides, idx->nsyms);
	ehv_index_free(idx);
	return 0;
}

static int cmd_resolve(int argc, char **argv)
{
	index_opts o;
	ehv_index *idx;
	ehv_context ctx;
	ehv_target t;
	hyp_doc *doc = NULL;
	const char *kind, *arg;
	unsigned int i;
	static const char *status[] = { "found", "page not found (opening default page)",
		"hypertext not found", "name not found" };

	if (argc < 2)
		usage();
	kind = argv[argc - 2];
	arg = argv[argc - 1];
	parse_index_opts(argc - 2, argv, &o);
	idx = get_index(&o, 0);
	memset(&ctx, 0, sizeof(ctx));
	ctx.idx = idx;
	ctx.current = o.current;
	if (o.current != NULL)
		doc = hyp_open(o.current, NULL);
	ctx.doc = doc;

	if (strcmp(kind, "xref") == 0)
		ehv_resolve_xref(&ctx, arg, &t);
	else if (strcmp(kind, "name") == 0)
		ehv_resolve_name(&ctx, arg, &t);
	else if (strcmp(kind, "request") == 0)
		ehv_resolve_request(&ctx, arg, &t);
	else
		usage();

	if (t.status < 0)
	{
		fputs("hypdump: out of memory\n", stderr);
		return 1;
	}
	printf("status:  %s\n", status[t.status]);
	if (t.path)
		printf("file:    %s\n", t.path);
	if (t.path)
	{
		printf("page:    ");
		put_str(t.node ? t.node : "(default page)");
		if (t.line)
			printf(", line %u", t.line);
		putchar('\n');
	}
	for (i = 0; i < t.nalt; i++)
		printf("also in: %s\n", idx->guides[t.alt[i]].path);
	for (i = 0; i < t.nsuggest; i++)
	{
		printf("did you mean: ");
		if (t.status == EHV_NO_FILE)
			printf("%s", idx->guides[t.suggest[i]].path);
		else
		{
			put_str(t.similar.s[i].name);
			printf(" (%s)", idx->guides[t.similar.s[i].guide].path);
		}
		putchar('\n');
	}
	i = (unsigned int) t.status;
	ehv_target_free(&t);
	hyp_close(doc);
	ehv_index_free(idx);
	return i == EHV_FOUND ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc < 3)
		usage();
	if (strcmp(argv[1], "info") == 0)
		return cmd_info(argv[2]);
	if (strcmp(argv[1], "list") == 0)
		return cmd_list(argv[2]);
	if (strcmp(argv[1], "text") == 0)
		return cmd_text(argv[2], argc - 3, argv + 3);
	if (strcmp(argv[1], "links") == 0)
		return cmd_links(argv[2], argc - 3, argv + 3);
	if (strcmp(argv[1], "check") == 0)
		return cmd_check(argc - 2, argv + 2);
	if (strcmp(argv[1], "scan") == 0)
		return cmd_scan(argc - 2, argv + 2);
	if (strcmp(argv[1], "resolve") == 0)
		return cmd_resolve(argc - 2, argv + 2);
	usage();
	return 2;
}
