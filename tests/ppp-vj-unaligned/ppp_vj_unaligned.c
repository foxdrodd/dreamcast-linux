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
 * No modem, serial port or peer is needed: a /dev/ppp unit with VJ
 * compression enabled runs slhc_compress() on every IP frame written
 * to it. The first segment of a flow goes out uncompressed, the second
 * one is VJ-compressed with a 1-byte header.
 *
 * The unit gets a pty as its channel, in the same async PPP line
 * discipline pppd uses on a serial port. Each frame comes back on the
 * pty master as HDLC bytes; it is un-escaped, its FCS checked, and the
 * TCP checksum and data in the VJ header compared with what was sent.
 * Every segment carries a different checksum, so a checksum that was
 * stored to the wrong place or in the wrong byte order shows up.
 *
 * Unpatched kernel: panics on the second write.
 * Patched kernel (0029 and/or 0030): prints "OK" and exits 0.
 *
 * Microbenchmark: "ppp_vj_unaligned <frames>" with more than 16 frames
 * writes that many frames without a channel (so only the compression
 * is timed, not the pty) and prints the time per frame and the number
 * of kernel unaligned fixups from /proc/cpu/alignment. With only 0029
 * every compressed frame takes one fixup trap, with 0029 + 0030 none.
 * Compare the ns/frame of both kernels to get the cost of a trap.
 *
 * Build: sh4-linux-gcc -static -o ppp_vj_unaligned ppp_vj_unaligned.c
 * Run as root on the Dreamcast.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/ppp_defs.h>
#include <linux/ppp-ioctl.h>

#ifndef N_PPP
#define N_PPP	3
#endif

#define SEGLEN	10
#define NEW_C	0x40	/* VJ change mask: connection ID present */

static unsigned short frame_csum(unsigned long i)
{
	return 0xb000 + i;
}

static void build_frame(unsigned char *f, unsigned short id, unsigned int seq,
			unsigned short csum)
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
	th[16] = csum >> 8;			/* checksum, not verified by */
	th[17] = csum & 0xff;			/* slhc, just passed on */

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

/*
 * Give the unit a channel: a pty slave in the async PPP line discipline.
 * Returns the pty master, which receives the HDLC-framed output.
 */
static int attach_pty_channel(int unit)
{
	int master, slave, chfd, ldisc = N_PPP, chan;
	struct termios tio;

	master = posix_openpt(O_RDWR | O_NOCTTY);
	if (master < 0 || grantpt(master) || unlockpt(master)) {
		perror("pty master");
		return -1;
	}
	slave = open(ptsname(master), O_RDWR | O_NOCTTY);
	if (slave < 0) {
		perror("pty slave");
		return -1;
	}
	/* no output processing, the line carries binary HDLC */
	tcgetattr(slave, &tio);
	cfmakeraw(&tio);
	tcsetattr(slave, TCSANOW, &tio);

	if (ioctl(slave, TIOCSETD, &ldisc) < 0) {
		perror("TIOCSETD N_PPP");
		return -1;
	}
	if (ioctl(slave, PPPIOCGCHAN, &chan) < 0) {
		perror("PPPIOCGCHAN");
		return -1;
	}
	chfd = open("/dev/ppp", O_RDWR);
	if (chfd < 0 || ioctl(chfd, PPPIOCATTCHAN, &chan) < 0 ||
	    ioctl(chfd, PPPIOCCONNECT, &unit) < 0) {
		perror("attach channel");
		return -1;
	}
	/* slave and chfd stay open for the lifetime of the test */
	return master;
}

/*
 * Read one HDLC frame from the pty master, un-escape it and check the
 * FCS. Returns the frame length without FCS, -1 on error.
 */
static int read_frame(int master, unsigned char *out, int max)
{
	unsigned short fcs = PPP_INITFCS;
	struct pollfd pfd = { master, POLLIN, 0 };
	int len = 0, esc = 0, b;
	unsigned char c;

	for (;;) {
		if (poll(&pfd, 1, 2000) <= 0 || read(master, &c, 1) != 1) {
			printf("no frame from the channel\n");
			return -1;
		}
		if (c == PPP_FLAG) {
			if (!len)
				continue;	/* opening flag */
			break;
		}
		if (c == PPP_ESCAPE) {
			esc = 1;
			continue;
		}
		if (esc)
			c ^= PPP_TRANS;
		esc = 0;
		if (len == max) {
			printf("frame too long\n");
			return -1;
		}
		out[len++] = c;
	}

	for (b = 0; b < len; b++) {
		fcs ^= out[b];
		for (int k = 0; k < 8; k++)
			fcs = fcs & 1 ? (fcs >> 1) ^ 0x8408 : fcs >> 1;
	}
	if (len < 6 || fcs != PPP_GOODFCS) {
		printf("bad frame: %d bytes, FCS residue %04x\n", len, fcs);
		return -1;
	}
	return len - 2;
}

/* check what slhc made of segment i */
static int verify_frame(unsigned long i, const unsigned char *f, int len,
			const unsigned char *sent)
{
	const unsigned char *data = sent + 2 + 40, *p;
	unsigned short proto, csum, want = frame_csum(i);

	if (len < 4 + 3 + SEGLEN || f[0] != PPP_ALLSTATIONS || f[1] != PPP_UI) {
		printf("frame %lu: bad header\n", i);
		return 1;
	}
	proto = f[2] << 8 | f[3];
	p = f + 4;

	if (proto == PPP_VJC_COMP) {
		/* change mask, [connection ID], TCP checksum, deltas */
		p += p[0] & NEW_C ? 2 : 1;
	} else if (proto == PPP_VJC_UNCOMP) {
		/* full IP + TCP header, IP protocol replaced by the slot */
		p += 20 + 16;
	} else {
		printf("frame %lu: protocol %04x, not VJ\n", i, proto);
		return 1;
	}

	csum = p[0] << 8 | p[1];
	if (csum != want || memcmp(f + len - SEGLEN, data, SEGLEN)) {
		printf("frame %lu: TCP checksum %04x (want %04x)%s\n", i, csum,
		       want, memcmp(f + len - SEGLEN, data, SEGLEN) ?
		       ", data differs" : "");
		return 1;
	}
	printf("  %s, TCP checksum %04x ok\n",
	       proto == PPP_VJC_COMP ? "VJ compressed" : "VJ uncompressed", csum);
	return 0;
}

int main(int argc, char **argv)
{
	unsigned char frame[2 + 40 + SEGLEN], out[256];
	int fd, unit = -1, val, verbose, master = -1, len, fails = 0;
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

	if (verbose) {
		master = attach_pty_channel(unit);
		if (master < 0)
			return 2;
	}

	printf("ppp%d: VJ on, sending %lu TCP segments\n", unit, frames);
	fflush(stdout);
	sync();

	fix0 = kernel_fixups();
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (i = 0; i < frames; i++) {
		build_frame(frame, 1 + i, 1000 + i * SEGLEN, frame_csum(i));
		if (verbose) {
			printf("segment %lu (%s)\n", i, i ? "VJ compressed, odd checksum offset" : "uncompressed");
			fflush(stdout);
		}
		if (write(fd, frame, sizeof(frame)) != sizeof(frame)) {
			perror("write");
			return 2;
		}
		if (verbose) {
			len = read_frame(master, out, sizeof(out));
			fails += len < 0 || verify_frame(i, out, len, frame);
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
	if (fails) {
		printf("FAIL: %d frames with wrong content\n", fails);
		return 1;
	}
	printf("OK\n");
	return 0;
}
