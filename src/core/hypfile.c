/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
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
#include "charset.h"
#include "hypfile.h"
#include "lh5.h"
#include "plat.h"

#define HEADER_SIZE   12
#define ENTRY_FIXED   14
#define MAX_OBJECT    (16UL * 1024UL * 1024UL)   /* sanity limit */
#define MAX_EXT_HEADERS 65536UL                   /* sanity limit */

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

/*
 * Extended headers sit between the index and the first object; `p` holds
 * exactly that region.  Old files have none.
 */
static int parse_ext_headers(hyp_doc *doc, const unsigned char *p, unsigned long size)
{
	unsigned long pos = 0;

	while (pos + 4 <= size)
	{
		unsigned int id = be16(p + pos);
		unsigned int len = be16(p + pos + 2);
		const unsigned char *data = p + pos + 4;
		char **target = NULL;

		if (id == EXT_EOF)
			break;
		pos += 4;
		if (len > size - pos)
			break;                   /* not a header after all */
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
				unsigned short val = (unsigned short) (data[0] | (data[1] << 8));

				if (id == EXT_FLAGS)
					doc->stg_flags = val;
				else
					doc->width = val;
			}
			break;
		default:
			break;                   /* unknown headers are skipped */
		}

		if (target != NULL)
		{
			char *str = malloc(len + 1);

			if (str == NULL)
				return HYP_ERR_MEMORY;
			memcpy(str, data, len);
			str[len] = '\0';
			free(*target);           /* a repeated header wins */
			*target = str;
		}
	}
	return HYP_OK;
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

static int recent_on;
static hyp_doc *recent;              /* the last one closed (hyp_recent) */

void hyp_recent(int on)
{
	hyp_forget();
	recent_on = on;
}

static void hyp_free(hyp_doc *doc);

void hyp_forget(void)
{
	hyp_free(recent);
	recent = NULL;
}

static hyp_doc *open_doc(const char *path, int *err, int hold)
{
	int h;
	pf_info info;
	hyp_doc *doc;
	unsigned char head[HEADER_SIZE];
	unsigned long tabsize;
	unsigned short count;
	int rc = HYP_ERR_FORMAT;

	if (err != NULL)
		*err = HYP_OK;
	if (recent != NULL && strcmp(recent->path, path) == 0)
	{
		doc = recent;                /* closed a moment ago: as it was */
		recent = NULL;
		if (!hold)
			hyp_let_go(doc);
		else if (doc->h < 0)
			doc->h = pf_open(path, PF_READ);   /* -1: hyp_load() tries again */
		return doc;
	}
	/* opened straight away: the size comes with it, no look at the folder */
	if ((h = pf_open(path, PF_READ)) < 0 || pf_finfo(h, &info) != 0 || info.is_dir)
	{
		if (h >= 0)
			pf_close(h);
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
	doc->h = -1;
	doc->filesize = info.size;

	if (doc->filesize < HEADER_SIZE || pf_read_at(h, 0, head, HEADER_SIZE) != 0 ||
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
	if (pf_read_at(h, HEADER_SIZE, doc->names, tabsize) != 0)
		goto fail;
	rc = parse_index(doc, (unsigned char *) doc->names, tabsize, count);
	if (rc != HYP_OK)
		goto fail;
	{
		unsigned long start = HEADER_SIZE + tabsize;
		unsigned long limit = doc->filesize;
		unsigned short i;
		unsigned char *ext;

		/* data begins at the lowest object offset */
		for (i = 0; i < count; i++)
			if (doc->entries[i].offset >= start && doc->entries[i].offset < limit)
				limit = doc->entries[i].offset;
		if (limit - start > MAX_EXT_HEADERS)
			limit = start + MAX_EXT_HEADERS;
		ext = malloc(limit - start + 1);
		if (ext == NULL)
		{
			rc = HYP_ERR_MEMORY;
			goto fail;
		}
		rc = pf_read_at(h, start, ext, limit - start) != 0 ? HYP_ERR_FORMAT
			: parse_ext_headers(doc, ext, limit - start);
		free(ext);
		if (rc != HYP_OK)
			goto fail;
	}

	if (hold)
		doc->h = h;
	else
		pf_close(h);
	return doc;

fail:
	pf_close(h);
	if (doc != NULL)
		doc->h = -1;
	hyp_free(doc);
	if (err != NULL)
		*err = rc;
	return NULL;
}

hyp_doc *hyp_open(const char *path, int *err)
{
	return open_doc(path, err, 0);
}

hyp_doc *hyp_open_held(const char *path, int *err)
{
	return open_doc(path, err, 1);
}

void hyp_let_go(hyp_doc *doc)
{
	if (doc != NULL && doc->h >= 0)
	{
		pf_close(doc->h);
		doc->h = -1;
	}
}

int hyp_hold(hyp_doc *doc)
{
	if (doc->h < 0)
		doc->h = pf_open(doc->path, PF_READ);
	return doc->h >= 0 ? 0 : -1;
}

void hyp_keep(hyp_doc *doc)
{
	if (doc == NULL)
		return;
	if (!recent_on)
	{
		hyp_free(doc);
		return;
	}
	hyp_free(recent);
	recent = doc;
}

void hyp_recent_let_go(void)
{
	hyp_let_go(recent);
}

void hyp_close(hyp_doc *doc)
{
	if (doc == NULL)
		return;
	hyp_let_go(doc);
	if (recent_on)
	{
		hyp_free(recent);
		recent = doc;
		return;
	}
	hyp_free(doc);
}

static void hyp_free(hyp_doc *doc)
{
	if (doc == NULL)
		return;
	hyp_let_go(doc);
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

unsigned short hyp_find_node(const hyp_doc *doc, const char *name)
{
	unsigned short i;

	if (name == NULL)
		return HYP_NO_NODE;
	for (i = 0; i < doc->num_entries; i++)
		if (HYP_IS_TEXT(doc->entries[i].type) && strcmp(doc->entries[i].name, name) == 0)
			return i;
	for (i = 0; i < doc->num_entries; i++)
		if (HYP_IS_TEXT(doc->entries[i].type) && atari_casecmp(doc->entries[i].name, name) == 0)
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
	int h;
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

	/* the file as held (hyp_open_held), else opened for the object */
	h = doc->h >= 0 ? doc->h : pf_open(doc->path, PF_READ);
	if (h < 0)
	{
		free(out);
		return HYP_ERR_OPEN;
	}

	if (e->packed == e->size)
	{
		/* stored uncompressed */
		if (pf_read_at(h, e->offset, out, e->size) != 0)
			rc = HYP_ERR_OPEN;
	} else
	{
		packed = malloc(e->packed ? e->packed : 1);
		if (packed == NULL)
			rc = HYP_ERR_MEMORY;
		else if (pf_read_at(h, e->offset, packed, e->packed) != 0)
			rc = HYP_ERR_OPEN;
		else if (lh5_decode(packed, e->packed, out, e->size) != 0)
			rc = HYP_ERR_DECODE;
		free(packed);
	}
	if (h != doc->h)
		pf_close(h);

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

/* ---------------------------------------------------------------- */
/* an object unpacked a piece at a time (hypfile.h)                  */

struct hyp_unpack {
	unsigned char *packed, *out;   /* out: made at the first piece */
	unsigned long packed_size, size, ready;
	lh5_state *lh5;
	int stored;                    /* kept as it is: packed is the object */
	int xor;                       /* "encrypted": XORed with 127 */
};

void hyp_unpack_free(hyp_unpack *u)
{
	if (u == NULL)
		return;
	if (u->lh5 != NULL)
		lh5_end(u->lh5);
	free(u->packed);
	free(u->out);
	free(u);
}

hyp_unpack *hyp_unpack_begin(hyp_doc *doc, unsigned short num, int *rc)
{
	const hyp_entry *e;
	hyp_unpack *u;
	int h, r;

	*rc = HYP_ERR_RANGE;
	if (num >= doc->num_entries)
		return NULL;
	e = &doc->entries[num];
	*rc = HYP_ERR_FORMAT;
	if (e->size > MAX_OBJECT || e->packed > e->size)
		return NULL;
	*rc = HYP_ERR_MEMORY;
	if ((u = calloc(1, sizeof(*u))) == NULL)
		return NULL;
	u->size = e->size;
	u->packed_size = e->packed;
	u->stored = e->packed == e->size;
	u->xor = (doc->stg_flags & HYP_STG_ENCRYPTED) != 0;
	/* only the packed bytes for now: the room for the whole when it is
	   unpacked (a page's pictures wait their turn, one at a time) */
	if ((u->packed = malloc(e->packed + 1)) == NULL)
	{
		hyp_unpack_free(u);
		return NULL;
	}
	/* the file as held (hyp_open_held), else opened for the object */
	h = doc->h >= 0 ? doc->h : pf_open(doc->path, PF_READ);
	*rc = HYP_ERR_OPEN;
	if (h < 0)
	{
		hyp_unpack_free(u);
		return NULL;
	}
	r = pf_read_at(h, e->offset, u->packed, e->packed);
	if (h != doc->h)
		pf_close(h);
	if (r != 0)
	{
		hyp_unpack_free(u);
		return NULL;
	}
	*rc = HYP_OK;
	return u;
}

int hyp_unpack_head(const hyp_unpack *u, unsigned char *buf, unsigned long n)
{
	unsigned long i, out;
	lh5_state *s;
	int r;

	if (n > u->size)
		return HYP_ERR_FORMAT;
	if (u->stored)
		memcpy(buf, u->packed, n);
	else
	{
		/* the first bytes alone, on a decoder of their own */
		if ((s = lh5_begin(u->packed, u->packed_size, buf, n)) == NULL)
			return HYP_ERR_MEMORY;
		r = lh5_more(s, n, &out);
		lh5_end(s);
		if (r != 1)
			return HYP_ERR_DECODE;
	}
	for (i = 0; u->xor && i < n; i++)
		buf[i] ^= 127;
	return HYP_OK;
}

int hyp_unpack_more(hyp_unpack *u, unsigned long budget)
{
	unsigned long was = u->ready, i;
	int r;

	if (u->ready == u->size && u->out != NULL)
		return 1;
	if (u->out == NULL)
	{
		if (u->stored)
		{
			/* the packed bytes are the object */
			u->out = u->packed;
			u->packed = NULL;
			u->ready = u->size;
			for (i = 0; u->xor && i < u->size; i++)
				u->out[i] ^= 127;
			return 1;
		}
		if ((u->out = malloc(u->size + 1)) == NULL ||
		    (u->lh5 = lh5_begin(u->packed, u->packed_size, u->out, u->size)) == NULL)
			return HYP_ERR_MEMORY;
	}
	r = lh5_more(u->lh5, budget, &u->ready);
	if (r < 0)
		return HYP_ERR_DECODE;
	for (i = was; u->xor && i < u->ready; i++)
		u->out[i] ^= 127;
	if (r == 1)
	{
		lh5_end(u->lh5);
		u->lh5 = NULL;
		free(u->packed);
		u->packed = NULL;
	}
	return r;
}

const unsigned char *hyp_unpack_out(const hyp_unpack *u, unsigned long *ready)
{
	*ready = u->out != NULL ? u->ready : 0;
	return u->out;
}

unsigned char *hyp_unpack_take(hyp_unpack *u, unsigned long *size)
{
	unsigned char *out;

	if (u->out == NULL || u->lh5 != NULL || u->ready < u->size)
		return NULL;
	out = u->out;
	out[u->size] = '\0';
	*size = u->size;
	u->out = NULL;
	hyp_unpack_free(u);
	return out;
}
