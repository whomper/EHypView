/*
 * EHypView - reader for ST-Guide hypertext files (*.HYP).
 *
 * File layout (all multi-byte values big-endian unless noted):
 *
 *   header      "HDOC", u32 index size, u16 entry count,
 *               u8 compiler version, u8 operating system
 *   index       one variable-length record per entry:
 *                 u8 record length, u8 type, u32 offset, u16 comp_diff,
 *                 u16 next, u16 prev, u16 toc, NUL-terminated name
 *   ext headers u16 id, u16 length, data ... until id 0
 *   data        the (normally LH5 compressed) objects
 */
#include <stdlib.h>
#include <string.h>
#include "hypfile.h"
#include "lh5.h"

#define HEADER_SIZE   12
#define ENTRY_FIXED   14
#define MAX_OBJECT    (16UL * 1024UL * 1024UL)   /* sanity limit */

#define EXT_EOF       0
#define EXT_DATABASE  1
#define EXT_DEFAULT   2
#define EXT_HOSTNAME  3
#define EXT_OPTIONS   4
#define EXT_AUTHOR    5
#define EXT_VERSION   6
#define EXT_HELP      7
#define EXT_SUBJECT   8
#define EXT_TREE      9
#define EXT_FLAGS     10
#define EXT_WIDTH     11
#define EXT_CHARSET   30
#define EXT_LANGUAGE  31

static unsigned long be32(const unsigned char *p)
{
	return ((unsigned long) p[0] << 24) | ((unsigned long) p[1] << 16) |
		((unsigned long) p[2] << 8) | (unsigned long) p[3];
}

static unsigned short be16(const unsigned char *p)
{
	return (unsigned short) ((p[0] << 8) | p[1]);
}

static char *dupstr(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = malloc(n);

	if (d != NULL)
		memcpy(d, s, n);
	return d;
}

const char *hyp_strerror(int err)
{
	switch (err)
	{
	case HYP_OK:         return "no error";
	case HYP_ERR_OPEN:   return "cannot read file";
	case HYP_ERR_FORMAT: return "not an ST-Guide hypertext, or damaged";
	case HYP_ERR_MEMORY: return "out of memory";
	case HYP_ERR_RANGE:  return "no such page";
	case HYP_ERR_DECODE: return "page data is damaged";
	}
	return "unknown error";
}

/* read a counted string from an extended header, always NUL-terminated */
static char *read_ext_string(FILE *fp, unsigned int len)
{
	char *s = malloc(len + 1);

	if (s == NULL)
		return NULL;
	if (len > 0 && fread(s, 1, len, fp) != len)
	{
		free(s);
		return NULL;
	}
	s[len] = '\0';
	return s;
}

/*
 * Extended headers sit between the index and the first object.  Old
 * files have none, so never read past `limit` (the start of the data).
 */
static int read_ext_headers(hyp_doc *doc, FILE *fp, unsigned long pos, unsigned long limit)
{
	for (;;)
	{
		unsigned char h[4];
		unsigned int id, len;
		char **target = NULL;

		if (pos + 4 > limit || fread(h, 1, 4, fp) != 4)
			return HYP_OK;
		id = be16(h);
		len = be16(h + 2);
		if (id == EXT_EOF)
			return HYP_OK;
		pos += 4;
		if (len > limit - pos)
			return HYP_OK;           /* not a header after all */
		pos += len;

		switch (id)
		{
		case EXT_DATABASE: target = &doc->database; break;
		case EXT_DEFAULT:  target = &doc->default_node; break;
		case EXT_HOSTNAME: target = &doc->hostname; break;
		case EXT_OPTIONS:  target = &doc->options; break;
		case EXT_AUTHOR:   target = &doc->author; break;
		case EXT_VERSION:  target = &doc->version; break;
		case EXT_HELP:     target = &doc->help_node; break;
		case EXT_SUBJECT:  target = &doc->subject; break;
		case EXT_CHARSET:  target = &doc->charset; break;
		case EXT_LANGUAGE: target = &doc->language; break;
		case EXT_FLAGS:
		case EXT_WIDTH:
			if (len == 2)
			{
				/* HCP only fills the first byte, so read little-endian */
				unsigned char v[2];
				unsigned short val;

				if (fread(v, 1, 2, fp) != 2)
					return HYP_ERR_FORMAT;
				val = (unsigned short) (v[0] | (v[1] << 8));
				if (id == EXT_FLAGS)
					doc->stg_flags = val;
				else
					doc->width = val;
				continue;
			}
			break;
		default:
			break;                   /* unknown headers are skipped */
		}

		if (target != NULL)
		{
			char *s = read_ext_string(fp, len);

			if (s == NULL)
				return HYP_ERR_FORMAT;
			free(*target);           /* a repeated header wins */
			*target = s;
		} else if (fseek(fp, (long) len, SEEK_CUR) != 0)
		{
			return HYP_ERR_FORMAT;
		}
	}
}

static int parse_index(hyp_doc *doc, unsigned char *tab, unsigned long tabsize,
                       unsigned short count)
{
	unsigned long pos = 0;
	unsigned short i, n;
	unsigned long end_offset;

	doc->entries = calloc(count ? count : 1, sizeof(hyp_entry));
	if (doc->entries == NULL)
		return HYP_ERR_MEMORY;

	for (i = 0; i < count; i++)
	{
		hyp_entry *e = &doc->entries[i];
		unsigned int len;
		const unsigned char *p;

		if (pos + ENTRY_FIXED > tabsize)
			return HYP_ERR_FORMAT;
		p = tab + pos;
		len = p[0];
		if (len < ENTRY_FIXED || pos + len > tabsize)
			return HYP_ERR_FORMAT;

		e->type = p[1];
		e->offset = be32(p + 2);
		e->size = be16(p + 6);      /* comp_diff for now */
		e->next = be16(p + 8);
		e->prev = be16(p + 10);
		e->toc = be16(p + 12);
		e->name = (const char *) p + ENTRY_FIXED;
		pos += len;
	}
	/* names are NUL-terminated in the file; make sure the last one is */
	tab[tabsize] = '\0';

	/* a trailing EOF entry only marks where the data ends */
	n = count;
	end_offset = doc->filesize;
	if (n > 0 && doc->entries[n - 1].type == HYP_NODE_EOF)
	{
		n--;
		end_offset = doc->entries[n].offset;
	}
	doc->num_entries = n;

	for (i = 0; i < n; i++)
	{
		hyp_entry *e = &doc->entries[i];
		unsigned long next_off = i + 1 < n ? doc->entries[i + 1].offset : end_offset;
		unsigned long diff = e->size;

		if (next_off < e->offset || next_off > doc->filesize)
			return HYP_ERR_FORMAT;
		e->packed = next_off - e->offset;
		e->size = e->packed + diff;
		if (e->type == HYP_NODE_IMAGE)
		{
			/* images may exceed 64 KB: 'next' holds the high bits */
			e->size += (unsigned long) e->next << 16;
			e->next = e->prev = e->toc = HYP_NO_NODE;
		}
	}
	return HYP_OK;
}

hyp_doc *hyp_open(const char *path, int *err)
{
	FILE *fp;
	hyp_doc *doc;
	unsigned char head[HEADER_SIZE];
	unsigned long tabsize;
	unsigned short count;
	int rc = HYP_ERR_FORMAT;

	if (err != NULL)
		*err = HYP_OK;
	fp = fopen(path, "rb");
	if (fp == NULL)
	{
		if (err != NULL)
			*err = HYP_ERR_OPEN;
		return NULL;
	}

	doc = calloc(1, sizeof(*doc));
	if (doc == NULL || (doc->path = dupstr(path)) == NULL)
	{
		rc = HYP_ERR_MEMORY;
		goto fail;
	}

	if (fseek(fp, 0L, SEEK_END) != 0)
	{
		rc = HYP_ERR_OPEN;
		goto fail;
	}
	doc->filesize = (unsigned long) ftell(fp);
	rewind(fp);

	if (fread(head, 1, HEADER_SIZE, fp) != HEADER_SIZE ||
		memcmp(head, "HDOC", 4) != 0)
		goto fail;
	tabsize = be32(head + 4);
	count = be16(head + 8);
	doc->compiler = head[10];
	doc->os = head[11];
	if (tabsize > doc->filesize - HEADER_SIZE)
		goto fail;

	doc->names = malloc(tabsize + 1);
	if (doc->names == NULL)
	{
		rc = HYP_ERR_MEMORY;
		goto fail;
	}
	if (fread(doc->names, 1, tabsize, fp) != tabsize)
		goto fail;
	rc = parse_index(doc, (unsigned char *) doc->names, tabsize, count);
	if (rc != HYP_OK)
		goto fail;
	{
		unsigned long start = HEADER_SIZE + tabsize;
		unsigned long limit = doc->filesize;
		unsigned short i;

		/* data begins at the lowest object offset */
		for (i = 0; i < count; i++)
			if (doc->entries[i].offset >= start && doc->entries[i].offset < limit)
				limit = doc->entries[i].offset;
		rc = read_ext_headers(doc, fp, start, limit);
		if (rc != HYP_OK)
			goto fail;
	}

	fclose(fp);
	return doc;

fail:
	fclose(fp);
	hyp_close(doc);
	if (err != NULL)
		*err = rc;
	return NULL;
}

void hyp_close(hyp_doc *doc)
{
	if (doc == NULL)
		return;
	free(doc->path);
	free(doc->entries);
	free(doc->names);
	free(doc->database);
	free(doc->default_node);
	free(doc->hostname);
	free(doc->options);
	free(doc->author);
	free(doc->version);
	free(doc->help_node);
	free(doc->subject);
	free(doc->charset);
	free(doc->language);
	free(doc);
}

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int namecasecmp(const char *a, const char *b)
{
	while (*a != '\0' && lower((unsigned char) *a) == lower((unsigned char) *b))
	{
		a++;
		b++;
	}
	return lower((unsigned char) *a) - lower((unsigned char) *b);
}

unsigned short hyp_find_node(const hyp_doc *doc, const char *name)
{
	unsigned short i;

	if (name == NULL)
		return HYP_NO_NODE;
	for (i = 0; i < doc->num_entries; i++)
		if (HYP_IS_TEXT(doc->entries[i].type) && strcmp(doc->entries[i].name, name) == 0)
			return i;
	for (i = 0; i < doc->num_entries; i++)
		if (HYP_IS_TEXT(doc->entries[i].type) && namecasecmp(doc->entries[i].name, name) == 0)
			return i;
	return HYP_NO_NODE;
}

unsigned short hyp_default_node(const hyp_doc *doc)
{
	unsigned short i;

	i = hyp_find_node(doc, doc->default_node);
	if (i != HYP_NO_NODE)
		return i;
	i = hyp_find_node(doc, "Main");
	if (i != HYP_NO_NODE)
		return i;
	for (i = 0; i < doc->num_entries; i++)
		if (doc->entries[i].type == HYP_NODE_INTERNAL)
			return i;
	return HYP_NO_NODE;
}

int hyp_load(hyp_doc *doc, unsigned short num, unsigned char **data, unsigned long *size)
{
	const hyp_entry *e;
	FILE *fp;
	unsigned char *packed = NULL, *out;
	int rc = HYP_OK;

	*data = NULL;
	*size = 0;
	if (num >= doc->num_entries)
		return HYP_ERR_RANGE;
	e = &doc->entries[num];
	if (e->size > MAX_OBJECT || e->packed > e->size)
		return HYP_ERR_FORMAT;

	out = malloc(e->size + 1);
	if (out == NULL)
		return HYP_ERR_MEMORY;

	fp = fopen(doc->path, "rb");
	if (fp == NULL)
	{
		free(out);
		return HYP_ERR_OPEN;
	}

	if (e->packed == e->size)
	{
		/* stored uncompressed */
		if (fseek(fp, (long) e->offset, SEEK_SET) != 0 ||
			fread(out, 1, e->size, fp) != e->size)
			rc = HYP_ERR_OPEN;
	} else
	{
		packed = malloc(e->packed ? e->packed : 1);
		if (packed == NULL)
			rc = HYP_ERR_MEMORY;
		else if (fseek(fp, (long) e->offset, SEEK_SET) != 0 ||
			fread(packed, 1, e->packed, fp) != e->packed)
			rc = HYP_ERR_OPEN;
		else if (lh5_decode(packed, e->packed, out, e->size) != 0)
			rc = HYP_ERR_DECODE;
		free(packed);
	}
	fclose(fp);

	if (rc != HYP_OK)
	{
		free(out);
		return rc;
	}

	/* "encrypted" files are XORed with 127 after decompression */
	if (doc->stg_flags & HYP_STG_ENCRYPTED)
	{
		unsigned long i;

		for (i = 0; i < e->size; i++)
			out[i] ^= 127;
	}
	out[e->size] = '\0';
	*data = out;
	*size = e->size;
	return HYP_OK;
}
