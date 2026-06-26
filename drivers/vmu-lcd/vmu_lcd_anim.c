/*
 * vmu_lcd_anim - play an animation on the SEGA Dreamcast VMU LCD.
 *
 * Each "frame" is the full 192-byte VMU framebuffer (48x32, 1bpp, 6 bytes per
 * row, MSB = leftmost pixel, row 0 at the top). The tool keeps the device open
 * and writes one frame per tick, pacing itself to the requested frame rate.
 *
 * Two sources of frames:
 *
 *   - the built-in "ball" demo (a bouncing ball), the default;
 *   - a raw stream of back-to-back 192-byte frames read from a file or stdin,
 *     e.g. as produced by frames2lcd.py.
 *
 * Examples:
 *
 *     vmu_lcd_anim                         # bouncing ball on /dev/vmu_lcd0
 *     vmu_lcd_anim -f 20 ball              # bouncing ball at 20 fps
 *     frames2lcd.py clip.gif | vmu_lcd_anim -l -    # loop a converted clip
 *     vmu_lcd_anim -o /dev/vmu_lcd1 clip.raw
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

#include "vmu_lcd.h"

static volatile sig_atomic_t stop;

static void on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

/*
 * The VMU LCD is slow: if a block write arrives while the device is still
 * painting the previous frame, the maple bus answers with "no response" and
 * the driver returns -ENXIO (the frame did not paint). This is transient - the
 * device is still there - so we briefly back off and resend the same frame. A
 * real unplug comes back as -ENODEV instead and is fatal.
 */
#define FRAME_RETRY_MAX	20
#define FRAME_RETRY_US	4000		/* 4 ms back-off between attempts */

/* Throughput counters, reported with -v. */
static unsigned long stat_frames;	/* frames presented (write accepted) */
static unsigned long stat_retries;	/* transient NAKs we resent through */
static unsigned long stat_drops;	/* frames abandoned after FRAME_RETRY_MAX */

/*
 * Write exactly one framebuffer, optionally rotated, retrying transient bus
 * conditions. Returns 0 on success (or a dropped frame), -1 on a fatal error.
 */
static int put_frame(int fd, unsigned char *fb, int flip)
{
	int attempts = 0;
	ssize_t n;

	if (flip)
		vmu_lcd_flip180(fb);

	for (;;) {
		n = write(fd, fb, LCD_FB_SIZE);
		if (n == LCD_FB_SIZE) {
			stat_frames++;
			return 0;
		}

		if (n < 0) {
			if (errno == EINTR) {
				if (stop)
					return 0;
				continue;
			}
			/* Transient: device busy/refreshing - back off and resend. */
			if (errno == EBUSY || errno == EAGAIN || errno == ENXIO) {
				if (stop || ++attempts > FRAME_RETRY_MAX) {
					stat_drops++;
					return 0;	/* give up on this frame */
				}
				stat_retries++;
				usleep(FRAME_RETRY_US);
				continue;
			}
			/* ENODEV (real unplug) or anything else: stop. */
			fprintf(stderr, "vmu_lcd_anim: write: %s\n",
				strerror(errno));
			return -1;
		}

		fprintf(stderr, "vmu_lcd_anim: short write (%zd/%d)\n",
			n, LCD_FB_SIZE);
		return -1;
	}
}

/* Sleep until the next frame deadline, tracking it absolutely to avoid drift. */
static void wait_tick(struct timespec *next, long period_ns)
{
	next->tv_nsec += period_ns;
	while (next->tv_nsec >= 1000000000L) {
		next->tv_nsec -= 1000000000L;
		next->tv_sec++;
	}
	while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, next, NULL) == EINTR)
		;
}

/* ---- bouncing ball demo ------------------------------------------------- */

static void fill_disc(unsigned char *fb, int cx, int cy, int r)
{
	int x, y;

	for (y = -r; y <= r; y++)
		for (x = -r; x <= r; x++)
			if (x * x + y * y <= r * r)
				vmu_lcd_set_pixel(fb, cx + x, cy + y);
}

static int run_ball(int fd, long period_ns, int flip)
{
	const int r = 4;
	/* Fixed-point position/velocity (8 fractional bits) for smooth motion. */
	int px = (LCD_W / 2) << 8, py = (LCD_H / 2) << 8;
	int vx = 137, vy = 91;		/* sub-pixel steps per frame */
	struct timespec next;
	unsigned char fb[LCD_FB_SIZE];

	clock_gettime(CLOCK_MONOTONIC, &next);

	while (!stop) {
		int cx, cy;

		px += vx;
		py += vy;

		/* Bounce off the walls, keeping the disc fully on screen. */
		if (px < (r << 8))			{ px = (r << 8); vx = -vx; }
		if (px > ((LCD_W - 1 - r) << 8))	{ px = (LCD_W - 1 - r) << 8; vx = -vx; }
		if (py < (r << 8))			{ py = (r << 8); vy = -vy; }
		if (py > ((LCD_H - 1 - r) << 8))	{ py = (LCD_H - 1 - r) << 8; vy = -vy; }

		cx = px >> 8;
		cy = py >> 8;

		memset(fb, 0, sizeof(fb));
		/* 1px frame border so the bounce reads clearly. */
		for (cx = 0; cx < LCD_W; cx++) {
			vmu_lcd_set_pixel(fb, cx, 0);
			vmu_lcd_set_pixel(fb, cx, LCD_H - 1);
		}
		for (cy = 0; cy < LCD_H; cy++) {
			vmu_lcd_set_pixel(fb, 0, cy);
			vmu_lcd_set_pixel(fb, LCD_W - 1, cy);
		}
		fill_disc(fb, px >> 8, py >> 8, r);

		if (put_frame(fd, fb, flip) < 0)
			return -1;

		wait_tick(&next, period_ns);
	}

	return 0;
}

/* ---- raw frame stream playback ------------------------------------------ */

static int run_stream(int fd, const char *path, long period_ns, int flip, int loop)
{
	struct timespec next;
	unsigned char fb[LCD_FB_SIZE];
	int in, ret = 0;
	int use_stdin = (!strcmp(path, "-"));

	clock_gettime(CLOCK_MONOTONIC, &next);

	do {
		if (use_stdin) {
			in = STDIN_FILENO;
		} else {
			in = open(path, O_RDONLY);
			if (in < 0) {
				fprintf(stderr, "vmu_lcd_anim: %s: %s\n",
					path, strerror(errno));
				return -1;
			}
		}

		for (;;) {
			size_t got = 0;
			ssize_t n;

			while (got < LCD_FB_SIZE) {
				n = read(in, fb + got, LCD_FB_SIZE - got);
				if (n < 0) {
					if (errno == EINTR)
						continue;
					fprintf(stderr, "vmu_lcd_anim: read: %s\n",
						strerror(errno));
					ret = -1;
					goto done;
				}
				if (n == 0)
					break;		/* EOF */
				got += (size_t)n;
			}

			if (got == 0)
				break;			/* clean end of stream */
			if (got != LCD_FB_SIZE) {
				fprintf(stderr, "vmu_lcd_anim: trailing partial "
					"frame (%zu bytes) ignored\n", got);
				break;
			}

			if (put_frame(fd, fb, flip) < 0) {
				ret = -1;
				goto done;
			}
			if (stop)
				goto done;

			wait_tick(&next, period_ns);
		}

done:
		if (!use_stdin)
			close(in);
		/* stdin can only be played once, even with -l. */
		if (use_stdin)
			break;
	} while (loop && !stop && ret == 0);

	return ret;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [-o DEV] [-f FPS] [-r] [-l] [-v] [ball | FILE | -]\n"
		"  -o DEV   output device (default /dev/vmu_lcd0)\n"
		"  -f FPS   max frames per second (default 30; it is only a ceiling -\n"
		"           the slow VMU LCD self-throttles below this)\n"
		"  -r       rotate 180 degrees (VMU mounted in a controller)\n"
		"  -l       loop a FILE source (ignored for stdin/ball)\n"
		"  -v       report achieved fps / retries / drops on exit\n"
		"  ball     bouncing ball demo (default with no source)\n"
		"  FILE     raw stream of 192-byte frames\n"
		"  -        read the raw frame stream from stdin\n",
		prog);
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/vmu_lcd0";
	const char *src = "ball";
	int fps = 30, flip = 0, loop = 0, verbose = 0;
	struct timespec t0, t1;
	double secs;
	long period_ns;
	int fd, ret, i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-o") && i + 1 < argc) {
			dev = argv[++i];
		} else if (!strcmp(argv[i], "-f") && i + 1 < argc) {
			fps = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "-r")) {
			flip = 1;
		} else if (!strcmp(argv[i], "-l")) {
			loop = 1;
		} else if (!strcmp(argv[i], "-v")) {
			verbose = 1;
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			return 0;
		} else if (argv[i][0] == '-' && argv[i][1] != '\0') {
			fprintf(stderr, "%s: unknown option '%s'\n",
				argv[0], argv[i]);
			usage(argv[0]);
			return 1;
		} else {
			src = argv[i];
		}
	}

	if (fps < 1)
		fps = 1;
	if (fps > 60)
		fps = 60;
	period_ns = 1000000000L / fps;

	fd = open(dev, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], dev, strerror(errno));
		return 1;
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	clock_gettime(CLOCK_MONOTONIC, &t0);

	if (!strcmp(src, "ball"))
		ret = run_ball(fd, period_ns, flip);
	else
		ret = run_stream(fd, src, period_ns, flip, loop);

	clock_gettime(CLOCK_MONOTONIC, &t1);
	close(fd);

	if (verbose) {
		secs = (t1.tv_sec - t0.tv_sec) +
		       (t1.tv_nsec - t0.tv_nsec) / 1e9;
		fprintf(stderr,
			"vmu_lcd_anim: %lu frames in %.1fs = %.1f fps "
			"(cap %d) | %lu retries, %lu dropped\n",
			stat_frames, secs,
			secs > 0 ? stat_frames / secs : 0.0, fps,
			stat_retries, stat_drops);
	}

	return ret ? 1 : 0;
}
