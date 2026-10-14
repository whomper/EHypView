/*
 * EHypView - reader for ST-Guide reference files (*.REF, ALL.REF).
 *
 *   "HREF"
 *   repeated modules:
 *     u32 length of the module data, u32 number of entries
 *     entries: u8 kind, u8 total length (incl. these two bytes),
 *              NUL-terminated string [, u16 line number for labels]
 *   8 zero bytes (a module header with length 0) ends the file
 */
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "plat.h"
#include "ref.h"

static unsigned long be32(const unsigned char *p)
{
	return ((unsigned long) p[0] << 24) | ((unsigned long) p[1] << 16) |
		((unsigned long) p[2] << 8) | (unsigned long) p[3];
}

/* walk the file; with db->modules == NULL only count */
static int scan(ref_db *db, unsigned long *nmod, unsigned long *nsym)
{
	unsigned long pos = 4;
	unsigned long m = 0, s = 0;

	while (pos + 8 <= db->size)
	{
		unsigned long len = be32(db->data + pos);
		unsigned long end;
		const char *node = NULL;

		pos += 8;
		if (len == 0)
			break;
		if (len > db->size - pos)
			return REF_ERR_FORMAT;
		end = pos + len;

		if (db->modules != NULL)
		{
			memset(&db->modules[m], 0, sizeof(ref_module));
			db->modules[m].first = s;
		}
		while (pos + 2 <= end)
		{
			unsigned char kind = db->data[pos];
			unsigned int elen = db->data[pos + 1];
			const char *str = (const char *) db->data + pos + 2;

			if (elen < 2 || pos + elen > end)
				return REF_ERR_FORMAT;
			/* strings must be terminated inside the entry */
			if (memchr(str, '\0', elen - 2) == NULL)
			{
				pos += elen;
				continue;
			}

			if (db->modules != NULL)
			{
				ref_module *mod = &db->modules[m];

				switch (kind)
				{
				case REF_FILE:     mod->file = str; break;
				case REF_DATABASE: mod->database = str; break;
				case REF_CHARSET:  mod->charset = str; break;
				case REF_OS:       mod->os = str[0]; break;
				case REF_TITLE:
					if (s > mod->first && db->syms[s - 1].kind == REF_NODE)
						db->syms[s - 1].title = str;
					break;
				case REF_NODE:
				case REF_ALIAS:
				case REF_LABEL:
					{
						ref_symbol *sym = &db->syms[s];
						size_t slen = strlen(str);

						if (kind == REF_NODE)
							node = str;
						sym->kind = kind;
						sym->name = str;
						sym->node = kind == REF_NODE ? str : node;
						sym->title = NULL;
						sym->module = m;
						sym->line = 0;
						if (kind == REF_LABEL && 2 + slen + 1 + 2 <= elen)
						{
							const unsigned char *l = db->data + pos + 2 + slen + 1;

							sym->line = (unsigned short) ((l[0] << 8) | l[1]);
						}
					}
					break;
				default:
					break;
				}
			}
			if (kind == REF_NODE || kind == REF_ALIAS || kind == REF_LABEL)
				s++;
			pos += elen;
		}
		if (db->modules != NULL)
			db->modules[m].count = s - db->modules[m].first;
		m++;
		pos = end;
	}
	*nmod = m;
	*nsym = s;
	return REF_OK;
}

ref_db *ref_open(const char *path, int *err)
{
	ref_db *db;
	unsigned long nmod, nsym;
	int rc, dummy;

	if (err == NULL)
		err = &dummy;
	*err = REF_OK;
	db = calloc(1, sizeof(*db));
	if (db == NULL)
	{
		*err = REF_ERR_MEMORY;
		return NULL;
	}
	db->path = malloc(strlen(path) + 1);
	if (db->path == NULL)
	{
		*err = REF_ERR_MEMORY;
		goto fail;
	}
	strcpy(db->path, path);
	db->data = (unsigned char *) pf_load(path, &db->size);
	if (db->data == NULL)
	{
		*err = REF_ERR_OPEN;
		goto fail;
	}
	if (db->size < 4 || memcmp(db->data, "HREF", 4) != 0)
	{
		*err = REF_ERR_FORMAT;
		goto fail;
	}

	/* pass 1 counts, pass 2 fills */
	rc = scan(db, &nmod, &nsym);
	if (rc != REF_OK)
	{
		*err = rc;
		goto fail;
	}
	db->modules = calloc(nmod ? nmod : 1, sizeof(ref_module));
	db->syms = calloc(nsym ? nsym : 1, sizeof(ref_symbol));
	if (db->modules == NULL || db->syms == NULL)
	{
		*err = REF_ERR_MEMORY;
		goto fail;
	}
	rc = scan(db, &db->nmodules, &db->nsyms);
	if (rc != REF_OK)
	{
		*err = rc;
		goto fail;
	}
	return db;

fail:
	ref_close(db);
	return NULL;
}

void ref_close(ref_db *db)
{
	if (db == NULL)
		return;
	free(db->path);
	free(db->data);
	free(db->modules);
	free(db->syms);
	free(db);
}

unsigned long ref_find(const ref_db *db, const char *name, unsigned long *hits, unsigned long max)
{
	unsigned long i, n = 0;
	int pass;

	for (pass = 0; pass < 2 && n == 0; pass++)
	{
		for (i = 0; i < db->nsyms; i++)
		{
			int match = pass == 0 ? strcmp(db->syms[i].name, name) == 0
				: atari_casecmp(db->syms[i].name, name) == 0;

			if (match)
			{
				if (n < max)
					hits[n] = i;
				n++;
			}
		}
	}
	return n;
}

void ref_module_filename(const ref_module *m, char *buf, unsigned long size)
{
	const char *f = m->file != NULL ? m->file : "";
	const char *base = f, *p;
	int has_ext = 0;

	if (size == 0)
		return;
	for (p = f; *p != '\0'; p++)
		if (*p == '\\' || *p == '/' || *p == ':')
			base = p + 1;
	for (p = base; *p != '\0'; p++)
		if (*p == '.')
			has_ext = 1;

	buf[0] = '\0';
	strncat(buf, f, size - 1);
	if (!has_ext && strlen(buf) + 4 < size)
		strcat(buf, ".hyp");
}
