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
 * Returns 0 on success, -1 if the input is corrupt or ends early.
 * Never writes outside dst[0 .. dstlen-1] and never reads outside
 * src[0 .. srclen-1].
 */
int lh5_decode(const unsigned char *src, unsigned long srclen,
               unsigned char *dst, unsigned long dstlen);

#endif
