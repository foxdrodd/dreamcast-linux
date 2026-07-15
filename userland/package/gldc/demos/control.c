/* GLdc-on-libpvr demo: an INTERACTIVE lit, texture-mapped object you drive with
 * the Dreamcast controller -- the Component-6 acceptance demo of the PVR2 3D plan
 * (a controllable, textured, lit, double-buffered 3D object at speed).
 *
 * Input comes from the Maple controller through the kernel evdev node
 * (/dev/input/eventN, "Dreamcast Controller"); we read raw struct input_event.
 *
 *   Analog stick   tilt / spin the object (deflection = angular velocity)
 *   D-pad          nudge rotation (digital)
 *   R trigger      zoom in        L trigger   zoom out
 *   A              toggle auto-spin
 *   B              switch shape (torus <-> cube)
 *   X              cycle texture (checker / stripes / grid)
 *   START          reset view
 *
 * Transform, lighting and input all run on the SH4; the PVR does the textured,
 * z-buffered, perspective-correct rasterisation. */
#include <sys/mount.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <math.h>
#include <linux/input.h>

#include <libpvr.h>
#include "GL/gl.h"
#include "GL/glu.h"
#include "GL/glext.h"
#include "GL/glkos.h"

/* sin+cos: sh4zam FSCA (one instruction) when built -DUSE_SH4ZAM, else libm.
 * The FSCA path needs -DSHZ_BACKEND=1 and NO -ffast-math (see gldc README). */
#ifdef USE_SH4ZAM
#include <sh4zam/shz_sh4zam.h>
static inline void SINCOS(float a, float *s, float *c)
{ shz_sincos_t v = shz_sincosf(a); *s = v.sin; *c = v.cos; }
#else
static inline void SINCOS(float a, float *s, float *c)
{ *s = sinf(a); *c = cosf(a); }
#endif

/* ------------------------------------------------------------------ input -- */

static int pad_fd = -1;
static struct {
    int ax, ay;          /* analog stick, 0..255, centre 128 */
    int gas, brake;      /* triggers, 0..255                 */
    int hatx, haty;      /* d-pad, -1/0/+1                   */
    int a, b, x, y, start;
    int a_prev, b_prev, x_prev, start_prev;
} pad;

/* Find the controller's evdev node by name (falls back to event2). */
static void pad_open(void)
{
    char path[64];
    for(int i = 0; i < 8; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if(fd < 0) continue;
        char name[128] = {0};
        if(ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
           strstr(name, "Controller")) {
            pad_fd = fd;
            printf("control: using %s (%s)\n", path, name);
            break;
        }
        close(fd);
    }
    if(pad_fd < 0)
        pad_fd = open("/dev/input/event2", O_RDONLY | O_NONBLOCK);
    pad.ax = pad.ay = 128;
}

static void pad_poll(void)
{
    if(pad_fd < 0) return;
    pad.a_prev = pad.a; pad.b_prev = pad.b;
    pad.x_prev = pad.x; pad.start_prev = pad.start;

    struct input_event ev;
    while(read(pad_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if(ev.type == EV_ABS) {
            switch(ev.code) {
                case ABS_X:      pad.ax    = ev.value; break;
                case ABS_Y:      pad.ay    = ev.value; break;
                case ABS_GAS:    pad.gas   = ev.value; break;
                case ABS_BRAKE:  pad.brake = ev.value; break;
                case ABS_HAT0X:  pad.hatx  = ev.value; break;
                case ABS_HAT0Y:  pad.haty  = ev.value; break;
            }
        } else if(ev.type == EV_KEY) {
            switch(ev.code) {
                case BTN_A:     pad.a     = ev.value; break;
                case BTN_B:     pad.b     = ev.value; break;
                case BTN_X:     pad.x     = ev.value; break;
                case BTN_Y:     pad.y     = ev.value; break;
                case BTN_START: pad.start = ev.value; break;
            }
        }
    }
}
static int pressed_a(void)     { return pad.a && !pad.a_prev; }
static int pressed_b(void)     { return pad.b && !pad.b_prev; }
static int pressed_x(void)     { return pad.x && !pad.x_prev; }
static int pressed_start(void) { return pad.start && !pad.start_prev; }

/* ---------------------------------------------------------------- texture -- */

#define TEX 64
static GLuint texid;
static uint8_t texdata[TEX * TEX * 3];

static void build_texture(int style)
{
    for(int y = 0; y < TEX; y++)
        for(int x = 0; x < TEX; x++) {
            int r, g, b;
            switch(style) {
                default:
                case 0: { /* blue/amber checker + grid */
                    int c = ((x >> 3) ^ (y >> 3)) & 1;
                    if(c) { r = 40; g = 120; b = 220; } else { r = 230; g = 170; b = 40; }
                    if(((x & 7) == 0) || ((y & 7) == 0)) r = g = b = 245;
                    break; }
                case 1: { /* diagonal candy stripes */
                    int s = ((x + y) >> 2) & 3;
                    static const int pal[4][3] = {{230,60,60},{240,220,60},{60,200,90},{60,120,230}};
                    r = pal[s][0]; g = pal[s][1]; b = pal[s][2];
                    break; }
                case 2: { /* neon grid on dark */
                    int gx = (x & 7) < 1, gy = (y & 7) < 1;
                    if(gx || gy) { r = 40; g = 250; b = 200; } else { r = 12; g = 16; b = 32; }
                    break; }
            }
            uint8_t *p = &texdata[(y * TEX + x) * 3];
            p[0] = r; p[1] = g; p[2] = b;
        }
    glBindTexture(GL_TEXTURE_2D, texid);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEX, TEX, GL_RGB, GL_UNSIGNED_BYTE, texdata);
}

/* ------------------------------------------------------------ maths / draw -- */

#define RINGS 40
#define SIDES 20
#define RMAJ  1.0f
#define RMIN  0.42f

static const float L[3] = { 0.40f, 0.55f, 0.73f };  /* world light dir */

static void build_rot(float ax, float ay, float m[9])
{
    float cx, sx, cy, sy;
    SINCOS(ax, &sx, &cx);
    SINCOS(ay, &sy, &cy);
    m[0] = cy;      m[1] = 0.0f; m[2] = sy;
    m[3] = sx * sy; m[4] = cx;   m[5] = -sx * cy;
    m[6] = -cx * sy;m[7] = sx;   m[8] = cx * cy;
}
static inline void emit(const float m[9], float px, float py, float pz,
                        float nx, float ny, float nz, float u, float v)
{
    float wx = m[0]*px + m[1]*py + m[2]*pz;
    float wy = m[3]*px + m[4]*py + m[5]*pz;
    float wz = m[6]*px + m[7]*py + m[8]*pz;
    float wnx = m[0]*nx + m[1]*ny + m[2]*nz;
    float wny = m[3]*nx + m[4]*ny + m[5]*nz;
    float wnz = m[6]*nx + m[7]*ny + m[8]*nz;
    float d = wnx*L[0] + wny*L[1] + wnz*L[2];
    if(d < 0.0f) d = 0.0f;
    float s = 0.25f + 0.75f * d;
    glColor3f(s, s, s);
    glTexCoord2f(u, v);
    glVertex3f(wx, wy, wz);
}

static void draw_torus(const float m[9])
{
    glBegin(GL_QUADS);
    for(int i = 0; i < RINGS; i++)
        for(int j = 0; j < SIDES; j++) {
            const int ii[4] = { i, i+1, i+1, i };
            const int jj[4] = { j, j, j+1, j+1 };
            for(int k = 0; k < 4; k++) {
                float th = ii[k] * (2.0f * (float)M_PI / RINGS);
                float ph = jj[k] * (2.0f * (float)M_PI / SIDES);
                float ct, st, cp, sp;
                SINCOS(th, &st, &ct);
                SINCOS(ph, &sp, &cp);
                emit(m, (RMAJ + RMIN*cp)*ct, (RMAJ + RMIN*cp)*st, RMIN*sp,
                        cp*ct, cp*st, sp,
                        ii[k] * (4.0f / RINGS), jj[k] * (2.0f / SIDES));
            }
        }
    glEnd();
}

static void draw_cube(const float m[9])
{
    /* 6 faces: normal + 4 corners (CCW), unit cube scaled to 1.1 */
    static const float N[6][3] = {
        { 0, 0, 1},{ 0, 0,-1},{ 0, 1, 0},{ 0,-1, 0},{ 1, 0, 0},{-1, 0, 0} };
    static const float V[6][4][3] = {
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},   /* +Z */
        {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}},   /* -Z */
        {{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1}},   /* +Y */
        {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}},   /* -Y */
        {{ 1,-1, 1},{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1}},   /* +X */
        {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}} }; /* -X */
    static const float UV[4][2] = {{0,0},{1,0},{1,1},{0,1}};
    const float sc = 1.05f;
    glBegin(GL_QUADS);
    for(int f = 0; f < 6; f++)
        for(int k = 0; k < 4; k++)
            emit(m, V[f][k][0]*sc, V[f][k][1]*sc, V[f][k][2]*sc,
                    N[f][0], N[f][1], N[f][2], UV[k][0], UV[k][1]);
    glEnd();
}

/* ------------------------------------------------------------------- state -- */

static float rot_x, rot_y;     /* current orientation           */
static float vel_x, vel_y;     /* auto-spin velocity            */
static float dist = -4.5f;     /* camera distance (negative z)  */
static int   autospin = 1;
static int   shape = 0;        /* 0 = torus, 1 = cube           */
static int   tex_style = 0;

static void reset_view(void)
{
    rot_x = rot_y = 0.0f;
    vel_x = 0.0f; vel_y = 0.020f;
    dist = -4.5f;
    autospin = 1;
}

static void update(void)
{
    pad_poll();

    if(pressed_start()) reset_view();
    if(pressed_a())     autospin = !autospin;
    if(pressed_b())     shape ^= 1;
    if(pressed_x())   { tex_style = (tex_style + 1) % 3; build_texture(tex_style); }

    /* analog stick -> angular velocity (deadzone around centre) */
    float sx = (pad.ax - 128) / 128.0f;
    float sy = (pad.ay - 128) / 128.0f;
    if(fabsf(sx) < 0.16f) sx = 0.0f;
    if(fabsf(sy) < 0.16f) sy = 0.0f;
    rot_y += sx * 0.08f + pad.hatx * 0.04f;
    rot_x += sy * 0.08f + pad.haty * 0.04f;

    if(autospin) { rot_x += vel_x; rot_y += vel_y; }

    /* triggers -> zoom (R closer, L farther) */
    dist += (pad.gas - pad.brake) / 255.0f * 0.14f;
    if(dist < -13.0f) dist = -13.0f;
    if(dist >  -2.3f) dist =  -2.3f;
}

static void draw(void)
{
    float m[9];
    build_rot(rot_x, rot_y, m);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, dist);
    glBindTexture(GL_TEXTURE_2D, texid);

    if(shape == 0) draw_torus(m);
    else           draw_cube(m);

    glKosSwapBuffers();
}

int main(int argc, char **argv)
{
    /* optional startup state: argv[1] = "cube"|"torus", argv[2] = tex 0..2 */
    if(argc > 1 && strcmp(argv[1], "cube") == 0)  shape = 1;
    if(argc > 2) { tex_style = atoi(argv[2]) % 3; if(tex_style < 0) tex_style = 0; }

    mkdir("/dev", 0755);
    mount("dev", "/dev", "devtmpfs", 0, 0);

    glKosInit();

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.05f, 0.05f, 0.08f, 0.0f);
    glShadeModel(GL_SMOOTH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, 640.0f / 480.0f, 0.1f, 100.0f);
    glMatrixMode(GL_MODELVIEW);

    glGenTextures(1, &texid);
    glBindTexture(GL_TEXTURE_2D, texid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEX, TEX, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    build_texture(tex_style);

    pad_open();
    reset_view();

    for(;;) {
        update();
        draw();
    }
    return 0;
}
