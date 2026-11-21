/*
 * vmu_lcd_status - Tux status screen for the SEGA Dreamcast VMU LCD.
 *
 * Shows the kernel's splash Tux on the 48x32 display of every plugged-in
 * VMU (the same picture on all of them) and uses it as a
 * status indicator:
 *
 *   - while waiting for a network address, Tux blinks next to "DHCP ...";
 *   - when an address appears (or changes), Tux shows it in a speech bubble
 *     for a few seconds;
 *   - otherwise Tux's belly fills up with used RAM, RAM and swap usage are
 *     printed next to him, he sweats while the system is actively swapping
 *     and gets dizzy when it is nearly out of memory (available RAM plus
 *     free swap below 10% of RAM, i.e. 90% used without swap).
 *
 * On SIGTERM / SIGINT the plain splash (Tux + "Linux") is drawn again.
 *
 * Self-contained (no vmu_lcd.h), so the package can fetch this single file.
 * The framebuffer is 192 bytes: 6 bytes per row, MSB = leftmost pixel, row 0
 * at the top. The driver only accepts the whole frame in one write().
 *
 *     vmu_lcd_status -r &          # VMU sitting in a controller
 *     vmu_lcd_status -p mood > f   # render one page to stdout
 *
 * Builds against a normal libc or, much smaller, against the kernel's nolibc
 * (static, no libc needed at all):
 *
 *     sh4-linux-gnu-gcc -Os -s -static -nostdlib -fno-asynchronous-unwind-tables \
 *         -include nolibc.h -I <linux>/tools/include/nolibc \
 *         -o vmu_lcd_status vmu_lcd_status.c -lgcc
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>

#ifndef NOLIBC
#include <sys/socket.h>
#else
/*
 * nolibc has neither socket() nor signal(); both are one syscall away.
 * SH uses the generic sigaction layout; without SA_RESTORER the kernel
 * returns through the vdso (or a trampoline on the stack).
 */
#include <linux/sockios.h>
#define AF_INET		2
#define SOCK_DGRAM	2

static int socket(int domain, int type, int protocol)
{
	return __sysret(__nolibc_syscall3(__NR_socket, domain, type, protocol));
}

typedef void (*sighandler_t)(int);
typedef int sig_atomic_t;

struct k_sigaction {
	sighandler_t handler;
	unsigned long flags;
	void (*restorer)(void);
	unsigned long mask[2];
};

static sighandler_t signal(int sig, sighandler_t handler)
{
	struct k_sigaction sa = { handler, 0, 0, { 0, 0 } };

	if (__nolibc_syscall4(__NR_rt_sigaction, sig, &sa, 0,
			      sizeof(sa.mask)) < 0)
		return (sighandler_t)-1;
	return 0;
}
#endif

#define W		48
#define H		32
#define STRIDE		(W / 8)
#define FB_SIZE		(STRIDE * H)		/* 192 bytes */

#define REDRAW_TICKS	10		/* rewrite an unchanged frame now and then */
#define DIZZY_PCT	10		/* RAM avail + swap free below this % */

/* ------------------------------------------------------------------ */
/* Pixel buffer                                                        */
/* ------------------------------------------------------------------ */

static unsigned char px[H][W];

static void pset(int x, int y, int v)
{
	if (x >= 0 && x < W && y >= 0 && y < H)
		px[y][x] = v;
}

static void rect(int x, int y, int w, int h, int v)
{
	int i, j;

	for (j = y; j < y + h; j++)
		for (i = x; i < x + w; i++)
			pset(i, j, v);
}

static void frame(int x, int y, int w, int h)
{
	int i;

	for (i = x; i < x + w; i++) {
		pset(i, y, 1);
		pset(i, y + h - 1, 1);
	}
	for (i = y; i < y + h; i++) {
		pset(x, i, 1);
		pset(x + w - 1, i, 1);
	}
}

static void hline(int x0, int x1, int y, int v)
{
	for (; x0 <= x1; x0++)
		pset(x0, y, v);
}

static void bitmap(const char *const *rows, int n, int x, int y, int v)
{
	int i, j;

	for (j = 0; j < n; j++)
		for (i = 0; rows[j][i]; i++)
			if (rows[j][i] == '#')
				pset(x + i, y + j, v);
}

static void pack(unsigned char *fb, int flip)
{
	int x, y, sx, sy;

	memset(fb, 0, FB_SIZE);
	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++) {
			sx = flip ? W - 1 - x : x;
			sy = flip ? H - 1 - y : y;
			if (px[sy][sx])
				fb[y * STRIDE + x / 8] |= 0x80 >> (x % 8);
		}
}

/* ------------------------------------------------------------------ */
/* Fonts (variable width, '#' = pixel on)                              */
/* ------------------------------------------------------------------ */

struct glyph {
	char c;
	const char *r[6];
};

struct font {
	int h;
	const struct glyph *g;
	int n;
};

/* 3x5 */
static const struct glyph f3_glyphs[] = {
	{ '0', { "###", "#.#", "#.#", "#.#", "###" } },
	{ '1', { ".#.", "##.", ".#.", ".#.", "###" } },
	{ '2', { "##.", "..#", ".#.", "#..", "###" } },
	{ '3', { "##.", "..#", ".#.", "..#", "##." } },
	{ '4', { "#.#", "#.#", "###", "..#", "..#" } },
	{ '5', { "###", "#..", "##.", "..#", "##." } },
	{ '6', { ".##", "#..", "###", "#.#", "###" } },
	{ '7', { "###", "..#", ".#.", ".#.", ".#." } },
	{ '8', { "###", "#.#", "###", "#.#", "###" } },
	{ '9', { "###", "#.#", "###", "..#", "##." } },
	{ 'A', { ".#.", "#.#", "###", "#.#", "#.#" } },
	{ 'C', { ".##", "#..", "#..", "#..", ".##" } },
	{ 'D', { "##.", "#.#", "#.#", "#.#", "##." } },
	{ 'F', { "###", "#..", "##.", "#..", "#.." } },
	{ 'H', { "#.#", "#.#", "###", "#.#", "#.#" } },
	{ 'M', { "#...#", "##.##", "#.#.#", "#...#", "#...#" } },
	{ 'O', { ".#.", "#.#", "#.#", "#.#", ".#." } },
	{ 'P', { "##.", "#.#", "##.", "#..", "#.." } },
	{ 'R', { "##.", "#.#", "##.", "#.#", "#.#" } },
	{ 'S', { ".##", "#..", ".#.", "..#", "##." } },
	{ 'W', { "#...#", "#...#", "#.#.#", "##.##", "#...#" } },
	{ '%', { "#.#", "..#", ".#.", "#..", "#.#" } },
	{ '.', { ".", ".", ".", ".", "#" } },
	{ '-', { "..", "..", "##", "..", ".." } },
	{ ' ', { "..", "..", "..", "..", ".." } },
};

/* 4x6, the digits of vmu_lcd_text without its blank top row */
static const struct glyph f4_glyphs[] = {
	{ '0', { ".##.", "#..#", "#.##", "##.#", "#..#", ".##." } },
	{ '1', { "..#.", ".##.", "..#.", "..#.", "..#.", ".###" } },
	{ '2', { ".##.", "#..#", "...#", ".##.", "#...", "####" } },
	{ '3', { "###.", "...#", ".##.", "...#", "#..#", ".##." } },
	{ '4', { "..#.", ".##.", "#.#.", "####", "..#.", "..#." } },
	{ '5', { "####", "#...", "###.", "...#", "#..#", ".##." } },
	{ '6', { ".##.", "#...", "###.", "#..#", "#..#", ".##." } },
	{ '7', { "####", "...#", "..#.", ".#..", ".#..", ".#.." } },
	{ '8', { ".##.", "#..#", ".##.", "#..#", "#..#", ".##." } },
	{ '9', { ".##.", "#..#", "#..#", ".###", "...#", ".##." } },
	{ '.', { ".", ".", ".", ".", ".", "#" } },
	{ ' ', { "..", "..", "..", "..", "..", ".." } },
};

static const struct font F3 = { 5, f3_glyphs, sizeof(f3_glyphs) / sizeof(f3_glyphs[0]) };
static const struct font F4 = { 6, f4_glyphs, sizeof(f4_glyphs) / sizeof(f4_glyphs[0]) };

static const struct glyph *find_glyph(const struct font *f, char c)
{
	int i;

	for (i = 0; i < f->n; i++)
		if (f->g[i].c == c)
			return &f->g[i];
	return &f->g[f->n - 1];		/* space */
}

static int text_w(const struct font *f, const char *s)
{
	int w = 0;

	for (; *s; s++)
		w += (int)strlen(find_glyph(f, *s)->r[0]) + 1;
	return w ? w - 1 : 0;
}

static void text(const struct font *f, const char *s, int x, int y)
{
	const struct glyph *g;

	for (; *s; s++) {
		g = find_glyph(f, *s);
		bitmap(g->r, f->h, x, y, 1);
		x += (int)strlen(g->r[0]) + 1;
	}
}

static void text_right(const struct font *f, const char *s, int y)
{
	text(f, s, W - text_w(f, s), y);
}

/* ------------------------------------------------------------------ */
/* Tux (the vmu-lcd driver's splash, stored upright here)              */
/* ------------------------------------------------------------------ */

static const char *const tux[H] = {
	"................................................",
	"...........#####................................",
	"..........########..............................",
	"..........########......#...#...................",
	"..........#########.....#.......................",
	".........##.###.###.....#...#.#.#..#.#..#.#.....",
	".........#.###.#.##.....#...#.##.#.#.#...#......",
	".........#.#####.##.....#...#.#..#.#.#..#.#.....",
	"..........#########.....###.#.#..#..#.#.#.#.....",
	"..........##########............................",
	"..........##########............................",
	"..........#####..###............................",
	".........#........###...........................",
	"........##........####..........................",
	".......##.........####..........................",
	".......##..........####.........................",
	"......##...........#####........................",
	"......##............####........................",
	"......#.............#####.......................",
	".....##.............#####.......................",
	".....##.............#####.......................",
	"....###.............#####.......................",
	"....###.............#####.......................",
	"....####...........######.......................",
	"..#######.........#######.......................",
	".##########.......########......................",
	".##########.......#########.....................",
	".#########.......###########....................",
	"###########....############.....................",
	"##########################......................",
	"..######################........................",
	"......#####.......####..........................",
};

#define TUX_W		28	/* Tux alone; "Linux" starts at x=24, rows 3-8 */
#define BELLY_TOP	11	/* rows above are the neck gap */

static unsigned char belly[H][W];
static int belly_rows[H];	/* belly pixels per row (y > BELLY_TOP) */
static int belly_total;

static unsigned char stack[W * H][2];
static int sp;

static void belly_push(int x, int y)
{
	if (x < 0 || x >= TUX_W || y < 0 || y >= H)
		return;
	if (belly[y][x] || tux[y][x] == '#')
		return;
	belly[y][x] = 1;		/* mark on push: each pixel once */
	if (y > BELLY_TOP) {
		belly_rows[y]++;
		belly_total++;
	}
	stack[sp][0] = x;
	stack[sp][1] = y;
	sp++;
}

static void belly_init(void)
{
	int x, y;

	belly_push(13, 20);		/* a pixel inside the belly */
	while (sp) {
		sp--;
		x = stack[sp][0];
		y = stack[sp][1];
		belly_push(x + 1, y);
		belly_push(x - 1, y);
		belly_push(x, y + 1);
		belly_push(x, y - 1);
	}
}

static void draw_tux(int with_text)
{
	int x, y, cut;

	memset(px, 0, sizeof(px));
	for (y = 0; y < H; y++) {
		cut = with_text ? W : (y > 8 ? TUX_W : 20);
		for (x = 0; x < cut; x++)
			if (tux[y][x] == '#')
				px[y][x] = 1;
	}
}

/* Fill from the bottom so that the filled AREA matches part / whole. */
static void fill_belly(long part, long whole)
{
	int n, acc = 0, level = -1, x, y;

	if (whole <= 0)
		return;
	if (part < 0)
		part = 0;
	if (part > whole)
		part = whole;
	n = (int)((part * belly_total + whole / 2) / whole);
	if (!n)
		return;
	for (y = H - 1; y > BELLY_TOP; y--) {
		acc += belly_rows[y];
		if (acc >= n) {
			level = y;
			break;
		}
	}
	for (y = BELLY_TOP + 1; y < H; y++)
		for (x = 0; x < TUX_W; x++) {
			if (!belly[y][x])
				continue;
			if (y == level || (y > level && (x + y) % 2 == 0))
				px[y][x] = 1;
		}
}

static void eyes_blink(void)
{
	rect(10, 4, 9, 4, 1);
	hline(11, 12, 6, 0);
	hline(16, 17, 6, 0);
}

static void eyes_dizzy(void)
{
	static const char *const star[] = { ".#.", "#.#", ".#." };
	int ex, i;

	rect(10, 4, 9, 4, 1);
	for (ex = 11; ex <= 15; ex += 4)
		for (i = 0; i < 3; i++) {
			pset(ex + i, 4 + i, 0);
			pset(ex + 2 - i, 4 + i, 0);
		}
	bitmap(star, 3, 4, 2, 1);
	bitmap(star, 3, 21, 1, 1);
	bitmap(star, 3, 24, 7, 1);
}

static void sweat(void)
{
	static const char *const drop[] = { ".#.", "#.#", "#.#", ".#." };

	bitmap(drop, 4, 21, 2, 1);
	pset(25, 6, 1);
	pset(25, 7, 1);
}

/* ------------------------------------------------------------------ */
/* System state                                                        */
/* ------------------------------------------------------------------ */

struct stats {
	long mem_total, mem_avail, swap_total, swap_free;
	long pswp;		/* pswpin + pswpout */
};

static char pbuf[8192];

/* Read a whole /proc file into pbuf, NUL terminated. */
static int read_proc(const char *path)
{
	int fd, len = 0;
	ssize_t n;

	pbuf[0] = 0;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	while (len < (int)sizeof(pbuf) - 1 &&
	       (n = read(fd, pbuf + len, sizeof(pbuf) - 1 - len)) > 0)
		len += n;
	pbuf[len] = 0;
	close(fd);
	return 0;
}

/* Value of "key <spaces> number" at the start of a line, else 0. */
static long proc_field(const char *key)
{
	size_t n = strlen(key);
	const char *p = pbuf;

	while (*p) {
		if (!strncmp(p, key, n) && (p[n] == ' ' || p[n] == '\t')) {
			p += n;
			while (*p == ' ' || *p == '\t')
				p++;
			return atol(p);
		}
		while (*p && *p != '\n')
			p++;
		if (*p)
			p++;
	}
	return 0;
}

static void read_stats(struct stats *st)
{
	memset(st, 0, sizeof(*st));
	if (!read_proc("/proc/meminfo")) {
		st->mem_total = proc_field("MemTotal:");
		st->mem_avail = proc_field("MemAvailable:");
		st->swap_total = proc_field("SwapTotal:");
		st->swap_free = proc_field("SwapFree:");
	}
	/*
	 * Keep the percentage maths in 32-bit longs (no 64-bit division from
	 * libgcc): count in MB instead of kB above 1 GB.
	 */
	if (st->mem_total > (1L << 20) || st->swap_total > (1L << 20)) {
		st->mem_total >>= 10;
		st->mem_avail >>= 10;
		st->swap_total >>= 10;
		st->swap_free >>= 10;
	}
	if (!read_proc("/proc/vmstat"))
		st->pswp = proc_field("pswpin") + proc_field("pswpout");
}

/*
 * IPv4 address of the first listed interface that has one.  Uses a raw
 * struct ifreq (16 byte name, then a sockaddr_in: family, port, address)
 * so no network headers are needed.
 */
static int get_ip(char *const *ifs, int nifs, unsigned char ip[4])
{
	unsigned char ifr[40];
	int fd, i, ok = 0;

	memset(ip, 0, 4);
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return 0;
	for (i = 0; i < nifs && !ok; i++) {
		memset(ifr, 0, sizeof(ifr));
		strncpy((char *)ifr, ifs[i], 15);
		if (ioctl(fd, SIOCGIFADDR, ifr) < 0)
			continue;
		memcpy(ip, ifr + 20, 4);
		ok = ip[0] | ip[1] | ip[2] | ip[3];
	}
	close(fd);
	return ok != 0;
}

/* ------------------------------------------------------------------ */
/* Pages                                                               */
/* ------------------------------------------------------------------ */

static void page_splash(void)
{
	draw_tux(1);
}

static void page_dhcp(int phase)
{
	int i;

	draw_tux(0);
	if (phase % 4 == 3)
		eyes_blink();
	text_right(&F3, "DHCP", 10);
	for (i = 0; i <= phase % 3; i++)
		rect(33 + i * 5, 19, 2, 2, 1);
}

static void page_ip(const unsigned char *ip)
{
	const int bx = 24, by = 0, bw = 24, bh = 29;
	char lines[4][16];
	const struct font *f = &F4;
	int n, i, step = 9;

	draw_tux(0);
	rect(bx, by, bw, bh, 0);
	frame(bx, by, bw, bh);
	pset(bx, by, 0);			/* rounded corners */
	pset(bx + bw - 1, by, 0);
	pset(bx, by + bh - 1, 0);
	pset(bx + bw - 1, by + bh - 1, 0);
	pset(bx, 8, 0);				/* tail toward the beak */
	pset(bx, 9, 0);
	pset(bx - 1, 8, 1);
	pset(bx - 1, 9, 1);
	pset(bx - 2, 9, 1);

	/* a. / b. / c.d in the 4x6 font, else one octet per line in 3x5 */
	snprintf(lines[0], sizeof(lines[0]), "%d.", ip[0]);
	snprintf(lines[1], sizeof(lines[1]), "%d.", ip[1]);
	snprintf(lines[2], sizeof(lines[2]), "%d.%d", ip[2], ip[3]);
	n = 3;
	for (i = 0; i < n; i++)
		if (text_w(&F4, lines[i]) > bw - 4)
			f = NULL;
	if (!f) {
		f = &F3;
		step = 7;
		n = 4;
		for (i = 0; i < 4; i++)
			snprintf(lines[i], sizeof(lines[i]), "%d%s", ip[i],
				 i < 3 ? "." : "");
	}
	for (i = 0; i < n; i++)
		text(f, lines[i], bx + bw - 3 - text_w(f, lines[i]),
		     by + 2 + i * step);
}

static int pct(long part, long whole)
{
	return whole > 0 ? (int)((100 * part + whole / 2) / whole) : 0;
}

static void page_mood(const struct stats *st, int swapping)
{
	long used = st->mem_total - st->mem_avail;
	long swap_used = st->swap_total - st->swap_free;
	char buf[16];

	draw_tux(0);
	fill_belly(used, st->mem_total);
	if (st->mem_total > 0 &&
	    100 * (st->mem_avail + st->swap_free) < st->mem_total * DIZZY_PCT)
		eyes_dizzy();
	else if (swapping)
		sweat();

	text_right(&F3, "RAM", 1);
	snprintf(buf, sizeof(buf), "%d%%", pct(used, st->mem_total));
	text_right(&F3, buf, 9);
	text_right(&F3, "SWP", 17);
	if (st->swap_total > 0)
		snprintf(buf, sizeof(buf), "%d%%", pct(swap_used, st->swap_total));
	else
		snprintf(buf, sizeof(buf), "OFF");
	text_right(&F3, buf, 25);
}

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

/*
 * Every VMU with a display gets the same picture.  The driver names them
 * vmu_lcd0..vmu_lcd7 (lowest free number first), so all eight names are
 * probed each frame; a VMU plugged in later shows Tux on the next frame.
 * -o restricts the output to one device.
 */
#define MAX_LCDS	8

struct lcd {
	int fd;
	int have_last;
	unsigned char last[FB_SIZE];
};

static volatile sig_atomic_t stop;
static const char *dev;			/* -o: only this device */
static int flip;
static struct lcd lcds[MAX_LCDS];

static void on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

/*
 * Present the current pixel buffer on one display; skips unchanged frames.
 *
 * One write per frame, no tight retry loop: every LCD write shares the
 * maple bus and the VMU with the memory card, and hammering it makes
 * vmufat reads fail with "VMU ... is busy".
 *
 *   ENXIO  the VMU did not answer: it was pulled, but the kernel keeps
 *          the device while its memory card is registered.  Count the
 *          frame as drawn so an empty slot is only retried on a change
 *          or after REDRAW_TICKS.
 *   EIO    the driver timed out but left the packet queued; it still lands.
 *   EBUSY, EAGAIN, EINTR
 *          the VMU is busy (e.g. with a flash read): try again next tick.
 *   other  gone or wedged: reopen next tick.
 */
static void present_one(struct lcd *l, const char *path,
			const unsigned char *fb)
{
	ssize_t n;

	if (l->fd < 0) {
		l->fd = open(path, O_WRONLY);
		if (l->fd < 0)
			return;			/* no VMU there (yet) */
		l->have_last = 0;
	}
	if (l->have_last && !memcmp(fb, l->last, FB_SIZE))
		return;

	n = write(l->fd, fb, FB_SIZE);
	if (n == FB_SIZE || (n < 0 && (errno == ENXIO || errno == EIO))) {
		memcpy(l->last, fb, FB_SIZE);
		l->have_last = 1;
		return;
	}
	if (n < 0 && (errno == EBUSY || errno == EAGAIN || errno == EINTR))
		return;
	close(l->fd);
	l->fd = -1;
}

/* Present the current pixel buffer on every VMU display (or just -o). */
static void present(void)
{
	char path[] = "/dev/vmu_lcd0";
	unsigned char fb[FB_SIZE];
	int i;

	pack(fb, flip);
	if (dev) {
		present_one(&lcds[0], dev, fb);
		return;
	}
	for (i = 0; i < MAX_LCDS; i++) {
		path[sizeof(path) - 2] = '0' + i;
		present_one(&lcds[i], path, fb);
	}
}

/* Repaint everything on the next present(), e.g. after a VMU hot-swap. */
static void forget_frames(void)
{
	int i;

	for (i = 0; i < MAX_LCDS; i++)
		lcds[i].have_last = 0;
}

static void sleep_ms(long ms)
{
	struct timespec ts;

	while (ms > 0 && !stop) {
		long step = ms > 200 ? 200 : ms;

		ts.tv_sec = 0;
		ts.tv_nsec = step * 1000000L;
		nanosleep(&ts, NULL);
		ms -= step;
	}
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [-r] [-o dev] [-i iface] [-t sec] [-s sec] [-w sec] [-b]\n"
		"       %s [-r] -p splash|dhcp|ip|mood   (one frame to stdout)\n"
		"  -r  rotate 180 degrees (VMU in a controller)\n"
		"  -o  only this LCD device (default: all /dev/vmu_lcd0..7)\n"
		"  -i  interface to watch (default: eth0, then ppp0)\n"
		"  -t  refresh interval (default 3 s)\n"
		"  -s  how long to show a new IP address (default 6 s)\n"
		"  -w  how long to wait for an address at start (default 10 s)\n"
		"  -b  run in the background\n",
		prog, prog);
}

int main(int argc, char **argv)
{
	static char *default_ifs[] = { "eth0", "ppp0" };
	char *one_if[1];
	char **ifs = default_ifs;
	int nifs = 2;
	int interval = 3, ip_secs = 6, wait_secs = 10, bg = 0;
	const char *page = NULL;
	unsigned char ip[4], shown_ip[4] = { 0, 0, 0, 0 };
	struct stats st;
	long prev_pswp = -1;
	int opt, phase = 0, tick = 0;

	while ((opt = getopt(argc, argv, "ro:i:t:s:w:bp:h")) != -1) {
		switch (opt) {
		case 'r': flip = 1; break;
		case 'o': dev = optarg; break;
		case 'i': one_if[0] = optarg; ifs = one_if; nifs = 1; break;
		case 't': interval = atoi(optarg); break;
		case 's': ip_secs = atoi(optarg); break;
		case 'w': wait_secs = atoi(optarg); break;
		case 'b': bg = 1; break;
		case 'p': page = optarg; break;
		default: usage(argv[0]); return opt == 'h' ? 0 : 1;
		}
	}
	if (interval < 1)
		interval = 1;
	for (opt = 0; opt < MAX_LCDS; opt++)
		lcds[opt].fd = -1;

	belly_init();

	if (page) {
		unsigned char fb[FB_SIZE];

		read_stats(&st);
		get_ip(ifs, nifs, ip);
		if (!strcmp(page, "splash"))
			page_splash();
		else if (!strcmp(page, "dhcp"))
			page_dhcp(2);
		else if (!strcmp(page, "ip"))
			page_ip(ip);
		else if (!strcmp(page, "mood"))
			page_mood(&st, 0);
		else {
			usage(argv[0]);
			return 1;
		}
		pack(fb, flip);
		return write(1, fb, FB_SIZE) == FB_SIZE ? 0 : 1;
	}

	if (bg) {
		pid_t pid = fork();

		if (pid < 0) {
			perror("fork");
			return 1;
		}
		if (pid > 0)
			return 0;
		setsid();
		opt = open("/dev/null", O_RDWR);
		if (opt >= 0) {
			dup2(opt, 0);
			dup2(opt, 1);
			dup2(opt, 2);
			if (opt > 2)
				close(opt);
		}
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, on_signal);

	/* Waiting for an address: Tux blinks, ~2 frames per second. */
	while (!stop && !get_ip(ifs, nifs, ip) && phase < wait_secs * 2) {
		page_dhcp(phase++);
		present();
		sleep_ms(500);
	}

	while (!stop) {
		if (get_ip(ifs, nifs, ip) && memcmp(ip, shown_ip, 4)) {
			memcpy(shown_ip, ip, 4);
			page_ip(ip);
			present();
			sleep_ms(ip_secs * 1000L);
			continue;
		}

		read_stats(&st);
		page_mood(&st, prev_pswp >= 0 && st.pswp != prev_pswp);
		prev_pswp = st.pswp;
		if (++tick % REDRAW_TICKS == 0)
			forget_frames();	/* repaint after a VMU hot-swap */
		present();
		sleep_ms(interval * 1000L);
	}

	stop = 0;			/* let present() run once more */
	page_splash();
	forget_frames();
	present();
	for (opt = 0; opt < MAX_LCDS; opt++)
		if (lcds[opt].fd >= 0)
			close(lcds[opt].fd);
	return 0;
}
