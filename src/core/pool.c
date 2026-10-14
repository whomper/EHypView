/*
 * EHypView - string pool.
 */
#include <stdlib.h>
#include <string.h>
#include "pool.h"

#define CHUNK_SIZE 16384

struct pool_chunk {
	pool_chunk *next;
	size_t used, size;
	char data[1];
};

char *pool_strndup(ehv_pool *pool, const char *s, size_t n)
{
	pool_chunk *c = pool->head;
	char *d;

	if (s == NULL)
		return NULL;
	if (c == NULL || c->size - c->used < n + 1)
	{
		size_t size = n + 1 > CHUNK_SIZE ? n + 1 : CHUNK_SIZE;

		c = malloc(sizeof(pool_chunk) + size);
		if (c == NULL)
			return NULL;
		c->size = size;
		c->used = 0;
		c->next = pool->head;
		pool->head = c;
	}
	d = c->data + c->used;
	memcpy(d, s, n);
	d[n] = '\0';
	c->used += n + 1;
	return d;
}

char *pool_strdup(ehv_pool *pool, const char *s)
{
	return s == NULL ? NULL : pool_strndup(pool, s, strlen(s));
}

void pool_free(ehv_pool *pool)
{
	while (pool->head != NULL)
	{
		pool_chunk *next = pool->head->next;

		free(pool->head);
		pool->head = next;
	}
}
