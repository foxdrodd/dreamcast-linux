// SPDX-License-Identifier: GPL-2.0
/*
 * maple_test - userspace Maple bus input tester for Dreamcast.
 *
 * Two-pane live TUI (raw ANSI, no ncurses/terminfo, single static binary):
 *   - top pane:  ASCII art of the *active* device (controller or mouse),
 *                buttons/dpad light up, sticks/triggers/crosshair move.
 *   - bottom pane: scrolling log of decoded events.
 * Auto-scans /dev/input/event*, keeps Maple devices, supports *multiple*
 * controllers on the bus - each keeps its own state; the art follows
 * whichever device you last touched.
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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define MAX_DEV		16
#define MAXR		48	/* frame buffer height cap */
#define MAXC		160	/* frame buffer width cap  */
#define LOG_LINES	64	/* event log ring capacity */
#define LOG_W		200

/* ---- device model ------------------------------------------------------ */

enum dtype { DT_OTHER, DT_PAD, DT_MOUSE };

/* local button bit indices for the pad */
enum { PB_A, PB_B, PB_C, PB_X, PB_Y, PB_Z, PB_START, PB_SELECT, PB_MAX };
/* local button bit indices for the mouse */
enum { MB_LEFT, MB_MIDDLE, MB_RIGHT, MB_MAX };

#define MBW 20		/* mouse crosshair box inner width  */
#define MBH 5		/* mouse crosshair box inner height */

struct pad_state {
	unsigned buttons;		/* bitmap of PB_*    */
	int hat_x, hat_y;		/* -1 / 0 / +1       */
	int x, y, rx, ry;		/* 0..255, center 128*/
	int gas, brake;			/* 0..255 triggers   */
};

struct mouse_state {
	unsigned buttons;		/* bitmap of MB_*    */
	int px, py;			/* crosshair pos in box */
	int wheel;			/* accumulated wheel */
};

struct dev {
	int fd;
	char name[256];
	char label[16];			/* "pad0", "mouse1", ... */
	enum dtype type;
	struct pad_state pad;
	struct mouse_state mouse;
};

static struct dev devs[MAX_DEV];
static int ndev;
static int g_active = -1;		/* index of last device touched */

/* ---- event log ring ---------------------------------------------------- */

static char logbuf[LOG_LINES][LOG_W];
static int log_head, log_count;
static long long t0_ms;

static long long mono_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void log_add(const char *label, const char *text)
{
	long long t = mono_ms() - t0_ms;
	int mm = (int)(t / 60000);
	int ss = (int)(t / 1000 % 60);
	int ms = (int)(t % 1000);

	snprintf(logbuf[log_head], LOG_W, "%02d:%02d.%03d  %-6s %s",
		 mm, ss, ms, label, text);
	log_head = (log_head + 1) % LOG_LINES;
	if (log_count < LOG_LINES)
		log_count++;
}

/* ---- frame buffer (char grid + attribute grid) ------------------------- */

static char fb_ch[MAXR][MAXC];
static unsigned char fb_at[MAXR][MAXC];		/* 0 = normal, 1 = reverse */
static int g_rows = 24, g_cols = 80;

static void fb_clear(void)
{
	int r;

	for (r = 0; r < g_rows; r++) {
		memset(fb_ch[r], ' ', g_cols);
		memset(fb_at[r], 0, g_cols);
	}
}

static void fb_putc(int r, int c, char ch, int at)
{
	if (r < 0 || r >= g_rows || c < 0 || c >= g_cols)
		return;
	fb_ch[r][c] = ch;
	fb_at[r][c] = at;
}

/* draw string, clipped to the interior [1 .. g_cols-2] to spare borders */
static void fb_puts(int r, int c, const char *s, int at)
{
	for (; *s; s++, c++) {
		if (c < 1)
			continue;
		if (c > g_cols - 2)
			break;
		fb_putc(r, c, *s, at);
	}
}

static void fb_frame(void)
{
	int r, c;

	for (c = 0; c < g_cols; c++) {
		fb_putc(0, c, '-', 0);
		fb_putc(g_rows - 1, c, '-', 0);
	}
	for (r = 1; r < g_rows - 1; r++) {
		fb_putc(r, 0, '|', 0);
		fb_putc(r, g_cols - 1, '|', 0);
	}
	fb_putc(0, 0, '+', 0);
	fb_putc(0, g_cols - 1, '+', 0);
	fb_putc(g_rows - 1, 0, '+', 0);
	fb_putc(g_rows - 1, g_cols - 1, '+', 0);
}

static void fb_hsep(int r)
{
	int c;

	for (c = 1; c < g_cols - 1; c++)
		fb_putc(r, c, '-', 0);
	fb_putc(r, 0, '+', 0);
	fb_putc(r, g_cols - 1, '+', 0);
}

/* serialize whole frame to one write() - home cursor, overwrite, no clear */
static void fb_flush(void)
{
	static char out[MAXR * (MAXC * 6 + 16) + 16];
	int r, c, p = 0;

	p += sprintf(out + p, "\033[H");
	for (r = 0; r < g_rows; r++) {
		int cur = 0;

		p += sprintf(out + p, "\033[%d;1H", r + 1);
		for (c = 0; c < g_cols; c++) {
			int a = fb_at[r][c];

			if (a != cur) {
				p += sprintf(out + p, a ? "\033[7m" : "\033[0m");
				cur = a;
			}
			out[p++] = fb_ch[r][c];
		}
		if (cur)
			p += sprintf(out + p, "\033[0m");
	}
	(void)!write(1, out, p);	/* nothing to do on failed console write */
}

/* ---- terminal setup ---------------------------------------------------- */

static volatile sig_atomic_t g_quit;

static void on_sig(int s)
{
	(void)s;
	g_quit = 1;
}

static void term_init(void)
{
	struct winsize ws;

	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row && ws.ws_col) {
		g_rows = ws.ws_row;
		g_cols = ws.ws_col;
	}
	if (g_rows > MAXR)
		g_rows = MAXR;
	if (g_cols > MAXC)
		g_cols = MAXC;
	if (g_rows < 12)
		g_rows = 12;
	if (g_cols < 40)
		g_cols = 40;

	/* hide cursor, disable line wrap, clear screen */
	(void)!write(1, "\033[?25l\033[?7l\033[2J", 14);
}

static void term_restore(void)
{
	/* show cursor, re-enable wrap, reset attrs, move below the frame */
	char buf[64];
	int p = snprintf(buf, sizeof(buf), "\033[0m\033[?25h\033[?7h\033[%d;1H\n",
			 g_rows);
	(void)!write(1, buf, p);
}

/* ---- art: controller --------------------------------------------------- */

static void draw_bar(int r, int c, const char *lbl, int val)
{
	int w = 8, fill = val * w / 255, i;
	char num[16];

	if (fill < 0)
		fill = 0;
	if (fill > w)
		fill = w;
	fb_puts(r, c, lbl, 0);
	fb_putc(r, c + 2, '[', 0);
	for (i = 0; i < w; i++)
		fb_putc(r, c + 3 + i, i < fill ? '#' : '-', 0);
	fb_putc(r, c + 3 + w, ']', 0);
	snprintf(num, sizeof(num), "%3d", val);
	fb_puts(r, c + 3 + w + 2, num, 0);
}

static void draw_pad(int top, int left, const struct pad_state *p)
{
	int fc = left + 22;		/* face-button cluster origin */
#define B(bit) (!!(p->buttons & (1u << (bit))))
	char stk[12];
	int sx, i;

	fb_puts(top + 0, left, "D-PAD", 0);
	fb_puts(top + 1, left + 4, "[U]", p->hat_y < 0);
	fb_puts(top + 2, left + 1, "[L]", p->hat_x < 0);
	fb_puts(top + 2, left + 7, "[R]", p->hat_x > 0);
	fb_puts(top + 3, left + 4, "[D]", p->hat_y > 0);

	fb_puts(top + 0, fc, "BUTTONS", 0);
	fb_puts(top + 1, fc + 0, "(Z)", B(PB_Z));
	fb_puts(top + 1, fc + 8, "(C)", B(PB_C));
	fb_puts(top + 2, fc + 4, "(Y)", B(PB_Y));
	fb_puts(top + 3, fc + 0, "(X)", B(PB_X));
	fb_puts(top + 3, fc + 8, "(B)", B(PB_B));
	fb_puts(top + 4, fc + 4, "(A)", B(PB_A));

	fb_puts(top + 5, left + 1, "[ START ]", B(PB_START));
	fb_puts(top + 5, left + 12, "[ SELECT ]", B(PB_SELECT));

	draw_bar(top + 7, left + 1, "L", p->brake);
	draw_bar(top + 8, left + 1, "R", p->gas);

	/* analog stick: horizontal marker + numeric X/Y (and 2nd stick) */
	sx = p->x * 7 / 255;
	if (sx < 0)
		sx = 0;
	if (sx > 6)
		sx = 6;
	stk[0] = '[';
	for (i = 0; i < 7; i++)
		stk[1 + i] = (i == sx) ? 'o' : ' ';
	stk[8] = ']';
	stk[9] = 0;
	fb_puts(top + 7, fc, "stick", 0);
	fb_puts(top + 7, fc + 7, stk, 0);
	{
		char num[40];
		snprintf(num, sizeof(num), "X=%3d Y=%3d  R:X=%3d Y=%3d",
			 p->x, p->y, p->rx, p->ry);
		fb_puts(top + 8, fc, num, 0);
	}
#undef B
}

/* ---- art: mouse -------------------------------------------------------- */

static void draw_mouse(int top, int left, const struct mouse_state *m)
{
	int r, c;
#define MB(bit) (!!(m->buttons & (1u << (bit))))

	fb_puts(top, left, "MOUSE  (relative motion)", 0);
	/* box border */
	for (c = 0; c <= MBW + 1; c++) {
		fb_putc(top + 1, left + c, '-', 0);
		fb_putc(top + 1 + MBH + 1, left + c, '-', 0);
	}
	for (r = 1; r <= MBH; r++) {
		fb_putc(top + 1 + r, left, '|', 0);
		fb_putc(top + 1 + r, left + MBW + 1, '|', 0);
	}
	fb_putc(top + 1, left, '+', 0);
	fb_putc(top + 1, left + MBW + 1, '+', 0);
	fb_putc(top + 1 + MBH + 1, left, '+', 0);
	fb_putc(top + 1 + MBH + 1, left + MBW + 1, '+', 0);
	/* crosshair */
	fb_putc(top + 2 + m->py, left + 1 + m->px, 'o', 0);
	/* buttons + wheel */
	fb_puts(top + MBH + 3, left, "[L]", MB(MB_LEFT));
	fb_puts(top + MBH + 3, left + 5, "[M]", MB(MB_MIDDLE));
	fb_puts(top + MBH + 3, left + 10, "[R]", MB(MB_RIGHT));
	{
		char num[24];
		snprintf(num, sizeof(num), "wheel=%+d", m->wheel);
		fb_puts(top + MBH + 3, left + 16, num, 0);
	}
#undef MB
}

/* ---- full frame render ------------------------------------------------- */

static void render(void)
{
	int i, art_top, sep2, log_top, log_h, shown, k, idx;
	char hdr[MAXC];

	fb_clear();
	fb_frame();

	/* header */
	if (g_active >= 0)
		snprintf(hdr, sizeof(hdr), " maple_test   active: %s (%s)",
			 devs[g_active].label, devs[g_active].name);
	else
		snprintf(hdr, sizeof(hdr),
			 " maple_test   waiting for input...");
	fb_puts(1, 1, hdr, 0);

	/* device roster: mark active with '*' */
	{
		char ros[MAXC];
		int p = 0;

		p += snprintf(ros + p, sizeof(ros) - p, " devices:");
		for (i = 0; i < ndev && p < (int)sizeof(ros) - 20; i++)
			p += snprintf(ros + p, sizeof(ros) - p, " %c%s%c",
				      i == g_active ? '*' : '[',
				      devs[i].label,
				      i == g_active ? '*' : ']');
		fb_puts(2, 1, ros, 0);
	}

	fb_hsep(3);

	/* geometry: art pane rows 4..sep2-1, log pane sep2+1..g_rows-2 */
	log_h = g_rows / 3;
	if (log_h < 5)
		log_h = 5;
	if (log_h > 15)
		log_h = 15;
	sep2 = g_rows - 2 - log_h;
	art_top = 4;
	log_top = sep2 + 1;
	if (sep2 <= art_top)		/* tiny terminal: shrink log */
		sep2 = art_top + 1, log_top = sep2 + 1;
	fb_hsep(sep2);

	/* art pane: draw the active device's art */
	if (g_active >= 0 && devs[g_active].type == DT_PAD)
		draw_pad(art_top + 1, 3, &devs[g_active].pad);
	else if (g_active >= 0 && devs[g_active].type == DT_MOUSE)
		draw_mouse(art_top + 1, 3, &devs[g_active].mouse);
	else
		fb_puts(art_top + 1, 3,
			"press a button / move the stick or mouse...", 0);

	/* log pane: last (log_h) lines, oldest first, bottom-aligned */
	shown = log_count < log_h ? log_count : log_h;
	for (k = 0; k < shown; k++) {
		idx = (log_head - shown + k + LOG_LINES) % LOG_LINES;
		fb_puts(log_top + k, 2, logbuf[idx], 0);
	}

	fb_flush();
}

/* ---- event decode + state update --------------------------------------- */

static int pad_btn_bit(int code)
{
	switch (code) {
	case BTN_A:	return PB_A;
	case BTN_B:	return PB_B;
	case BTN_C:	return PB_C;
	case BTN_X:	return PB_X;
	case BTN_Y:	return PB_Y;
	case BTN_Z:	return PB_Z;
	case BTN_START:	return PB_START;
	case BTN_SELECT:return PB_SELECT;
	default:	return -1;
	}
}

static const char *pad_btn_name(int bit)
{
	static const char *n[PB_MAX] = {
		"A", "B", "C", "X", "Y", "Z", "START", "SELECT"
	};
	return (bit >= 0 && bit < PB_MAX) ? n[bit] : "?";
}

/* returns 1 if a log line was produced */
static int apply(struct dev *d, struct input_event *e)
{
	char text[80];

	switch (e->type) {
	case EV_KEY:
		if (d->type == DT_MOUSE) {
			int bit = e->code == BTN_LEFT ? MB_LEFT :
				  e->code == BTN_RIGHT ? MB_RIGHT :
				  e->code == BTN_MIDDLE ? MB_MIDDLE : -1;
			const char *nm = bit == MB_LEFT ? "LEFT" :
					 bit == MB_RIGHT ? "RIGHT" :
					 bit == MB_MIDDLE ? "MIDDLE" : "?";
			if (bit < 0)
				return 0;
			if (e->value)
				d->mouse.buttons |= 1u << bit;
			else
				d->mouse.buttons &= ~(1u << bit);
			snprintf(text, sizeof(text), "mouse %s %s", nm,
				 e->value ? "pressed" : "released");
		} else {
			int bit = pad_btn_bit(e->code);

			if (bit < 0) {
				snprintf(text, sizeof(text), "key 0x%x %s",
					 e->code, e->value ? "pressed" : "released");
			} else {
				if (e->value)
					d->pad.buttons |= 1u << bit;
				else
					d->pad.buttons &= ~(1u << bit);
				snprintf(text, sizeof(text), "%s %s",
					 pad_btn_name(bit),
					 e->value ? "pressed" : "released");
			}
		}
		break;
	case EV_ABS:
		switch (e->code) {
		case ABS_HAT0X:
			d->pad.hat_x = e->value;
			snprintf(text, sizeof(text), "D-pad %s", e->value < 0 ?
				 "LEFT" : e->value > 0 ? "RIGHT" : "X-center");
			break;
		case ABS_HAT0Y:
			d->pad.hat_y = e->value;
			snprintf(text, sizeof(text), "D-pad %s", e->value < 0 ?
				 "UP" : e->value > 0 ? "DOWN" : "Y-center");
			break;
		case ABS_X:	d->pad.x = e->value;
			snprintf(text, sizeof(text), "stick X=%d", e->value); break;
		case ABS_Y:	d->pad.y = e->value;
			snprintf(text, sizeof(text), "stick Y=%d", e->value); break;
		case ABS_RX:	d->pad.rx = e->value;
			snprintf(text, sizeof(text), "stick2 X=%d", e->value); break;
		case ABS_RY:	d->pad.ry = e->value;
			snprintf(text, sizeof(text), "stick2 Y=%d", e->value); break;
		case ABS_GAS:	d->pad.gas = e->value;
			snprintf(text, sizeof(text), "R-trigger=%d", e->value); break;
		case ABS_BRAKE:	d->pad.brake = e->value;
			snprintf(text, sizeof(text), "L-trigger=%d", e->value); break;
		default:
			snprintf(text, sizeof(text), "abs 0x%x=%d",
				 e->code, e->value);
			break;
		}
		break;
	case EV_REL:
		switch (e->code) {
		case REL_X:
			d->mouse.px += e->value;
			if (d->mouse.px < 0)		d->mouse.px = 0;
			if (d->mouse.px > MBW - 1)	d->mouse.px = MBW - 1;
			snprintf(text, sizeof(text), "mouse dx=%d", e->value);
			break;
		case REL_Y:
			d->mouse.py += e->value;
			if (d->mouse.py < 0)		d->mouse.py = 0;
			if (d->mouse.py > MBH - 1)	d->mouse.py = MBH - 1;
			snprintf(text, sizeof(text), "mouse dy=%d", e->value);
			break;
		case REL_WHEEL:
			d->mouse.wheel += e->value;
			snprintf(text, sizeof(text), "mouse wheel=%d", e->value);
			break;
		default:
			snprintf(text, sizeof(text), "rel 0x%x=%d",
				 e->code, e->value);
			break;
		}
		break;
	default:
		return 0;	/* skip EV_SYN, EV_MSC noise */
	}

	log_add(d->label, text);
	g_active = (int)(d - devs);
	return 1;
}

/* ---- device discovery -------------------------------------------------- */

#define BITS_PER_LONG	(8 * (int)sizeof(long))
#define TEST_BIT(nr, a)	(((a)[(nr) / BITS_PER_LONG] >> ((nr) % BITS_PER_LONG)) & 1)

static enum dtype detect_type(int fd)
{
	unsigned long evbit[(EV_MAX + BITS_PER_LONG) / BITS_PER_LONG] = { 0 };

	if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit) < 0)
		return DT_OTHER;
	if (TEST_BIT(EV_REL, evbit))
		return DT_MOUSE;
	if (TEST_BIT(EV_ABS, evbit))
		return DT_PAD;
	return DT_OTHER;
}

static void assign_label(struct dev *d)
{
	static int npad, nmouse, nother;
	const char *base = d->type == DT_PAD ? "pad" :
			   d->type == DT_MOUSE ? "mouse" : "dev";
	int *cnt = d->type == DT_PAD ? &npad :
		   d->type == DT_MOUSE ? &nmouse : &nother;

	snprintf(d->label, sizeof(d->label), "%s%d", base, (*cnt)++);
}

static int ci_contains(const char *hay, const char *needle)
{
	size_t nl = strlen(needle);

	if (!nl)
		return 1;
	for (; *hay; hay++)
		if (strncasecmp(hay, needle, nl) == 0)
			return 1;
	return 0;
}

static int is_maple(const char *name)
{
	return ci_contains(name, "dreamcast") || ci_contains(name, "maple");
}

/* ---- main -------------------------------------------------------------- */

int main(int argc, char **argv)
{
	struct pollfd pfd[MAX_DEV];
	struct sigaction sa;
	int filter = 1, i;
	long long last_render = 0;
	int dirty = 0;
	DIR *dir;
	struct dirent *de;

	if (argc > 1 && !strcmp(argv[1], "--all"))
		filter = 0;

	dir = opendir("/dev/input");
	if (!dir) {
		perror("opendir /dev/input");
		return 1;
	}
	while ((de = readdir(dir)) && ndev < MAX_DEV) {
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
		devs[ndev].fd = fd;
		snprintf(devs[ndev].name, sizeof(devs[ndev].name), "%s", name);
		devs[ndev].type = detect_type(fd);
		devs[ndev].mouse.px = MBW / 2;
		devs[ndev].mouse.py = MBH / 2;
		assign_label(&devs[ndev]);
		pfd[ndev].fd = fd;
		pfd[ndev].events = POLLIN;
		ndev++;
	}
	closedir(dir);

	if (!ndev) {
		fprintf(stderr, "no %sinput devices found%s\n",
			filter ? "maple " : "",
			filter ? " (try --all)" : "");
		return 1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_sig;			/* no SA_RESTART: poll() sees EINTR */
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	t0_ms = mono_ms();
	term_init();
	render();

	while (!g_quit) {
		long long now = mono_ms();
		int timeout = -1;
		int r;

		if (dirty) {
			int wait = 33 - (int)(now - last_render);
			timeout = wait < 0 ? 0 : wait;	/* coalesce to ~30 Hz */
		}

		r = poll(pfd, ndev, timeout);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		for (i = 0; i < ndev; i++) {
			struct input_event ev[32];
			ssize_t rd;
			size_t j;

			if (!(pfd[i].revents & POLLIN))
				continue;
			rd = read(devs[i].fd, ev, sizeof(ev));
			if (rd < (ssize_t)sizeof(ev[0]))
				continue;
			for (j = 0; j < rd / sizeof(ev[0]); j++)
				if (apply(&devs[i], &ev[j]))
					dirty = 1;
		}

		now = mono_ms();
		if (dirty && now - last_render >= 33) {
			render();
			last_render = now;
			dirty = 0;
		}
	}

	term_restore();
	return 0;
}
