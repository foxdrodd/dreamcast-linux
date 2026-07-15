! SH4 matrix routines for the libpvr (Dreamcast-Linux) GLdc platform.
!
! Ported from KallistiOS kernel/arch/dreamcast/math/matrix.s (originally by
! Marcus Comstedt; optimizations by Andrew Kieschnick / Mathieu Legris), with
! the symbol names adjusted for the SH-ELF *Linux* ABI (no leading underscore).
!
! These operate on the SH4 "internal" matrix held in the XMTRX bank and require
! single-precision FP mode on entry (build with -m4-single).  They are pure
! unprivileged FPU code and run fine in Linux userland.
!
! matrix args to mat_store/mat_load must be 8-byte aligned (32 for perf).

	.text

! Copy the internal (XMTRX) matrix out to a memory matrix (r4).
	.globl mat_store
mat_store:
	fschg
	add	#4*16, r4
	fmov	xd14, @-r4
	fmov	xd12, @-r4
	fmov	xd10, @-r4
	fmov	xd8, @-r4
	fmov	xd6, @-r4
	fmov	xd4, @-r4
	fmov	xd2, @-r4
	fmov	xd0, @-r4
	rts
	fschg

! Load a memory matrix (r4) into the internal (XMTRX) one.
	.globl mat_load
mat_load:
	fschg
	fmov	@r4+, xd0
	fmov	@r4+, xd2
	fmov	@r4+, xd4
	fmov	@r4+, xd6
	fmov	@r4+, xd8
	fmov	@r4+, xd10
	fmov	@r4+, xd12
	fmov	@r4+, xd14
	rts
	fschg

! Set the internal matrix to identity.
	.globl mat_identity
mat_identity:
	fldi0	fr0
	fldi0	fr1
	fldi1	fr2
	fldi0	fr3
	fldi0	fr4
	fldi1	fr5
	fschg
	fmov	dr2, xd0
	fmov	dr0, xd2
	fmov	dr4, xd4
	fmov	dr0, xd6
	fmov	dr0, xd8
	fmov	dr2, xd10
	fmov	dr0, xd12
	fmov	dr4, xd14
	rts
	fschg

! Enter single-precision FP mode (FPSCR.PR = 0) so FTRV is defined, SAVING the
! original FPSCR in r3.  The Linux -m4 ABI rests in double mode (PR = 1), where
! FTRV is a silent no-op; GCC-emitted float code XOR-toggles PR relative to that
! resting PR=1, so on return we MUST restore it (lds r3, fpscr) or every
! subsequent C float op runs in the wrong mode.  Clobbers r0, r1; writes r3.
.macro FORCE_SINGLE
	sts	fpscr, r3	! save caller's FPSCR (PR=1, SZ=0)
	mov	#1, r1
	shll16	r1		! bit 16
	shll	r1		! bit 17
	shll	r1		! bit 18
	shll	r1		! bit 19 (PR) = 0x80000
	not	r1, r1
	mov	r3, r0
	and	r1, r0		! clear PR
	lds	r0, fpscr
.endm

! Multiply a memory matrix (r4) onto the internal one: XMTRX = XMTRX * r4.
	.globl mat_apply
mat_apply:
	fmov.s	fr15, @-r15
	fmov.s	fr14, @-r15
	fmov.s	fr13, @-r15
	fmov.s	fr12, @-r15
	FORCE_SINGLE
	fschg
	fmov	@r4+, dr0
	fmov	@r4+, dr2
	fmov	@r4+, dr4
	fmov	@r4+, dr6
	ftrv	xmtrx, fv0
	fmov	@r4+, dr8
	fmov	@r4+, dr10
	ftrv	xmtrx, fv4
	fmov	@r4+, dr12
	fmov	@r4+, dr14
	ftrv	xmtrx, fv8
	ftrv	xmtrx, fv12
	fmov	dr0, xd0
	fmov	dr2, xd2
	fmov	dr4, xd4
	fmov	dr6, xd6
	fmov	dr8, xd8
	fmov	dr10, xd10
	fmov	dr12, xd12
	fmov	dr14, xd14
	lds	r3, fpscr	! restore caller's FPSCR (PR=1, SZ=0)
	fmov.s	@r15+, fr12
	fmov.s	@r15+, fr13
	fmov.s	@r15+, fr14
	rts
	fmov.s	@r15+, fr15

! Multiply memory matrix r5 onto internal, store result to memory matrix r4.
	.globl mat_multiply
mat_multiply:
	fmov.s	fr15, @-r15
	fmov.s	fr14, @-r15
	fmov.s	fr13, @-r15
	fmov.s	fr12, @-r15
	FORCE_SINGLE
	fschg
	fmov	@r5+, dr0
	fmov	@r5+, dr2
	fmov	@r5+, dr4
	fmov	@r5+, dr6
	ftrv	xmtrx, fv0
	fmov	@r5+, dr8
	fmov	@r5+, dr10
	ftrv	xmtrx, fv4
	fmov	@r5+, dr12
	fmov	@r5+, dr14
	ftrv	xmtrx, fv8
	ftrv	xmtrx, fv12
	fmov	dr0, @r4
	add	#8, r4
	fmov	dr2, @r4
	add	#8, r4
	fmov	dr4, @r4
	add	#8, r4
	fmov	dr6, @r4
	add	#8, r4
	fmov	dr8, @r4
	add	#8, r4
	fmov	dr10, @r4
	add	#8, r4
	fmov	dr12, @r4
	add	#8, r4
	fmov	dr14, @r4
	lds	r3, fpscr	! restore caller's FPSCR (PR=1, SZ=0)
	fmov.s	@r15+, fr12
	fmov.s	@r15+, fr13
	fmov.s	@r15+, fr14
	rts
	fmov.s	@r15+, fr15
