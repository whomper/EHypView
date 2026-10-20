/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - the index's names (names.c), between index.c and names.c.
 */
#ifndef EHV_NAMES_H
#define EHV_NAMES_H

#include "index.h"

/* the last names gathered sorted and written, then all of them merged
   into the blocks (ehv_index_finish_some()): 1 = done, 0 = more, -1 =
   failed */
int ehv_names_finish_some(ehv_index *idx, unsigned int budget);

/* the next step of the merge reads or writes a file (a program begins
   a slice of its own with it) */
int ehv_names_next_io(const ehv_index *idx);

/* the building's memory and NAME.RUN gone (ehv_index_free()) */
void ehv_names_build_free(ehv_index *idx);

/* where a finished index sorted its names into (NAME.ID$), or NULL */
const char *ehv_names_target(const ehv_index *idx);

/* the blocks read through in pieces of this many, and written when
   this many are sorted (ehv_index_pieces()) */
unsigned long ehv_names_piece(void);
unsigned long ehv_names_out(void);

/* each block's mark: a block read from a file that has since been
   written again doesn't carry it; such reads so far (the program then
   reads the index again) */
extern unsigned long ehv_names_misread;

#endif
