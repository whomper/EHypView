| EHypView runtime for TOS, taken from ECal (github.com/whomper/ECal),
| which took it from EMail (whomper/EMAIL) and Claude ST (whomper/atari_claude).
| Copyright (C) the EHypView, ECal, EMail and Claude ST authors.
| Free software under the GNU General Public License, version 2 or
| (at your option) any later version; see COPYING.

| NatFeats (emulator native features) for debug output under Hatari/ARAnyM.
| long nf_probe(void)  - supervisor only: NF id of "NF_STDERR", 0 if absent
| long nf_call2(long id, long arg)

	.text
	.globl	nf_probe
nf_probe:
	move.l	0x10.w,%a1		| illegal instruction vector
	move.l	%sp,%a0
	move.l	#1f,0x10.w
	pea	nf_name
	subq.l	#4,%sp			| NatFeats read the argument at 4(sp),
	moveq	#0,%d0			| as in a C call: leave room for the
	.dc.w	0x7300			| return address (nf_id)
	addq.l	#8,%sp
	move.l	%a1,0x10.w
	rts
1:	move.l	%a0,%sp			| no NatFeats: illegal instruction
	moveq	#0,%d0
	move.l	%a1,0x10.w
	rts

	.globl	nf_call2
nf_call2:
	.dc.w	0x7301			| nf_call(id, arg)
	rts

	.data
nf_name:
	.asciz	"NF_STDERR"
	.even

| long nf_find(const char *name) - supervisor only: NF id of `name`,
| 0 if absent (EHypView: used for NF_EXIT by the test program)
	.text
	.globl	nf_find
nf_find:
	move.l	4(%sp),%d1
	move.l	0x10.w,%a1
	move.l	%sp,%a0
	move.l	#2f,0x10.w
	move.l	%d1,-(%sp)
	subq.l	#4,%sp			| argument at 4(sp), as in a C call
	moveq	#0,%d0
	.dc.w	0x7300			| nf_id
	addq.l	#8,%sp
	move.l	%a1,0x10.w
	rts
2:	move.l	%a0,%sp
	moveq	#0,%d0
	move.l	%a1,0x10.w
	rts
