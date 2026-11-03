// SPDX-License-Identifier: GPL-2.0
/*
 * ttytype TTY [TEXT] - type TEXT plus Enter into TTY via TIOCSTI, as if
 * typed on the DC's keyboard, so it shows up on the framebuffer console.
 * Needs CONFIG_LEGACY_TIOCSTI=y. Build natively on the DC with plain
 * `cc -o ttytype ttytype.c` (no -O flags).
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *s = argc > 2 ? argv[2] : "";
	char cr = 13;
	int fd;

	if (argc < 2) {
		fprintf(stderr, "usage: ttytype TTY [TEXT]\n");
		return 2;
	}
	fd = open(argv[1], O_RDWR | O_NOCTTY);
	if (fd < 0) {
		perror(argv[1]);
		return 1;
	}
	for (; *s; s++) {
		if (ioctl(fd, TIOCSTI, s) < 0) {
			perror("TIOCSTI");
			return 1;
		}
		usleep(90000);
	}
	usleep(300000);
	if (ioctl(fd, TIOCSTI, &cr) < 0) {
		perror("TIOCSTI");
		return 1;
	}
	close(fd);
	return 0;
}
