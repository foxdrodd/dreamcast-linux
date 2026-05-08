#define _GNU_SOURCE
#include <sys/mount.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

// Maybe use a tiny custom init, instead of busybox to exec the userland shell
// build like: 
// sh4-linux-gnu-gcc -fno-asynchronous-unwind-tables -fno-ident -s -Os -nostdlib \
//   -static -include nolibc.h -o init init.c -lgcc -I ~/devel/linux/tools/include/nolibc
static void spawn_shell(const char *tty)
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        int fd;

        setsid();

        fd = open(tty, O_RDWR);
        if (fd < 0) {
            perror(tty);
            _exit(1);
        }

        ioctl(fd, TIOCSCTTY, 0);

        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);

        if (fd > STDERR_FILENO)
            close(fd);

        char *argv[] = { "/bin/mksh", "-l", NULL };
        char *envp[] = {
            "HOME=/root",
            "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
            "TERM=linux",
            NULL
        };

	execve("/bin/mksh", argv, envp);

        perror("execve /bin/mksh");
        _exit(1);
    }
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
	int ret = 0;
	ret = mount("proc",  "/proc", "proc",    0, "");
	if (ret) printf("proc failed with %d", ret);
	ret = mount("sysfs", "/sys",  "sysfs",   0, "");
	if (ret) printf("sysfs failed with %d", ret);
	ret = mount("devtmpfs", "/dev", "devtmpfs", 0, "mode=0755");
	if (ret) printf("devtmpfs failed with %d", ret);
	ret = mount("devpts",  "/dev/pts", "devpts",    0, "");
        if (ret) printf("devpts failed with %d", ret);

	ret = mount("/dev/gdrom", "/media", "iso9660", MS_RDONLY, NULL);
	if (ret) printf("gdrom failed with %d", ret);
	ret = mount("overlay", "/run/overlay-root", "overlay", 0, "rw,lowerdir=/media,upperdir=/run/overlay-rw/upper,workdir=/run/overlay-rw/work,redirect_dir=nofollow,uuid=null");
	if (ret) printf("overlay failed with %d", ret);

	ret = chdir("/run/overlay-root");
	if (ret) printf("chdir overlay-root failed with %d", ret);
	ret = bind_mount("/dev",  "/run/overlay-root/dev");
	if (ret) printf("bind dev failed with %d", ret);
	ret = bind_mount("/dev/pts",  "/run/overlay-root/dev/pts");
	if (ret) printf("bind pts failed with %d", ret);
	ret = bind_mount("/proc", "/run/overlay-root/proc");
	if (ret) printf("bind proc failed with %d", ret);
	ret = bind_mount("/sys",  "/run/overlay-root/sys");
	if (ret) printf("bind sys failed with %d", ret);

	if (access("/run/overlay-root/bin/mksh", X_OK) < 0)
        	perror("err /run/overlay-root/bin/mksh");

	if (access("/media/bin/mksh", X_OK) < 0)        
                perror("err /media/bin/mksh"); 


//	ret = mount("/run/overlay-root", "/", NULL, MS_MOVE, NULL);
//	if (ret) printf("mount move failed with %d", ret);
	ret = chroot("/run/overlay-root");
	if (ret) printf("chroot failed with %d", ret);
	chdir("/");

	if (access("/bin/mksh", X_OK) < 0)        
		perror("err /bin/mksh"); 

	printf("Hello hello hello hello");
	printf("Hello hello hello hello");
	printf("Hello hello hello hello");
	printf("Hello hello hello hello");
	spawn_shell("/dev/console");
	spawn_shell("/dev/tty0");

	for (;;) {
		int status;
		wait(&status);
	}

	return 0;
}
