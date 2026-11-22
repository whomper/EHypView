/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
| EHVCHECK's start under qemu: TOS stood in for (tos_stub.c), then crt0.S's
| own start (crt0_start) entered as TOS enters an application: its basepage
| at 4(sp), a0 = 0, the command line C:\FIXT
	.text
	.globl	_start
_start:
	jsr	tos_install
	lea	fake_bp,%a0
	clr.l	-(%sp)
	move.l	%a0,-(%sp)
	clr.l	-(%sp)
	sub.l	%a0,%a0
	jmp	crt0_start

	.data
	.balign	4
fake_bp:				| p_tlen 0x0c, p_dlen 0x14, p_blen 0x1c
	.long	0, 0, 0, 0x1234, 0, 0x560, 0, 0x7a000
	.space	0x80 - 32
	.byte	7			| the command line
	.ascii	"C:\\FIXT"
	.space	0x100 - 0x80 - 8
