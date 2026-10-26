/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - LH5 (LHarc "-lh5-") decompressor.
 *
 * Written from the public description of the format:
 *
 *   - LZSS with an 8 KB window, match lengths 3..256.
 *   - The stream is a sequence of blocks.  Each block starts with a
 *     16 bit symbol count followed by three canonical Huffman tables:
 *       T: code lengths of the C table   (19 symbols, 5 bit count)
 *       C: literals 0..255 and match lengths 256..509 (9 bit count)
 *       P: position slots 0..13          (4 bit count)
 *   - All bit fields are stored MSB first.
 *
 * Huffman decoding uses a small direct lookup table for short codes and
 * a canonical walk for the rare longer ones, which keeps the tables
 * small enough for a 68000 machine.
 */
#include <string.h>
#include "lh5.h"

#define DICBIT    13
#define MAXMATCH  256
#define THRESHOLD 3
#define NC        (255 + MAXMATCH + 2 - THRESHOLD)   /* 510 */
#define CBIT      9
#define NP        (DICBIT + 1)                         /* 14 */
#define PBIT      4
#define NT        19
#define TBIT      5
#define MAXLEN    16

#define FASTBITS  10
#define FASTSIZE  (1 << FASTBITS)
#define SLOW      0xFFFFu

typedef struct {
	unsigned short nsym;
	int constant;                    /* >= 0: every code decodes to this */
	unsigned char len[NC];
	unsigned short count[MAXLEN + 1];
	unsigned short sorted[NC];       /* symbols ordered by (len, value) */
	unsigned short fast[FASTSIZE];   /* (len << 9) | sym, or SLOW */
} huff_t;

typedef struct {
	const unsigned char *src;
	unsigned long srclen;
	unsigned long pos;               /* next byte to load */
	unsigned long bitbuf;            /* MSB aligned at bit 31 */
	int bitcount;                    /* valid bits in bitbuf */
	unsigned long consumed;          /* bits consumed so far */
	int error;

	huff_t t, c, p;
} lh5_state;

/* ---------------------------------------------------------------- */
/* bit input                                                         */

static void fill(lh5_state *s)
{
	while (s->bitcount <= 24)
	{
		unsigned long b = 0;

		if (s->pos < s->srclen)
			b = s->src[s->pos];
		s->pos++;                    /* past the end reads as zero */
		s->bitbuf |= b << (24 - s->bitcount);
		s->bitcount += 8;
	}
}

/* peek n (1..16) bits without consuming them */
static unsigned int peek(lh5_state *s, int n)
{
	if (s->bitcount < n)
		fill(s);
	return (unsigned int) (s->bitbuf >> (32 - n));
}

static void skip(lh5_state *s, int n)
{
	if (n <= 0)
		return;
	if (s->bitcount < n)
		fill(s);
	s->bitbuf <<= n;
	s->bitbuf &= 0xFFFFFFFFUL;
	s->bitcount -= n;
	s->consumed += n;
	if (s->consumed > s->srclen * 8UL)
		s->error = 1;
}

static unsigned int getbits(lh5_state *s, int n)
{
	unsigned int v;

	if (n == 0)
		return 0;
	v = peek(s, n);
	skip(s, n);
	return v;
}

/* ---------------------------------------------------------------- */
/* canonical Huffman tables                                          */

static void huff_constant(huff_t *h, unsigned short nsym, unsigned int sym)
{
	unsigned int i;

	h->nsym = nsym;
	h->constant = (int) sym;
	memset(h->len, 0, sizeof(h->len));
	for (i = 0; i < FASTSIZE; i++)     /* lh5_decode()'s own lookup: no bits */
		h->fast[i] = (unsigned short) sym;
}

/* build lookup structures from h->len[0 .. nsym-1]; -1 if oversubscribed */
static int huff_build(huff_t *h, unsigned short nsym)
{
	unsigned short offs[MAXLEN + 2];
	unsigned long code, left;
	unsigned int i, l;

	h->nsym = nsym;
	h->constant = -1;
	memset(h->count, 0, sizeof(h->count));
	for (i = 0; i < nsym; i++)
		h->count[h->len[i]]++;
	h->count[0] = 0;

	/* reject over-subscribed codes; incomplete codes are tolerated */
	left = 1;
	for (l = 1; l <= MAXLEN; l++)
	{
		left <<= 1;
		if (h->count[l] > left)
			return -1;
		left -= h->count[l];
	}

	offs[1] = 0;
	for (l = 1; l <= MAXLEN; l++)
		offs[l + 1] = (unsigned short) (offs[l] + h->count[l]);
	for (i = 0; i < nsym; i++)
		if (h->len[i] != 0)
			h->sorted[offs[h->len[i]]++] = (unsigned short) i;

	/* fast table: walk symbols in canonical order, assigning codes */
	for (i = 0; i < FASTSIZE; i++)
		h->fast[i] = SLOW;
	code = 0;
	i = 0;
	for (l = 1; l <= MAXLEN; l++)
	{
		unsigned int n;

		for (n = 0; n < h->count[l]; n++, i++)
		{
			if (l <= FASTBITS)
			{
				unsigned long first = code << (FASTBITS - l);
				unsigned long cnt = 1UL << (FASTBITS - l);
				unsigned long k;

				for (k = 0; k < cnt; k++)
					h->fast[first + k] = (unsigned short) ((l << 9) | h->sorted[i]);
			}
			code++;
		}
		code <<= 1;
	}
	return 0;
}

static unsigned int huff_decode(lh5_state *s, huff_t *h)
{
	unsigned int bits, e, l;
	unsigned int code, first, index;

	if (h->constant >= 0)
		return (unsigned int) h->constant;

	bits = peek(s, MAXLEN);
	e = h->fast[bits >> (MAXLEN - FASTBITS)];
	if (e != SLOW)
	{
		skip(s, (int) (e >> 9));
		return e & 0x1FF;
	}

	/* canonical walk for codes longer than FASTBITS */
	code = first = index = 0;
	for (l = 1; l <= MAXLEN; l++)
	{
		code |= (bits >> (MAXLEN - l)) & 1;
		if (code - first < h->count[l])
		{
			skip(s, (int) l);
			return h->sorted[index + (code - first)];
		}
		index += h->count[l];
		first = (first + h->count[l]) << 1;
		code <<= 1;
	}
	s->error = 1;                    /* unassigned code */
	return 0;
}

/* ---------------------------------------------------------------- */
/* table headers                                                     */

static int read_pt_len(lh5_state *s, huff_t *h, unsigned short nn, int nbit, int special)
{
	unsigned int n, i;

	n = getbits(s, nbit);
	if (n == 0)
	{
		unsigned int c = getbits(s, nbit);

		if (c >= nn)
			return -1;
		huff_constant(h, nn, c);
		return 0;
	}
	if (n > nn)
		return -1;

	memset(h->len, 0, sizeof(h->len));
	i = 0;
	while (i < n)
	{
		unsigned int c = peek(s, 16) >> 13;

		if (c == 7)
		{
			unsigned int mask = 1U << 12;
			unsigned int bits = peek(s, 16);

			while (mask != 0 && (bits & mask))
			{
				mask >>= 1;
				c++;
			}
			if (c > MAXLEN)
				return -1;
			skip(s, (int) c - 3);
		} else
		{
			skip(s, 3);
		}
		h->len[i++] = (unsigned char) c;
		if ((int) i == special)
		{
			unsigned int z = getbits(s, 2);

			while (z-- > 0 && i < nn)
				h->len[i++] = 0;
		}
	}
	return huff_build(h, nn);
}

static int read_c_len(lh5_state *s)
{
	huff_t *h = &s->c;
	unsigned int n, i;

	n = getbits(s, CBIT);
	if (n == 0)
	{
		unsigned int c = getbits(s, CBIT);

		if (c >= NC)
			return -1;
		huff_constant(h, NC, c);
		return 0;
	}
	if (n > NC)
		return -1;

	memset(h->len, 0, sizeof(h->len));
	i = 0;
	while (i < n)
	{
		unsigned int c = huff_decode(s, &s->t);

		if (s->error)
			return -1;
		if (c <= 2)
		{
			unsigned int z;

			if (c == 0)
				z = 1;
			else if (c == 1)
				z = getbits(s, 4) + 3;
			else
				z = getbits(s, CBIT) + 20;
			if (i + z > NC)
				return -1;
			while (z-- > 0)
				h->len[i++] = 0;
		} else
		{
			h->len[i++] = (unsigned char) (c - 2);
		}
	}
	return huff_build(h, NC);
}

/* ---------------------------------------------------------------- */

/*
 * The symbols, the decoder's whole time (an ST spent 8.7 s on a 640 x
 * 400 picture): the bit buffer is held here, not in the state, and a
 * code of up to FASTBITS bits (nearly all of them) is one lookup; the
 * table headers, once a block, and longer codes go through the state.
 * Reading past the end gives zeros; it is an error when the bits taken
 * go past it (checked at the end of each block and of the data).
 */
int lh5_decode(const unsigned char *src, unsigned long srclen,
               unsigned char *dst, unsigned long dstlen)
{
	static lh5_state st;             /* ~6 KB: keep it off the TOS stack */
	lh5_state *s = &st;
	unsigned long out = 0, blocksize = 0, pos;
	unsigned int bb;                 /* 32 bits, here and on the Atari */
	int bc;

	memset(s, 0, sizeof(*s));
	s->src = src;
	s->srclen = srclen;

/* at least n (up to 24) bits in bb, from its top */
#define NEED(n) \
	while (bc < (n)) \
	{ \
		bb |= (unsigned int) (pos < srclen ? src[pos] : 0) << (24 - bc); \
		pos++; \
		bc += 8; \
	}
/* the state's bits to and from the locals */
#define LOAD() (pos = s->pos, bb = (unsigned int) s->bitbuf, bc = s->bitcount)
#define SAVE() (s->pos = pos, s->bitbuf = bb, s->bitcount = bc, \
		s->consumed = pos * 8UL - (unsigned long) bc)
#define PAST_END() (pos * 8UL - (unsigned long) bc > srclen * 8UL)

	LOAD();
	while (out < dstlen)
	{
		unsigned int c, e;

		if (blocksize == 0)
		{
			if (PAST_END())
				return -1;
			SAVE();
			blocksize = getbits(s, 16);
			if (blocksize == 0)
				blocksize = 65536UL;
			if (read_pt_len(s, &s->t, NT, TBIT, 3) < 0 ||
				read_c_len(s) < 0 ||
				read_pt_len(s, &s->p, NP, PBIT, -1) < 0 ||
				s->error)
				return -1;
			LOAD();
		}
		blocksize--;

		NEED(MAXLEN);
		e = s->c.fast[bb >> (32 - FASTBITS)];
		if (e != SLOW)
		{
			bb <<= e >> 9;
			bc -= (int) (e >> 9);
			c = e & 0x1FF;
		} else
		{
			SAVE();
			c = huff_decode(s, &s->c);
			if (s->error)
				return -1;
			LOAD();
		}
		if (c < 256)
		{
			dst[out++] = (unsigned char) c;
			continue;
		}
		{
			unsigned long len = c - (256 - THRESHOLD), dist;
			unsigned int j;

			NEED(MAXLEN);
			e = s->p.fast[bb >> (32 - FASTBITS)];
			if (e != SLOW)
			{
				bb <<= e >> 9;
				bc -= (int) (e >> 9);
				j = e & 0x1FF;
			} else
			{
				SAVE();
				j = huff_decode(s, &s->p);
				if (s->error)
					return -1;
				LOAD();
			}
			if (j > 1)
			{
				int n = (int) j - 1;      /* at most 13 bits */

				NEED(n);
				j = (1U << n) + (unsigned int) (bb >> (32 - n));
				bb <<= n;
				bc -= n;
			}
			dist = (unsigned long) j + 1;
			if (len > dstlen - out)
				len = dstlen - out;
			if (dist <= out)
			{
				unsigned char *d = dst + out;
				const unsigned char *f = d - dist;

				out += len;
				while (len-- > 0)
					*d++ = *f++;
			} else
				while (len-- > 0)
				{
					/* LHarc starts with a window full of spaces */
					dst[out] = out >= dist ? dst[out - dist] : ' ';
					out++;
				}
		}
	}
	return PAST_END() ? -1 : 0;
}
