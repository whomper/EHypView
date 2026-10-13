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

#endif
