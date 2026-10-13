/*
 * hypdump - inspect ST-Guide hypertexts with the EHypView core library.
 *
 *   hypdump info  FILE            header and extended headers
 *   hypdump list  FILE            index table
 *   hypdump text  FILE [NODE...]  pages as plain UTF-8 text
 *   hypdump links FILE [NODE...]  links, cross references and graphics
 *   hypdump check FILE...         decode every object, report problems
 *
 * Runs on the build host; used by the test suite.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hypfile.h"
#include "page.h"
#include "charset.h"

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
		  "       hypdump check FILE...\n", stderr);
	exit(2);
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
	usage();
	return 2;
}
