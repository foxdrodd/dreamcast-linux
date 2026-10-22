/*
 * Reproducer for the "ssh over serial PPP" kernel panic (7.2.9):
 *
 *   delay-slot-insn faulting in handle_unaligned_delayslot: 0000 [#1]
 *   Kernel panic - not syncing: Fatal exception in interrupt
 *
 * slhc_compress() stores the 16-bit TCP checksum through a plain
 * __sum16 pointer. Once a TCP flow is established and the connection
 * ID is left out of the VJ header, that store lands on an odd address.
 * gcc places it in the delay slot of the following memcpy() call, and
 * the SH unaligned fixup can't fetch kernel delay-slot instructions.
 *
 * No modem, tty or peer is needed: a bare /dev/ppp unit with VJ
 * compression enabled runs slhc_compress() on every IP frame written
 * to it. The first segment of a flow goes out uncompressed, the second
 * one is VJ-compressed with a 1-byte header.
 *
 * Unpatched kernel: panics on the second write.
 * Patched kernel (0029 + 0030): prints "OK" and exits 0.
 *
 * Microbenchmark: "ppp_vj_unaligned <frames>" writes that many frames
 * and prints the time per frame and the number of kernel unaligned
 * fixups from /proc/cpu/alignment. With only 0029 every compressed
 * frame takes one fixup trap, with 0029 + 0030 none. Compare the
 * ns/frame of both kernels to get the cost of a trap.
 *
 * Build: sh4-linux-gcc -static -o ppp_vj_unaligned ppp_vj_unaligned.c
 * Run as root on the Dreamcast.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/ppp_defs.h>
#include <linux/ppp-ioctl.h>

#define SEGLEN	10

static void build_frame(unsigned char *f, unsigned short id, unsigned int seq)
{
	unsigned char *ip = f + 2, *th = ip + 20;
	int i;

	memset(f, 0, 2 + 40 + SEGLEN);
	f[0] = PPP_IP >> 8;
	f[1] = PPP_IP & 0xff;

	ip[0] = 0x45;				/* IPv4, ihl 5 */
	ip[3] = 40 + SEGLEN;			/* tot_len */
	ip[4] = id >> 8;
	ip[5] = id & 0xff;
	ip[8] = 64;				/* ttl */
	ip[9] = 6;				/* TCP */
	ip[12] = 10; ip[15] = 1;		/* 10.0.0.1 */
	ip[16] = 10; ip[19] = 2;		/* 10.0.0.2 */

	th[0] = 0x80; th[1] = 0x00;		/* sport 32768 */
	th[2] = 0x00; th[3] = 22;		/* dport 22 */
	th[4] = seq >> 24; th[5] = seq >> 16;
	th[6] = seq >> 8;  th[7] = seq;
	th[11] = 1;				/* ack_seq */
	th[12] = 5 << 4;			/* doff 5 */
	th[13] = 0x18;				/* ACK|PSH */
	th[14] = 0x10;				/* window */
	th[16] = 0xbe; th[17] = 0xef;		/* checksum, not verified */

	for (i = 0; i < SEGLEN; i++)
		th[20 + i] = 'a' + i;
}

/* kernel-mode unaligned fixups so far, -1 if unknown */
static long kernel_fixups(void)
{
	char line[64];
	long n = -1;
	FILE *f;

	f = fopen("/proc/cpu/alignment", "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "System: %ld", &n) == 1)
			break;
	fclose(f);
	return n;
}

int main(int argc, char **argv)
{
	unsigned char frame[2 + 40 + SEGLEN];
	int fd, unit = -1, val, verbose;
	unsigned long i, frames = 4;
	struct timespec t0, t1;
	long fix0, fix1;
	double ns;
	struct npioctl npi;

	if (argc > 1)
		frames = strtoul(argv[1], NULL, 0);
	if (frames < 2) {
		fprintf(stderr, "usage: %s [frames >= 2]\n", argv[0]);
		return 2;
	}
	verbose = frames <= 16;

	if (access("/dev/ppp", F_OK) && mknod("/dev/ppp", S_IFCHR | 0600, makedev(108, 0))) {
		perror("mknod /dev/ppp");
		return 2;
	}

	fd = open("/dev/ppp", O_RDWR);
	if (fd < 0) {
		perror("open /dev/ppp");
		return 2;
	}
	if (ioctl(fd, PPPIOCNEWUNIT, &unit) < 0) {
		perror("PPPIOCNEWUNIT");
		return 2;
	}

	/* allocate the VJ state (pppd does this once VJ is negotiated) */
	val = 15;
	if (ioctl(fd, PPPIOCSMAXCID, &val) < 0) {
		perror("PPPIOCSMAXCID");
		return 2;
	}
	val = SC_COMP_TCP;
	if (ioctl(fd, PPPIOCSFLAGS, &val) < 0) {
		perror("PPPIOCSFLAGS");
		return 2;
	}
	npi.protocol = PPP_IP;
	npi.mode = NPMODE_PASS;
	if (ioctl(fd, PPPIOCSNPMODE, &npi) < 0) {
		perror("PPPIOCSNPMODE");
		return 2;
	}

	printf("ppp%d: VJ on, sending %lu TCP segments\n", unit, frames);
	fflush(stdout);
	sync();

	fix0 = kernel_fixups();
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (i = 0; i < frames; i++) {
		build_frame(frame, 1 + i, 1000 + i * SEGLEN);
		if (verbose) {
			printf("segment %lu (%s)\n", i, i ? "VJ compressed, odd checksum offset" : "uncompressed");
			fflush(stdout);
		}
		if (write(fd, frame, sizeof(frame)) != sizeof(frame)) {
			perror("write");
			return 2;
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	fix1 = kernel_fixups();

	close(fd);

	ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
	if (!verbose)
		printf("%lu frames in %.1f ms, %.0f ns/frame\n", frames, ns / 1e6, ns / frames);
	if (fix0 >= 0 && fix1 >= 0)
		printf("kernel unaligned fixups: %ld\n", fix1 - fix0);
	printf("OK\n");
	return 0;
}
