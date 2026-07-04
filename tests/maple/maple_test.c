// SPDX-License-Identifier: GPL-2.0
/*
 * maple_test - userspace Maple bus input tester for Dreamcast.
 *
 * Auto-scans /dev/input/event*, keeps Maple devices (controller, mouse,
 * keyboard), and prints a readable line for every event so you can see
 * on screen which device you use and which direction/button you press.
 *
 * Build (cross, SH4 / Dreamcast, musl):
 *   sh4-linux-musl-gcc -static -O2 -o maple_test maple_test.c
 * Build (native test on host):
 *   gcc -O2 -o maple_test maple_test.c
 *
 * Run:  ./maple_test          (reads all maple devices)
 *       ./maple_test --all     (don't filter by name; read every device)
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <linux/input.h>

#define MAX_DEV 32

struct dev {
	int fd;
	char name[256];
};

static const char *key_name(int code)
{
	switch (code) {
	/* controller */
	case BTN_A:	return "A";
	case BTN_B:	return "B";
	case BTN_C:	return "C";
	case BTN_X:	return "X";
	case BTN_Y:	return "Y";
	case BTN_Z:	return "Z";
	case BTN_START:	return "START";
	case BTN_SELECT:return "SELECT";
	/* mouse */
	case BTN_LEFT:	return "M-LEFT";
	case BTN_RIGHT:	return "M-RIGHT";
	case BTN_MIDDLE:return "M-MIDDLE";
	default:	return NULL;
	}
}

static void report(const char *dev, struct input_event *e)
{
	const char *k;

	switch (e->type) {
	case EV_KEY:
		k = key_name(e->code);
		if (!k)
			printf("[%s] key 0x%x %s\n", dev, e->code,
			       e->value ? "pressed" : "released");
		else
			printf("[%s] %s %s\n", dev, k,
			       e->value ? "pressed" : "released");
		break;
	case EV_ABS:
		switch (e->code) {
		case ABS_HAT0X:
			printf("[%s] D-pad %s\n", dev,
			       e->value < 0 ? "LEFT" :
			       e->value > 0 ? "RIGHT" : "X-center");
			break;
		case ABS_HAT0Y:
			printf("[%s] D-pad %s\n", dev,
			       e->value < 0 ? "UP" :
			       e->value > 0 ? "DOWN" : "Y-center");
			break;
		case ABS_X:
			printf("[%s] stick X=%d\n", dev, e->value);
			break;
		case ABS_Y:
			printf("[%s] stick Y=%d\n", dev, e->value);
			break;
		case ABS_RX:
			printf("[%s] stick2 X=%d\n", dev, e->value);
			break;
		case ABS_RY:
			printf("[%s] stick2 Y=%d\n", dev, e->value);
			break;
		case ABS_GAS:
			printf("[%s] R-trigger=%d\n", dev, e->value);
			break;
		case ABS_BRAKE:
			printf("[%s] L-trigger=%d\n", dev, e->value);
			break;
		default:
			printf("[%s] abs 0x%x=%d\n", dev, e->code, e->value);
			break;
		}
		break;
	case EV_REL:
		switch (e->code) {
		case REL_X:	printf("[%s] mouse dx=%d\n", dev, e->value); break;
		case REL_Y:	printf("[%s] mouse dy=%d\n", dev, e->value); break;
		case REL_WHEEL:	printf("[%s] mouse wheel=%d\n", dev, e->value); break;
		default:	printf("[%s] rel 0x%x=%d\n", dev, e->code, e->value); break;
		}
		break;
	default:
		break; /* skip EV_SYN, EV_MSC noise */
	}
}

/* case-insensitive substring; avoids GNU strcasestr (musl needs _GNU_SOURCE) */
static int ci_contains(const char *hay, const char *needle)
{
	size_t nl = strlen(needle);

	if (!nl)
		return 1;
	for (; *hay; hay++) {
		if (strncasecmp(hay, needle, nl) == 0)
			return 1;
	}
	return 0;
}

static int is_maple(const char *name)
{
	/* Maple drivers set dev->name = mdev->product_name, which for
	 * real peripherals reads e.g. "Dreamcast Controller". Match loosely.
	 */
	return ci_contains(name, "dreamcast") || ci_contains(name, "maple");
}

int main(int argc, char **argv)
{
	struct dev devs[MAX_DEV];
	struct pollfd pfd[MAX_DEV];
	int filter = 1, n = 0;
	DIR *d;
	struct dirent *de;

	/* unbuffered: each event prints now, even when stdout is a file/pipe/
	 * fbcon (otherwise libc full-buffers 4 KB and sparse controller events
	 * appear in bursts while high-rate mouse events flush quickly).
	 */
	setvbuf(stdout, NULL, _IONBF, 0);

	if (argc > 1 && !strcmp(argv[1], "--all"))
		filter = 0;

	d = opendir("/dev/input");
	if (!d) {
		perror("opendir /dev/input");
		return 1;
	}

	while ((de = readdir(d)) && n < MAX_DEV) {
		char path[300];
		char name[256] = "unknown";
		int fd;

		if (strncmp(de->d_name, "event", 5))
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		if (filter && !is_maple(name)) {
			close(fd);
			continue;
		}
		devs[n].fd = fd;
		snprintf(devs[n].name, sizeof(devs[n].name), "%s", name);
		pfd[n].fd = fd;
		pfd[n].events = POLLIN;
		printf("watching %s = \"%s\"\n", path, name);
		n++;
	}
	closedir(d);

	if (!n) {
		fprintf(stderr, "no %sinput devices found%s\n",
			filter ? "maple " : "",
			filter ? " (try --all)" : "");
		return 1;
	}
	printf("--- press buttons / move stick / move mouse (Ctrl-C to quit) ---\n");

	for (;;) {
		int i, r = poll(pfd, n, -1);

		if (r < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			return 1;
		}
		for (i = 0; i < n; i++) {
			struct input_event ev[16];
			ssize_t rd;

			if (!(pfd[i].revents & POLLIN))
				continue;
			rd = read(devs[i].fd, ev, sizeof(ev));
			if (rd < (ssize_t)sizeof(ev[0]))
				continue;
			for (size_t j = 0; j < rd / sizeof(ev[0]); j++)
				report(devs[i].name, &ev[j]);
		}
		fflush(stdout);
	}
}
