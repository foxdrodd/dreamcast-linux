/*
 * SH4 user-mode unaligned access fixups in branch delay slots.
 *
 * The SH4 can't do unaligned loads/stores; the kernel emulates them in
 * do_address_error() -> handle_unaligned_access(). If the faulting
 * instruction sits in a branch delay slot, the CPU reports the PC of
 * the branch. The kernel then has to fetch the delay slot instruction
 * itself (handle_delayslot()), emulate it, and emulate the branch.
 *
 * This runs a 32-bit unaligned load and store in the delay slot of
 * every SH4 delayed branch (bra, bsr, braf, bsrf, jmp, jsr, rts, bt/s
 * and bf/s both taken and not taken), at the misalignments 1, 2 and 3.
 * Each case checks the loaded/stored value, that the branch went to
 * the right place and, for bsr/bsrf/jsr, that it returned through the
 * right PR.
 *
 * User mode was never broken (copy_from_user() is right for user
 * PCs); this guards the user path against regressions in fixes for
 * the kernel path. A broken fixup shows up as SIGBUS or a FAIL line.
 *
 * Build: sh4-linux-gcc -static -o user_delayslot user_delayslot.c
 * Needs the fixup policy in /proc/cpu/alignment ("User faults") to
 * include fixup, which is the default.
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>

/*
 * Every test function is called as f(p, marker, val):
 *   r4 = unaligned address, r5 = int *marker, r6 = value to store.
 * The delay slot either loads @r4 into r0 (preset to -2, so a skipped
 * load shows), or stores r6 to @r4 with r0 preset to 0. The branch
 * target writes 1 to *marker; a wrong fall through returns -1.
 * bsr/bsrf/jsr add 1 more after returning, so they expect a marker
 * of 2.
 */
#define L(n, s)	".L" #n "_" #s
#define FN(n)	".global " #n "\n.type " #n ",@function\n" #n ":\n"
#define HIT(n)	L(n, t) ":\n\tmov #1,r1\n\trts\n\t mov.l r1,@r5\n"
#define MISS	"\trts\n\t mov #-1,r0\n"
#define RET2	"\tmov.l @r5,r1\n\tadd #1,r1\n\tmov.l r1,@r5\n" \
		"\tlds.l @r15+,pr\n\trts\n\t nop\n"
#define LIT(n, v) "\t.align 2\n" L(n, o) ":\t.long " v "\n"

#define T_BRA(n, init, slot) FN(n) "\t" init "\n" \
	"\tbra " L(n, t) "\n\t " slot "\n" MISS HIT(n)

#define T_BSR(n, init, slot) FN(n) "\t" init "\n" \
	"\tsts.l pr,@-r15\n" \
	"\tbsr " L(n, t) "\n\t " slot "\n" RET2 HIT(n)

#define T_BRAF(n, init, slot) FN(n) "\t" init "\n" \
	"\tmov.l " L(n, o) ",r1\n" \
	"\tbraf r1\n\t " slot "\n" L(n, b) ":\n" MISS HIT(n) \
	LIT(n, L(n, t) "-" L(n, b))

#define T_BSRF(n, init, slot) FN(n) "\t" init "\n" \
	"\tsts.l pr,@-r15\n" \
	"\tmov.l " L(n, o) ",r1\n" \
	"\tbsrf r1\n\t " slot "\n" L(n, b) ":\n" RET2 HIT(n) \
	LIT(n, L(n, t) "-" L(n, b))

#define T_JMP(n, init, slot) FN(n) "\t" init "\n" \
	"\tmov.l " L(n, o) ",r1\n" \
	"\tjmp @r1\n\t " slot "\n" MISS HIT(n) LIT(n, L(n, t))

#define T_JSR(n, init, slot) FN(n) "\t" init "\n" \
	"\tsts.l pr,@-r15\n" \
	"\tmov.l " L(n, o) ",r1\n" \
	"\tjsr @r1\n\t " slot "\n" RET2 HIT(n) LIT(n, L(n, t))

#define T_RTS(n, init, slot) FN(n) "\t" init "\n" \
	"\tmov #1,r1\n\tmov.l r1,@r5\n" \
	"\trts\n\t " slot "\n" MISS

/* conditional delayed branches, T set up so that they are taken */
#define T_CT(n, init, slot, cond, br) FN(n) "\t" init "\n" \
	"\t" cond "\n\t" br " " L(n, t) "\n\t " slot "\n" MISS HIT(n)
/* ... and not taken */
#define T_CN(n, init, slot, cond, br) FN(n) "\t" init "\n" \
	"\t" cond "\n\t" br " " L(n, t) "\n\t " slot "\n" \
	"\tmov #1,r1\n\trts\n\t mov.l r1,@r5\n" L(n, t) ":\n" MISS

#define T_BTS_T(n, i, s)	T_CT(n, i, s, "sett", "bt/s")
#define T_BTS_N(n, i, s)	T_CN(n, i, s, "clrt", "bt/s")
#define T_BFS_T(n, i, s)	T_CT(n, i, s, "clrt", "bf/s")
#define T_BFS_N(n, i, s)	T_CN(n, i, s, "sett", "bf/s")

#define LD	"mov #-2,r0", "mov.l @r4,r0"
#define ST	"mov #0,r0", "mov.l r6,@r4"
#define CALL(M, ...) M(__VA_ARGS__)
#define BOTH(M, n) CALL(M, n##_ld, LD) CALL(M, n##_st, ST)

#define CASES(X) \
	X(T_BRA, bra, 1) X(T_BSR, bsr, 2) X(T_BRAF, braf, 1) \
	X(T_BSRF, bsrf, 2) X(T_JMP, jmp, 1) X(T_JSR, jsr, 2) \
	X(T_RTS, rts, 1) X(T_BTS_T, bts_taken, 1) \
	X(T_BTS_N, bts_not_taken, 1) X(T_BFS_T, bfs_taken, 1) \
	X(T_BFS_N, bfs_not_taken, 1)

#define ASM(M, n, m)	BOTH(M, n)
__asm__("\t.text\n" CASES(ASM));

typedef unsigned long (*test_fn)(void *p, int *marker, unsigned long val);

#define DECL(M, n, m) \
	unsigned long n##_ld(void *, int *, unsigned long); \
	unsigned long n##_st(void *, int *, unsigned long);
CASES(DECL)

static const struct test {
	const char *name;
	test_fn ld, st;
	int marker;
} tests[] = {
#define ENTRY(M, n, m) { #n, n##_ld, n##_st, m },
	CASES(ENTRY)
};

/* "User:" fixup counter from /proc/cpu/alignment, -1 if unknown */
static long user_fixups(char *mode, size_t len)
{
	char line[64];
	long n = -1;
	FILE *f;

	f = fopen("/proc/cpu/alignment", "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		sscanf(line, "User: %ld", &n);
		if (mode && !strncmp(line, "User faults:", 12)) {
			strncpy(mode, line + 12, len - 1);
			mode[len - 1] = 0;
		}
	}
	fclose(f);
	return n;
}

/*
 * The whole buffer is compared before and after, so a fixup that loads
 * the wrong bytes, writes the wrong bytes or touches anything outside
 * the 4 accessed bytes fails.
 */
static int run(const struct test *t, int store, int off)
{
	unsigned char buf[16] __attribute__((aligned(8))), before[16];
	unsigned long r, want, got, val = 0xa1b2c3d4;
	int i, marker = 0;

	for (i = 0; i < 16; i++)
		buf[i] = 0x10 + i;
	memcpy(before, buf, 16);
	memcpy(before + off, store ? (void *)&val : buf + off, 4);
	memcpy(&want, before + off, 4);

	if (store)
		r = t->st(buf + off, &marker, val);
	else
		r = t->ld(buf + off, &marker, 0);
	memcpy(&got, buf + off, 4);

	if (r == (store ? 0 : want) && !memcmp(buf, before, 16) &&
	    marker == t->marker)
		return 0;

	printf("FAIL %s %s off %d: r0=%lx mem=%08lx (want %08lx) marker=%d (want %d)",
	       t->name, store ? "st" : "ld", off, r, got, want, marker, t->marker);
	for (i = 0; i < 16; i++)
		if ((i < off || i >= off + 4) && buf[i] != before[i])
			printf(" byte %d changed", i);
	printf("\n");
	return 1;
}

int main(void)
{
	int n = sizeof(tests) / sizeof(tests[0]);
	int i, off, store, f, fails = 0, runs = 0;
	char mode[32] = "";
	long fix0, fix1;

	/* don't log every fixup, there are a lot of them */
	prctl(PR_SET_UNALIGN, PR_UNALIGN_NOPRINT);

	fix0 = user_fixups(mode, sizeof(mode));
	if (*mode && !strstr(mode, "fixup")) {
		printf("user fixups disabled in /proc/cpu/alignment:%s"
		       "enable with: echo 3 > /proc/cpu/alignment\n", mode);
		return 2;
	}

	for (i = 0; i < n; i++) {
		/* name first, so a SIGBUS shows which branch broke */
		printf("%-14s\n", tests[i].name);
		fflush(stdout);
		for (f = 0, store = 0; store < 2; store++) {
			for (off = 1; off < 4; off++) {
				f += run(&tests[i], store, off);
				runs++;
			}
		}
		fails += f;
	}

	fix1 = user_fixups(NULL, 0);
	if (fix0 >= 0 && fix1 >= 0) {
		printf("user unaligned fixups: %ld (expected %d)\n", fix1 - fix0, runs);
		if (fix1 - fix0 != runs)
			fails++;
	}

	printf("%s: %d cases, %d failed\n", fails ? "FAIL" : "OK", runs, fails);
	return !!fails;
}
