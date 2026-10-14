/*
 * EHypView runtime for TOS, taken from ECal (github.com/whomper/ECal),
 * which took it from EMail (whomper/EMAIL) and Claude ST (whomper/atari_claude).
 * Copyright (C) the EHypView, ECal, EMail and Claude ST authors.
 * Free software under the GNU General Public License, version 2 or
 * (at your option) any later version; see COPYING.
 */
/* Flat TOS image: text (+rodata) | data | bss, linked at address 0.
   elf2tos.py turns the --emit-relocs output into a relocatable PRG. */
ENTRY(_start)
SECTIONS
{
  . = 0;
  .text : {
    *crt0.o(.text)
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
  }
  .data : {
    *(.data .data.*)
    . = ALIGN(4);
  }
  .bss : {
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
  }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu*) }
}
