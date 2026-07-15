/* GLdc-on-libpvr demo: a lit, texture-mapped spinning torus (donut).
 *
 * Shows the full fixed-function 3D path now that textures render correctly:
 * a procedurally-generated checker texture wrapped over a torus, Gouraud-shaded
 * by a world-fixed directional light.  The transform & lighting run on the SH4
 * (positions and normals are rotated in C so the light stays fixed in the world
 * while the donut spins); the PVR does the textured, perspective-correct,
 * z-buffered rasterisation. */
#include <sys/mount.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include <libpvr.h>
#include "GL/gl.h"
#include "GL/glu.h"
#include "GL/glext.h"
#include "GL/glkos.h"

#define RINGS 40          /* segments around the main ring */
#define SIDES 20          /* segments around the tube */
#define RMAJ  1.0f
#define RMIN  0.42f
#define TEX   64

static GLuint texid;
static uint8_t texdata[TEX * TEX * 3];

/* Procedural checker texture: two tinted tiles with a bright grid. */
static void make_texture(void)
{
    for(int y = 0; y < TEX; y++)
        for(int x = 0; x < TEX; x++) {
            int checker = ((x >> 3) ^ (y >> 3)) & 1;
            int r, g, b;
            if(checker) { r = 40;  g = 120; b = 220; }   /* blue tile   */
            else        { r = 230; g = 170; b = 40;  }   /* amber tile  */
            if(((x & 7) == 0) || ((y & 7) == 0)) { r = g = b = 245; } /* grid */
            uint8_t *p = &texdata[(y * TEX + x) * 3];
            p[0] = r; p[1] = g; p[2] = b;
        }
}

static void InitGL(int w, int h)
{
    make_texture();
    glGenTextures(1, &texid);
    glBindTexture(GL_TEXTURE_2D, texid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    /* Allocate then upload via glTexSubImage2D -- the upload path proven clean
     * through the 64-bit VRAM area (see README, _glVramCopy fix). */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEX, TEX, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEX, TEX, GL_RGB, GL_UNSIGNED_BYTE, texdata);

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.05f, 0.05f, 0.08f, 0.0f);
    glShadeModel(GL_SMOOTH);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, (GLfloat)w / (GLfloat)h, 0.1f, 100.0f);
    glMatrixMode(GL_MODELVIEW);
}

/* 3x3 rotation R = Rx(ax) * Ry(ay), row-major. */
static void build_rot(float ax, float ay, float m[9])
{
    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay);
    m[0] = cy;       m[1] = 0.0f; m[2] = sy;
    m[3] = sx * sy;  m[4] = cx;   m[5] = -sx * cy;
    m[6] = -cx * sy; m[7] = sx;   m[8] = cx * cy;
}
static inline void rot3(const float m[9], float x, float y, float z, float *o)
{
    o[0] = m[0]*x + m[1]*y + m[2]*z;
    o[1] = m[3]*x + m[4]*y + m[5]*z;
    o[2] = m[6]*x + m[7]*y + m[8]*z;
}

static float ang = 0.0f;

static void DrawGLScene(void)
{
    static const float L[3] = { 0.40f, 0.55f, 0.73f };   /* world light dir */
    float m[9];
    build_rot(ang * 0.7f, ang, m);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -4.0f);
    glBindTexture(GL_TEXTURE_2D, texid);

    glBegin(GL_QUADS);
    for(int i = 0; i < RINGS; i++) {
        for(int j = 0; j < SIDES; j++) {
            /* four corners of this quad in (ring, side) index space */
            const int ii[4] = { i,   i+1, i+1, i   };
            const int jj[4] = { j,   j,   j+1, j+1 };
            for(int k = 0; k < 4; k++) {
                float th = ii[k] * (2.0f * (float)M_PI / RINGS);
                float ph = jj[k] * (2.0f * (float)M_PI / SIDES);
                float ct = cosf(th), st = sinf(th), cp = cosf(ph), sp = sinf(ph);

                float px = (RMAJ + RMIN * cp) * ct;
                float py = (RMAJ + RMIN * cp) * st;
                float pz =  RMIN * sp;
                float nx = cp * ct, ny = cp * st, nz = sp;

                float wp[3], wn[3];
                rot3(m, px, py, pz, wp);
                rot3(m, nx, ny, nz, wn);

                float d = wn[0]*L[0] + wn[1]*L[1] + wn[2]*L[2];
                if(d < 0.0f) d = 0.0f;
                float s = 0.25f + 0.75f * d;               /* ambient + diffuse */

                glColor3f(s, s, s);
                glTexCoord2f(ii[k] * (4.0f / RINGS), jj[k] * (2.0f / SIDES));
                glVertex3f(wp[0], wp[1], wp[2]);
            }
        }
    }
    glEnd();

    ang += 0.02f;
    glKosSwapBuffers();
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    mkdir("/dev", 0755);
    mount("dev", "/dev", "devtmpfs", 0, 0);

    glKosInit();
    InitGL(640, 480);

    for(;;)
        DrawGLScene();
    return 0;
}
