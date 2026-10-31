/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - LH5 (LHarc "-lh5-") decompressor.
 *
 * ST-Guide compresses every page and image with the static-Huffman
 * LZSS scheme known from LHarc as "-lh5-" (8 KB sliding window).
 */
#ifndef EHV_LH5_H
#define EHV_LH5_H

/*
 * Decode `srclen` bytes of LH5 data from `src` into exactly `dstlen`
 * bytes at `dst`.
 *
 * Returns 0 on success, -1 if the input is corrupt or ends early (or,
 * while a piece decoder is running, no memory for a second one).
 * Never writes outside dst[0 .. dstlen-1] and never reads outside
 * src[0 .. srclen-1].
 */
int lh5_decode(const unsigned char *src, unsigned long srclen,
               unsigned char *dst, unsigned long dstlen);

/*
 * The same a piece at a time (a picture unpacked between event calls,
 * the guidelines' Speed): lh5_begin() takes the same arguments (NULL: no
 * memory; src and dst stay the caller's until lh5_end()); lh5_more()
 * puts out about `budget` more bytes and sets *out to how many are out
 * from the start: 1 all of them, 0 more to come, -1 corrupt.
 */
typedef struct lh5_state lh5_state;
lh5_state *lh5_begin(const unsigned char *src, unsigned long srclen,
                     unsigned char *dst, unsigned long dstlen);
int lh5_more(lh5_state *s, unsigned long budget, unsigned long *out);
void lh5_end(lh5_state *s);

#endif
