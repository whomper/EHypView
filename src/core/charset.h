/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EHypView - Atari ST character set to Unicode.
 *
 * On the Atari, GEM fonts use the Atari character set directly, so the
 * viewer never converts text.  This is used by the host tools (dumping,
 * tests) and anything that exports text.
 */
#ifndef EHV_CHARSET_H
#define EHV_CHARSET_H

#include <stddef.h>

/* Unicode code point for an Atari ST character */
unsigned long atari_to_unicode(unsigned char c);

/* write the UTF-8 form of an Atari character to out (>= 4 bytes);
   returns the number of bytes written */
size_t atari_to_utf8(unsigned char c, char *out);

/* lower-case an Atari character: ASCII and the accented capitals */
unsigned char atari_tolower(unsigned char c);

/* compare ignoring case (Atari character set), like strcmp */
int atari_casecmp(const char *a, const char *b);

/* the same for at most n characters */
int atari_ncasecmp(const char *a, const char *b, unsigned long n);

#endif
