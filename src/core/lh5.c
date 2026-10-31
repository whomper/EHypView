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
#include <stddef.h>
#include <stdlib.h>
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

struct lh5_state {
	const unsigned char *src;
	unsigned long srclen;
	unsigned long pos;               /* next byte to load */
	unsigned long bitbuf;            /* MSB aligned at bit 31 */
	int bitcount;                    /* valid bits in bitbuf */
	unsigned long consumed;          /* bits consumed so far */
	int error;

	unsigned char *dst;              /* where it goes, how much, how far */
	unsigned long dstlen, out;
	unsigned long blocksize;         /* symbols left in the block */

	huff_t t, c, p;
};

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

/* peek n (1..16) bits without consuming them; these two inline, a
   block's tables read with them (read_c_len: up to 510 codes) */
static inline unsigned int peek(lh5_state *s, int n)
{
	if (s->bitcount < n)
		fill(s);
	return (unsigned int) (s->bitbuf >> (32 - n));
}

static inline void skip(lh5_state *s, int n)
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
	unsigned long left;
	unsigned int i, l;

	h->nsym = nsym;
	h->constant = -1;
	memset(h->count, 0, sizeof(h->count));
	{
		const unsigned char *lp = h->len, *le = h->len + nsym;

		while (lp < le)
			h->count[*lp++]++;
	}
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
	{
		const unsigned char *lp = h->len;
		unsigned short sym;

		for (sym = 0; sym < nsym; sym++, lp++)
			if (*lp != 0)
				h->sorted[offs[*lp]++] = sym;
	}

	/* fast table: walk symbols in canonical order, assigning codes; they
	   fill it from the front, the rest is SLOW.  Once a block, three
	   tables: 16-bit counts and a pointer (an ST took 55 ms a block) */
	{
		unsigned short *f = h->fast, *end = h->fast + FASTSIZE;

		i = 0;
		for (l = 1; l <= FASTBITS; l++)
		{
			unsigned short n, cnt = (unsigned short) (1U << (FASTBITS - l));

			for (n = 0; n < h->count[l]; n++, i++)
			{
				unsigned short v = (unsigned short) ((l << 9) | h->sorted[i]), k;

				for (k = cnt; k > 0; k--)
					*f++ = v;
			}
		}
		while (f < end)
			*f++ = SLOW;
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
 * go past it (checked at the end of each block and of the data).  It
 * stops once `budget` more bytes are out (a match may go a little past):
 * 1 all of them out, 0 more to come, -1 corrupt.
 */
static int run(lh5_state *s, unsigned long budget)
{
	const unsigned char *src = s->src;
	unsigned char *dst = s->dst;
	unsigned long srclen = s->srclen, dstlen = s->dstlen, out = s->out, blocksize = s->blocksize, pos;
	unsigned long stop = budget < dstlen - out ? out + budget : dstlen;
	unsigned int bb;                 /* 32 bits, here and on the Atari */
	int bc;

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

	if (s->error)
		return -1;
	LOAD();
	while (out < stop)
	{
		unsigned int c, e;

		if (blocksize == 0)
		{
			if (PAST_END())
				return s->error = 1, -1;
			SAVE();
			blocksize = getbits(s, 16);
			if (blocksize == 0)
				blocksize = 65536UL;
			if (read_pt_len(s, &s->t, NT, TBIT, 3) < 0 ||
				read_c_len(s) < 0 ||
				read_pt_len(s, &s->p, NP, PBIT, -1) < 0 ||
				s->error)
				return s->error = 1, -1;
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
	SAVE();
	s->out = out;
	s->blocksize = blocksize;
	if (out < dstlen)
		return 0;
	if (PAST_END())
		return s->error = 1, -1;
	return 1;
}

static void begin(lh5_state *s, const unsigned char *src, unsigned long srclen,
                  unsigned char *dst, unsigned long dstlen)
{
	/* not the tables, made at each block's start: 11 KB cleared took
	   7 ms a page on an ST */
	memset(s, 0, offsetof(lh5_state, t));
	s->src = src;
	s->srclen = srclen;
	s->dst = dst;
	s->dstlen = dstlen;
}

/* one decoder kept (~11 KB, off the TOS stack): a page's pictures are
   unpacked one at a time, and a page between their steps; a second at
   once gets its own from the heap. One, not one for pages and one for
   pieces: 11 KB less of an accessory resident from the boot on, where
   the family's first boot under NVDI is a few KB short (hub, 31 Oct) */
static lh5_state piece_state;
static int piece_used;

lh5_state *lh5_begin(const unsigned char *src, unsigned long srclen,
                     unsigned char *dst, unsigned long dstlen)
{
	lh5_state *s = piece_used ? malloc(sizeof(lh5_state)) : &piece_state;

	if (s == &piece_state)
		piece_used = 1;
	if (s != NULL)
		begin(s, src, srclen, dst, dstlen);
	return s;
}

int lh5_more(lh5_state *s, unsigned long budget, unsigned long *out)
{
	int r = run(s, budget);

	*out = s->out;
	return r;
}

void lh5_end(lh5_state *s)
{
	if (s == &piece_state)
		piece_used = 0;
	else
		free(s);
}

int lh5_decode(const unsigned char *src, unsigned long srclen,
               unsigned char *dst, unsigned long dstlen)
{
	lh5_state *s = lh5_begin(src, srclen, dst, dstlen);
	int r;

	if (s == NULL)
		return -1;
	r = run(s, dstlen);
	lh5_end(s);
	return r == 1 ? 0 : -1;
}
