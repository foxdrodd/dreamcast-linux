#define _GNU_SOURCE
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

/*
 * Tiny PID 1 for Dreamcast Linux.
 *
 * Replaces busybox as the first-stage init: it only mounts the gdrom,
 * stacks a writable overlay over it, chroots into the musl sysroot and
 * then runs an mksh login shell on each console.  No second shell layer,
 * no `script` wrapper, no busybox resident in RAM.
 *
 * Console policy (matches busybox "askfirst" inittab behaviour):
 *   - physical framebuffer console (/dev/tty0) and serial console
 *     (/dev/ttySC1): no shell process exists until a key arrives on the
 *     line, then it is respawned on each exit.
 *     For the serial line this doubles as connection detection - the
 *     Dreamcast coder's cable is 3-wire (no DCD/DSR), so a keypress is
 *     the only reliable "someone is connected" signal.
 *
 * build like:
 * sh4-linux-gnu-gcc -fno-asynchronous-unwind-tables -fno-ident -s -Os -nostdlib \
 *   -static -include nolibc.h -o init init.c -lgcc -I ~/devel/linux/tools/include/nolibc
 */

#ifndef TIOCSCTTY
#define TIOCSCTTY 0x540E
#endif

struct console {
	const char  *path;     /* tty device node                          */
	char *const *envp;     /* environment (mainly TERM) for the shell  */
	int          askfirst; /* wait for a keypress before spawning      */
	pid_t        pid;      /* current manager pid, -1 when not running */
};

static char *const envp_fb[] = {
	"HOME=/root",
	"PATH=/bin:/sbin:/usr/bin:/usr/sbin",
	"TERM=linux",
	NULL
};

static char *const envp_serial[] = {
	"HOME=/root",
	"PATH=/bin:/sbin:/usr/bin:/usr/sbin",
	"TERM=vt100",
	NULL
};

static struct console consoles[] = {
	{ "/dev/tty0",   envp_fb,     1, -1 },  /* physical: framebuffer + maple kbd */
	{ "/dev/ttySC1", envp_serial, 1, -1 },  /* serial: askfirst                  */
};
#define NR_CONSOLES (sizeof(consoles) / sizeof(consoles[0]))

static const char askfirst_msg[] =
	"\r\nPlease press Enter to activate this console.\r\n";

/*
 * Run one mksh login shell on `c` to completion, with the shell itself as
 * the session leader and controlling-terminal owner so job control works.
 */
static void run_shell_once(const struct console *c)
{
	int fd = open(c->path, O_RDWR | O_NOCTTY);

	if (fd < 0) {
		/* device not ready yet - back off so we never busy-spin */
		usleep(500000);
		return;
	}

	if (c->askfirst) {
		char ch;

		write(fd, askfirst_msg, sizeof(askfirst_msg) - 1);

		/* block until the user presses Enter; bail on hangup/EOF */
		while (read(fd, &ch, 1) == 1 && ch != '\n')
			;
	}

	pid_t pid = fork();

	if (pid < 0) {
		close(fd);
		usleep(500000);
		return;
	}

	if (pid == 0) {
		char *const argv[] = { "/bin/mksh", "-l", NULL };

		setsid();
		ioctl(fd, TIOCSCTTY, 1);

		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			close(fd);

		execve("/bin/mksh", argv, c->envp);
		perror("execve /bin/mksh");
		_exit(127);
	}

	close(fd);
	waitpid(pid, NULL, 0);
}

/*
 * Per-console manager: respawns the shell forever.  Lives as its own tiny
 * process (shared text from the static init binary), idle-blocked in read()
 * for askfirst consoles, so an unused serial port costs almost nothing.
 */
static void run_console(const struct console *c)
{
	for (;;)
		run_shell_once(c);
}

static int tty_exists(const char *path)
{
	struct stat st;

	if (stat(path, &st) < 0)
		return 0;

	return S_ISCHR(st.st_mode);
}

static void start_manager(struct console *c)
{
	pid_t pid = fork();

	if (pid < 0) {
		perror("fork");
		c->pid = -1;
		return;
	}

	if (pid == 0) {
		run_console(c);
		_exit(0); /* never reached */
	}

	c->pid = pid;
}

static int xmkdir(const char *path)
{
	if (mkdir(path, 0755) < 0 && errno != EEXIST) {
		fprintf(stderr, "mkdir %s: %s\n", path, strerror(errno));
		return -1;
	}
	return 0;
}

static int bind_mount(const char *src, const char *dst)
{
	if (xmkdir(dst) < 0)
		return -1;

	if (mount(src, dst, NULL, MS_BIND | MS_REC, NULL) < 0) {
		fprintf(stderr, "bind mount %s -> %s: %s\n",
			src, dst, strerror(errno));
		return -1;
	}

	return 0;
}

int main(void)
{
	int ret;

	ret = mount("proc",  "/proc", "proc",    0, "");
	if (ret) printf("proc failed with %d\n", ret);
	ret = mount("sysfs", "/sys",  "sysfs",   0, "");
	if (ret) printf("sysfs failed with %d\n", ret);
	ret = mount("devtmpfs", "/dev", "devtmpfs", 0, "mode=0755");
	if (ret) printf("devtmpfs failed with %d\n", ret);
	ret = mount("devpts",  "/dev/pts", "devpts", 0, "");
	if (ret) printf("devpts failed with %d\n", ret);

	ret = mount("/dev/gdrom", "/media", "iso9660", MS_RDONLY, NULL);
	if (ret) printf("gdrom failed with %d\n", ret);
	ret = mount("overlay", "/run/overlay-root", "overlay", 0,
		    "rw,lowerdir=/media,upperdir=/run/overlay-rw/upper,workdir=/run/overlay-rw/work,redirect_dir=nofollow,uuid=null");
	if (ret) printf("overlay failed with %d\n", ret);

	ret = chdir("/run/overlay-root");
	if (ret) printf("chdir overlay-root failed with %d\n", ret);
	bind_mount("/dev",     "/run/overlay-root/dev");
	bind_mount("/dev/pts", "/run/overlay-root/dev/pts");
	bind_mount("/proc",    "/run/overlay-root/proc");
	bind_mount("/sys",     "/run/overlay-root/sys");

	ret = chroot("/run/overlay-root");
	if (ret) printf("chroot failed with %d\n", ret);
	chdir("/");

	if (access("/bin/mksh", X_OK) < 0)
		perror("err /bin/mksh");

	/* Launch a manager per console that actually exists. */
	for (size_t i = 0; i < NR_CONSOLES; i++) {
		if (tty_exists(consoles[i].path))
			start_manager(&consoles[i]);
		else
			fprintf(stderr, "console %s absent, skipping\n",
				consoles[i].path);
	}

	/*
	 * Reap children. A reaped pid that matches a console manager means
	 * the manager itself died (shouldn't happen - it loops forever), so
	 * restart it. Stray orphans reparented to PID 1 just get reaped.
	 */
	for (;;) {
		pid_t pid = wait(NULL);

		if (pid < 0) {
			if (errno == ECHILD) {
				/* no managers left; idle instead of spinning */
				usleep(1000000);
				continue;
			}
			continue;
		}

		for (size_t i = 0; i < NR_CONSOLES; i++) {
			if (consoles[i].pid == pid) {
				start_manager(&consoles[i]);
				break;
			}
		}
	}

	return 0;
}
