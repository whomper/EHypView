/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the library's index kept up to date, in short steps.
 *
 * An accessory runs only while the program in front calls the AES, so
 * it must never hold the machine for long (the AERA guidelines, 2.2,
 * "A quiet start").  This job does everything the index needs, one short
 * step at a time:
 *
 *   1. read EHYPVIEW.IDX's head (the names stay in the file): EHV_JOB_INDEX,
 *   2. look through the folders for hypertexts that are new or changed,
 *      the names of those read gathered and sorted a batch at a time,
 *   3. when nothing changed (nor ALL.REF), stop: no sorting, no writing;
 *      else merge ALL.REF, sort the names into the new file's blocks
 *      (EHV_JOB_INDEX again) and save its head.
 *
 *     ehv_libjob *j = ehv_libjob_begin(cache, roots, n, allref);
 *     while ((r = ehv_libjob_step(j)) >= 0 && r != EHV_JOB_DONE)
 *         if (r == EHV_JOB_INDEX)
 *             use ehv_libjob_take(j), freeing the one used before
 *     ehv_libjob_free(j);
 *
 * Each index handed out belongs to the caller, who must keep the last one
 * until the job ends (the job still reads it while it scans and saves).
 */
#ifndef EHV_LIBJOB_H
#define EHV_LIBJOB_H

#include "index.h"
#include "scan.h"

typedef struct ehv_libjob ehv_libjob;

#define EHV_JOB_FAILED  (-1)   /* out of memory: the job is over */
#define EHV_JOB_MORE      0
#define EHV_JOB_INDEX     1    /* an index to use: ehv_libjob_take() */
#define EHV_JOB_DONE      2

/* what it does now */
#define EHV_JOB_LOADING   0
#define EHV_JOB_SCANNING  1
#define EHV_JOB_SORTING   2    /* ALL.REF merged, the tables and names sorted */
#define EHV_JOB_SAVING    3
#define EHV_JOB_FINISHED  4

/* allref: ALL.REF's path, or NULL; read only when the index must be
   built again */
ehv_libjob *ehv_libjob_begin(const char *cache, const char *const *roots, unsigned int nroots,
                             const char *allref);
int ehv_libjob_step(ehv_libjob *j);
ehv_index *ehv_libjob_take(ehv_libjob *j);
int ehv_libjob_phase(const ehv_libjob *j);
const char *ehv_libjob_doing(const ehv_libjob *j);   /* in words, for a profile */
int ehv_libjob_next_is_big(const ehv_libjob *j);  /* opens a file or reads a folder: a slice's first */
const ehv_scan_status *ehv_libjob_scan_status(const ehv_libjob *j);  /* NULL unless scanning */
int ehv_libjob_unchanged(const ehv_libjob *j);   /* done, with the index read kept */
int ehv_libjob_saved(const ehv_libjob *j);       /* done, and EHYPVIEW.IDX written */
void ehv_libjob_free(ehv_libjob *j);

#endif
