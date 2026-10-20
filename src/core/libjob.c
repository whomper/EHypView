/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the library's index kept up to date, in short steps
 * (libjob.h).  A step is one unit of the loader, the scanner, the
 * ALL.REF merge, the sort or the saver: a few lines, a folder or a
 * hypertext, a few modules, a few hundred moves, 16 KB.
 */
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "libjob.h"
#include "plat.h"
#include "ref.h"

struct ehv_libjob {
	int phase;
	char *cache, *allref, **roots;
	unsigned int nroots;

	ehv_loader *loader;
	ehv_scan *scan;
	const ehv_index *prev;          /* the one read, while scanning */
	ehv_index *fresh;               /* built by the scan */
	ehv_index *ready;               /* to be taken */
	ref_db *ref;
	ehv_refmerge refm;
	ehv_finish *fin;
	ehv_saver *saver;
	int started, unchanged, rebuild, saved;
	int ref_checked;                /* ALL.REF looked at */
	pf_info ref_info;
	int have_ref;
};

static char *dupstr(const char *s)
{
	char *d = s != NULL ? malloc(strlen(s) + 1) : NULL;

	if (d != NULL)
		strcpy(d, s);
	return d;
}

ehv_libjob *ehv_libjob_begin(const char *cache, const char *const *roots, unsigned int nroots,
                             const char *allref)
{
	ehv_libjob *j = calloc(1, sizeof(*j));
	unsigned int i;

	if (j == NULL)
		return NULL;
	j->cache = dupstr(cache);
	j->roots = calloc(nroots ? nroots : 1, sizeof(char *));
	if (j->cache == NULL || j->roots == NULL)
		goto fail;
	for (i = 0; i < nroots; i++)
		if ((j->roots[j->nroots++] = dupstr(roots[i])) == NULL)
			goto fail;
	if (allref != NULL && (j->allref = dupstr(allref)) == NULL)
		goto fail;
	return j;

fail:
	ehv_libjob_free(j);
	return NULL;
}

void ehv_libjob_free(ehv_libjob *j)
{
	unsigned int i;

	if (j == NULL)
		return;
	ehv_index_load_abort(j->loader);
	ehv_scan_abort(j->scan);
	ehv_index_finish_abort(j->fin);
	ehv_index_save_abort(j->saver);
	ref_close(j->ref);
	/* indices not handed out yet are the job's */
	if (j->ready != NULL)
	{
		if (j->fresh == j->ready)
			j->fresh = NULL;
		ehv_index_free(j->ready);
	}
	if (j->phase < EHV_JOB_SAVING)
		ehv_index_free(j->fresh);
	for (i = 0; i < j->nroots; i++)
		free(j->roots[i]);
	free(j->roots);
	free(j->cache);
	free(j->allref);
	free(j);
}

ehv_index *ehv_libjob_take(ehv_libjob *j)
{
	ehv_index *idx = j->ready;

	j->ready = NULL;
	return idx;
}

int ehv_libjob_next_is_big(const ehv_libjob *j)
{
	switch (j->phase)
	{
	case EHV_JOB_LOADING:
		return !j->started;
	case EHV_JOB_SCANNING:
		return j->scan == NULL || ehv_scan_next_is_big(j->scan);
	case EHV_JOB_SORTING:
		return (j->have_ref && j->ref == NULL && j->fin == NULL) || (j->ref == NULL && j->fin == NULL);
	case EHV_JOB_SAVING:
		return j->saver == NULL;
	}
	return 0;
}

int ehv_libjob_phase(const ehv_libjob *j)
{
	return j->phase;
}

const ehv_scan_status *ehv_libjob_scan_status(const ehv_libjob *j)
{
	return j->phase == EHV_JOB_SCANNING && j->scan != NULL ? ehv_scan_status_get(j->scan) : NULL;
}

int ehv_libjob_unchanged(const ehv_libjob *j)
{
	return j->unchanged;
}

int ehv_libjob_saved(const ehv_libjob *j)
{
	return j->saved;
}

/* ALL.REF as it is now against the one merged into the index read */
static int same_allref(const ehv_libjob *j)
{
	const ehv_index *p = j->prev;

	if (!j->have_ref)
		return p->allref_path == NULL;
	return p->allref_path != NULL && atari_casecmp(p->allref_path, j->allref) == 0 &&
	       p->allref_size == j->ref_info.size && p->allref_mtime == j->ref_info.mtime;
}

int ehv_libjob_step(ehv_libjob *j)
{
	int r;

	switch (j->phase)
	{
	case EHV_JOB_LOADING:
		if (!j->started)
		{
			j->started = 1;
			j->loader = ehv_index_load_begin(j->cache);
			if (j->loader == NULL)
			{
				j->phase = EHV_JOB_SCANNING;      /* none yet */
				return EHV_JOB_MORE;
			}
		}
		r = ehv_index_load_some(j->loader, 1);
		if (r == 0)
			return EHV_JOB_MORE;
		j->phase = EHV_JOB_SCANNING;
		if (r < 0)
		{
			/* damaged, or from another version: a scan from scratch */
			ehv_index_load_abort(j->loader);
			j->loader = NULL;
			return EHV_JOB_MORE;
		}
		j->ready = ehv_index_load_take(j->loader);
		j->loader = NULL;
		j->prev = j->ready;
		return EHV_JOB_INDEX;

	case EHV_JOB_SCANNING:
		if (j->scan == NULL)
		{
			/* ALL.REF looked at first, once: when it changed the index
			   is built straight away, with no checking pass before (the
			   folders read once, the AERA guidelines, 6.5) */
			if (!j->ref_checked)
			{
				j->ref_checked = 1;
				j->have_ref = j->allref != NULL && pf_stat(j->allref, &j->ref_info) == 0 &&
					      !j->ref_info.is_dir;
				if (j->prev != NULL && !same_allref(j))
					j->rebuild = 1;
			}
			/* checking first, or building */
			j->scan = j->rebuild ? ehv_scan_begin((const char *const *) j->roots, j->nroots, j->prev)
				: ehv_scan_begin_check((const char *const *) j->roots, j->nroots, j->prev);
			if (j->scan == NULL || ehv_scan_build_to(j->scan, j->cache) < 0)
				return EHV_JOB_FAILED;
		}
		r = ehv_scan_step(j->scan, 1);
		if (r < 0)
			return EHV_JOB_FAILED;
		if (r == 0)
			return EHV_JOB_MORE;
		if (j->prev != NULL && !j->rebuild && !ehv_scan_changed(j->scan))
		{
			ehv_scan_abort(j->scan);
			j->scan = NULL;
			if (!same_allref(j))
			{
				j->rebuild = 1;          /* the hypertexts as they were, ALL.REF anew */
				return EHV_JOB_MORE;
			}
			/* the index read is the one the scan would build */
			j->unchanged = 1;
			j->phase = EHV_JOB_FINISHED;
			return EHV_JOB_DONE;
		}
		j->fresh = ehv_scan_take(j->scan);
		j->scan = NULL;
		j->prev = NULL;
		if (j->have_ref)
		{
			j->fresh->allref_size = j->ref_info.size;
			j->fresh->allref_mtime = j->ref_info.mtime;
			if ((j->fresh->allref_path = pool_strdup(&j->fresh->pool, j->allref)) == NULL)
				return EHV_JOB_FAILED;
		}
		j->phase = EHV_JOB_SORTING;
		return EHV_JOB_MORE;

	case EHV_JOB_SORTING:
		if (j->have_ref && j->ref == NULL && j->fin == NULL)
		{
			if ((j->ref = ref_open(j->allref, NULL)) == NULL)
				j->have_ref = 0;            /* unreadable: without it */
			return EHV_JOB_MORE;
		}
		if (j->ref != NULL)
		{
			r = ehv_index_merge_ref_some(j->fresh, j->ref, &j->refm, 4);
			if (r < 0)
				return EHV_JOB_FAILED;
			if (r == 1)
			{
				ref_close(j->ref);
				j->ref = NULL;
				j->have_ref = 0;
			}
			return EHV_JOB_MORE;
		}
		if (j->fin == NULL && (j->fin = ehv_index_finish_begin(j->fresh)) == NULL)
			return EHV_JOB_FAILED;
		r = ehv_index_finish_some(j->fin, 1);
		if (r < 0)
			return EHV_JOB_FAILED;
		if (r == 0)
			return EHV_JOB_MORE;
		ehv_index_finish_abort(j->fin);
		j->fin = NULL;
		j->ready = j->fresh;
		j->phase = EHV_JOB_SAVING;
		return EHV_JOB_INDEX;

	case EHV_JOB_SAVING:
		if (j->saver == NULL && (j->saver = ehv_index_save_begin(j->fresh, j->cache)) == NULL)
		{
			j->phase = EHV_JOB_FINISHED;     /* not saved: scanned again next time */
			return EHV_JOB_DONE;
		}
		r = ehv_index_save_some(j->saver, 1);
		if (r == 0)
			return EHV_JOB_MORE;
		j->saved = r == 1;
		ehv_index_save_abort(j->saver);
		j->saver = NULL;
		j->phase = EHV_JOB_FINISHED;
		return EHV_JOB_DONE;
	}
	return EHV_JOB_DONE;
}
