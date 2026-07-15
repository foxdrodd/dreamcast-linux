/* GLdc-on-libpvr demo: NeHe lesson 5 (spinning colour pyramid + cube),
 * the classic proof that fixed-function GL 1.x runs on the Dreamcast's PVR2
 * through our /dev/pvr + libpvr stack.  Based on GLdc samples/nehe05. */
#include <sys/mount.h>
#include <sys/stat.h>

#include <stdio.h>
#include <libpvr.h>

#include "GL/gl.h"
#include "GL/glu.h"
#include "GL/glkos.h"

GLfloat rtri;
GLfloat rquad;

void InitGL(int Width, int Height)
{
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, (GLfloat)Width / (GLfloat)Height, 0.1f, 100.0f);
    glMatrixMode(GL_MODELVIEW);
}

void DrawGLScene()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(-1.5f, 0.0f, -6.0f);
    glRotatef(rtri, 0.0f, 1.0f, 0.0f);

    glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f); glVertex3f( 0.0f, 1.0f, 0.0f);
        glColor3f(0.0f, 1.0f, 0.0f); glVertex3f(-1.0f,-1.0f, 1.0f);
        glColor3f(0.0f, 0.0f, 1.0f); glVertex3f( 1.0f,-1.0f, 1.0f);

        glColor3f(1.0f, 0.0f, 0.0f); glVertex3f( 0.0f, 1.0f, 0.0f);
        glColor3f(0.0f, 0.0f, 1.0f); glVertex3f( 1.0f,-1.0f, 1.0f);
        glColor3f(0.0f, 1.0f, 0.0f); glVertex3f( 1.0f,-1.0f,-1.0f);

        glColor3f(1.0f, 0.0f, 0.0f); glVertex3f( 0.0f, 1.0f, 0.0f);
        glColor3f(0.0f, 1.0f, 0.0f); glVertex3f( 1.0f,-1.0f,-1.0f);
        glColor3f(0.0f, 0.0f, 1.0f); glVertex3f(-1.0f,-1.0f,-1.0f);

        glColor3f(1.0f, 0.0f, 0.0f); glVertex3f( 0.0f, 1.0f, 0.0f);
        glColor3f(0.0f, 0.0f, 1.0f); glVertex3f(-1.0f,-1.0f,-1.0f);
        glColor3f(0.0f, 1.0f, 0.0f); glVertex3f(-1.0f,-1.0f, 1.0f);
    glEnd();

    glLoadIdentity();
    glTranslatef(1.5f, 0.0f, -7.0f);
    glRotatef(rquad, 1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
        glColor3f(0.0f, 1.0f, 0.0f);
        glVertex3f( 1.0f, 1.0f,-1.0f); glVertex3f(-1.0f, 1.0f,-1.0f);
        glVertex3f(-1.0f, 1.0f, 1.0f); glVertex3f( 1.0f, 1.0f, 1.0f);

        glColor3f(1.0f, 0.5f, 0.0f);
        glVertex3f( 1.0f,-1.0f, 1.0f); glVertex3f(-1.0f,-1.0f, 1.0f);
        glVertex3f(-1.0f,-1.0f,-1.0f); glVertex3f( 1.0f,-1.0f,-1.0f);

        glColor3f(1.0f, 0.0f, 0.0f);
        glVertex3f( 1.0f, 1.0f, 1.0f); glVertex3f(-1.0f, 1.0f, 1.0f);
        glVertex3f(-1.0f,-1.0f, 1.0f); glVertex3f( 1.0f,-1.0f, 1.0f);

        glColor3f(1.0f, 1.0f, 0.0f);
        glVertex3f( 1.0f,-1.0f,-1.0f); glVertex3f(-1.0f,-1.0f,-1.0f);
        glVertex3f(-1.0f, 1.0f,-1.0f); glVertex3f( 1.0f, 1.0f,-1.0f);

        glColor3f(0.0f, 0.0f, 1.0f);
        glVertex3f(-1.0f, 1.0f, 1.0f); glVertex3f(-1.0f, 1.0f,-1.0f);
        glVertex3f(-1.0f,-1.0f,-1.0f); glVertex3f(-1.0f,-1.0f, 1.0f);

        glColor3f(1.0f, 0.0f, 1.0f);
        glVertex3f( 1.0f, 1.0f,-1.0f); glVertex3f( 1.0f, 1.0f, 1.0f);
        glVertex3f( 1.0f,-1.0f, 1.0f); glVertex3f( 1.0f,-1.0f,-1.0f);
    glEnd();

    rtri  += 0.2f;
    rquad -= 0.15f;

    glKosSwapBuffers();
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    mkdir("/dev", 0755);
    mount("dev", "/dev", "devtmpfs", 0, 0);

    glKosInit();
    InitGL(640, 480);
    printf("screen %dx%d\n", pvr_screen_w, pvr_screen_h);

    for(int frame = 0; ; frame++) {
        DrawGLScene();
        if(frame < 5)
            printf("frame %d: TA_VERTBUF_POS=%08x\n", frame, pvr_debug_reg(0x138));
    }

    return 0;
}
