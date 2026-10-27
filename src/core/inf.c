/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - reader for ST-GUIDE.INF style configuration files.
 */
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "inf.h"
#include "plat.h"

static int is_blank(char c)
{
	return c == ' ' || c == '\t';
}

/* trim blanks in place, return the new start */
static char *trim(char *s)
{
	char *e;

	while (is_blank(*s))
		s++;
	e = s + strlen(s);
	while (e > s && is_blank(e[-1]))
		*--e = '\0';
	return s;
}

inf_file *inf_parse(const char *text, int *err)
{
	inf_file *inf;
	char *line, *p;
	unsigned int maxpairs = 1;
	int dummy;

	if (err == NULL)
		err = &dummy;
	*err = INF_OK;
	inf = calloc(1, sizeof(*inf));
	if (inf == NULL || (inf->buf = malloc(strlen(text) + 1)) == NULL)
	{
		free(inf);
		*err = INF_ERR_MEMORY;
		return NULL;
	}
	strcpy(inf->buf, text);
	for (p = inf->buf; *p != '\0'; p++)
		if (*p == '\n')
			maxpairs++;
	inf->pairs = calloc(maxpairs, sizeof(inf_pair));
	if (inf->pairs == NULL)
	{
		inf_free(inf);
		*err = INF_ERR_MEMORY;
		return NULL;
	}

	line = inf->buf;
	while (line != NULL)
	{
		char *next = strchr(line, '\n');
		char *eq;

		if (next != NULL)
			*next++ = '\0';
		p = line + strlen(line);
		if (p > line && p[-1] == '\r')
			p[-1] = '\0';
		line = trim(line);
		eq = strchr(line, '=');
		if (*line != '#' && *line != ';' && eq != NULL)
		{
			*eq = '\0';
			inf->pairs[inf->npairs].key = trim(line);
			inf->pairs[inf->npairs].value = trim(eq + 1);
			if (inf->pairs[inf->npairs].key[0] != '\0')
				inf->npairs++;
		}
		line = next;
	}
	return inf;
}

inf_file *inf_load(const char *path, int *err)
{
	char *text = pf_load(path, NULL);
	inf_file *inf;

	if (text == NULL)
	{
		if (err != NULL)
			*err = INF_ERR_OPEN;
		return NULL;
	}
	inf = inf_parse(text, err);
	free(text);
	return inf;
}

void inf_free(inf_file *inf)
{
	if (inf == NULL)
		return;
	free(inf->buf);
	free(inf->pairs);
	free(inf);
}

const char *inf_get(const inf_file *inf, const char *key)
{
	unsigned int i = inf->npairs;

	while (i-- > 0)
		if (atari_casecmp(inf->pairs[i].key, key) == 0)
			return inf->pairs[i].value;
	return NULL;
}

/* the line at p (to its '\n') sets key */
static int sets(const char *p, const char *key)
{
	char k[40];
	size_t i = 0;

	while (is_blank(*p))
		p++;
	while (*p != '\0' && *p != '=' && *p != '\n' && i < sizeof(k) - 1)
		k[i++] = *p++;
	if (*p != '=')
		return 0;
	while (i > 0 && is_blank(k[i - 1]))
		i--;
	k[i] = '\0';
	return k[0] != ';' && k[0] != '#' && atari_casecmp(k, key) == 0;
}

char *inf_put(const char *text, const char *key, const char *value, const char *note)
{
	size_t tl = text ? strlen(text) : 0, kl = strlen(key), vl = strlen(value);
	size_t nl = note ? strlen(note) : 0, lines = 1, size;
	const char *p;
	char *out, *o;
	int found = 0;

	for (p = text; p && *p; p++)
		if (*p == '\n')
			lines++;
	size = tl + lines * (kl + vl + 5) + nl + kl + vl + 16;
	out = malloc(size);
	if (out == NULL)
		return NULL;
	o = out;
	for (p = text; p && *p;)
	{
		const char *e = strchr(p, '\n');
		size_t len = e ? (size_t) (e - p) : strlen(p);

		if (sets(p, key))
		{
			found = 1;
			memcpy(o, key, kl);
			o += kl;
			*o++ = '=';
			memcpy(o, value, vl);
			o += vl;
			*o++ = '\r';
			*o++ = '\n';
		} else
		{
			memcpy(o, p, len);
			o += len;
			if (e != NULL || len > 0)
			{
				/* every line ends CR LF, the last one too */
				if (o > out && o[-1] != '\r')
					*o++ = '\r';
				*o++ = '\n';
			}
		}
		p += len + (e != NULL);
	}
	if (!found)
	{
		if (note != NULL)
		{
			*o++ = ';';
			*o++ = ' ';
			memcpy(o, note, nl);
			o += nl;
			*o++ = '\r';
			*o++ = '\n';
		}
		memcpy(o, key, kl);
		o += kl;
		*o++ = '=';
		memcpy(o, value, vl);
		o += vl;
		memcpy(o, "\r\n\r\n", 4);
		o += 4;
	}
	*o = '\0';
	return out;
}

char *inf_drop(const char *text, const char *key)
{
	size_t tl = strlen(text);
	const char *p = text, *note = NULL;
	char *out = malloc(tl + 1), *o = out;

	if (out == NULL)
		return NULL;
	while (*p)
	{
		const char *e = strchr(p, '\n'), *next;
		size_t len = e ? (size_t) (e - p) + 1 : strlen(p);

		next = p + len;
		if (*p == ';')
		{
			if (note == NULL)
				note = o;          /* where the notes above a line start */
		} else if (sets(p, key))
		{
			if (note != NULL)
				o = (char *) note; /* its notes go with it */
			note = NULL;
			if (*next == '\n' || (next[0] == '\r' && next[1] == '\n'))
				next += *next == '\n' ? 1 : 2;	/* and the empty line after it */
			p = next;
			continue;
		} else
			note = NULL;
		memcpy(o, p, len);
		o += len;
		p = next;
	}
	*o = '\0';
	return out;
}

unsigned int inf_split_list(const char *value, char ***out)
{
	unsigned int n = 0, max = 1;
	const char *p;
	char **list;

	*out = NULL;
	if (value == NULL)
		return 0;
	for (p = value; *p != '\0'; p++)
		if (*p == ';' || *p == ',')
			max++;
	list = calloc(max, sizeof(char *));
	if (list == NULL)
		return 0;

	p = value;
	for (;;)
	{
		const char *e = p;
		char *item;

		while (*e != '\0' && *e != ';' && *e != ',')
			e++;
		item = malloc((size_t) (e - p) + 1);
		if (item == NULL)
		{
			inf_free_list(list, n);
			return 0;
		}
		memcpy(item, p, (size_t) (e - p));
		item[e - p] = '\0';
		{
			char *t = trim(item);

			if (*t != '\0')
			{
				memmove(item, t, strlen(t) + 1);
				list[n++] = item;
			} else
			{
				free(item);
			}
		}
		if (*e == '\0')
			break;
		p = e + 1;
	}
	if (n == 0)
	{
		free(list);
		return 0;
	}
	*out = list;
	return n;
}

void inf_free_list(char **list, unsigned int n)
{
	unsigned int i;

	if (list == NULL)
		return;
	for (i = 0; i < n; i++)
		free(list[i]);
	free(list);
}
