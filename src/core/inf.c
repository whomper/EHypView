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
