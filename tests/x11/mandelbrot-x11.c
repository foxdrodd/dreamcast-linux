/*
 * mandelbrot-x11.c - a small, portable Xlib Mandelbrot viewer.
 *
 * Written to build against a minimal musl / uClibc userland with libX11 only
 * (no XShm, no toolkit, no extra deps). Tuned for the Dreamcast: small default
 * window, modest iteration cap, and a `real` typedef so you can drop to float
 * for the SH4's single-precision FPU (much faster than double there).
 *
 * Build (cross, adjust CC/sysroot to taste):
 *   $CC -O2 -o mandelbrot-x11 mandelbrot-x11.c -lX11 -lm
 *   # SH4 tip: -DUSE_FLOAT builds the math in float (fast on the DC's FPU):
 *   $CC -O2 -DUSE_FLOAT -o mandelbrot-x11 mandelbrot-x11.c -lX11 -lm
 *
 * Controls:
 *   space        toggle auto-zoom animation (dives into fractal detail)
 *   left click   zoom in 2x, centered on the cursor
 *   right click  zoom out 2x
 *   arrows       pan
 *   + / -        more / fewer iterations (detail vs. speed)
 *   r            reset view
 *   q / Esc      quit
 *
 * Handles TrueColor/DirectColor visuals by packing RGB from the visual's
 * masks, and PseudoColor (8-bit fbdev) by allocating a shared colour ramp.
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifdef USE_FLOAT
typedef float real;
#else
typedef double real;
#endif

/* Dreamcast-friendly defaults: small frame, so a full render stays snappy. */
#define DEF_W 320
#define DEF_H 240
#define DEF_ITER 128
#define MAX_ITER 1024     /* cap so deep auto-zooms stay responsive on the SH4 */

/* A classic detail-rich point (seahorse valley) to auto-zoom into. */
#define FAM_CX (-0.743643887037158704752191506114774)
#define FAM_CY ( 0.131825904205311970493132056385139)

/* --- colour handling --------------------------------------------------- */

/* For TrueColor/DirectColor we pack straight into the visual's masks. */
static int   truecolor;
static unsigned long r_mask, g_mask, b_mask;
static int   r_shift, g_shift, b_shift, r_bits, g_bits, b_bits;

/* For PseudoColor we pre-allocate a ramp and index into it. */
static unsigned long ramp[256];
static int ramp_n;

static int mask_shift(unsigned long m) { int s = 0; if (!m) return 0; while (!(m & 1)) { m >>= 1; s++; } return s; }
static int mask_bits (unsigned long m) { int n = 0; while (m) { n += m & 1; m >>= 1; } return n; }

/* Map an escape count to an 8-bit RGB triple (a simple, cheap palette). */
static void iter_to_rgb(int it, int maxit, unsigned char *r, unsigned char *g, unsigned char *b)
{
    if (it >= maxit) { *r = *g = *b = 0; return; }      /* inside the set: black */
    /* Smooth-ish bands: cycle hue with iteration count. */
    double t = (double)it / (double)maxit;
    *r = (unsigned char)(9  * (1 - t) * t * t * t * 255);
    *g = (unsigned char)(15 * (1 - t) * (1 - t) * t * t * 255);
    *b = (unsigned char)(8.5 * (1 - t) * (1 - t) * (1 - t) * t * 255);
}

static unsigned long pack_truecolor(unsigned char r, unsigned char g, unsigned char b)
{
    unsigned long px = 0;
    px |= ((unsigned long)(r >> (8 - r_bits)) << r_shift) & r_mask;
    px |= ((unsigned long)(g >> (8 - g_bits)) << g_shift) & g_mask;
    px |= ((unsigned long)(b >> (8 - b_bits)) << b_shift) & b_mask;
    return px;
}

static unsigned long color_for(int it, int maxit)
{
    unsigned char r, g, b;
    iter_to_rgb(it, maxit, &r, &g, &b);
    if (truecolor) return pack_truecolor(r, g, b);
    /* PseudoColor: map luminance onto the allocated ramp. */
    int lum = (r * 30 + g * 59 + b * 11) / 100;   /* 0..255 */
    return ramp[(lum * (ramp_n - 1)) / 255];
}

static void alloc_ramp(Display *dpy, Colormap cmap)
{
    int i;
    ramp_n = 64;                                  /* modest, fits 8-bit maps */
    for (i = 0; i < ramp_n; i++) {
        XColor c;
        int v = i * 255 / (ramp_n - 1);
        /* Warm ramp black->orange->white, arbitrary but readable. */
        c.red   = (v < 128 ? v * 2 : 255) * 257;
        c.green = (v < 128 ? 0 : (v - 128) * 2) * 257;
        c.blue  = (v < 200 ? 0 : (v - 200) * 4) * 257;
        c.flags = DoRed | DoGreen | DoBlue;
        if (!XAllocColor(dpy, cmap, &c)) c.pixel = 0;
        ramp[i] = c.pixel;
    }
}

/* --- render ------------------------------------------------------------ */

static void render(XImage *img, int w, int h,
                   real cx, real cy, real scale, int maxit)
{
    /* scale = width of the view in the complex plane, per pixel. */
    real ox = cx - scale * w / 2;
    real oy = cy - scale * h / 2;
    int px, py;
    for (py = 0; py < h; py++) {
        real y0 = oy + scale * py;
        for (px = 0; px < w; px++) {
            real x0 = ox + scale * px;
            real x = 0, y = 0;
            int it = 0;
            /* z = z^2 + c, escape when |z|^2 > 4. */
            while (x * x + y * y <= (real)4 && it < maxit) {
                real xt = x * x - y * y + x0;
                y = (real)2 * x * y + y0;
                x = xt;
                it++;
            }
            XPutPixel(img, px, py, color_for(it, maxit));
        }
    }
}

int main(int argc, char **argv)
{
    int w = DEF_W, h = DEF_H, maxit = DEF_ITER;
    if (argc >= 3) { w = atoi(argv[1]); h = atoi(argv[2]); }
    if (argc >= 4) maxit = atoi(argv[3]);
    if (w < 16) w = 16;
    if (h < 16) h = 16;

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "cannot open display (is X/DISPLAY set?)\n"); return 1; }

    int scr = DefaultScreen(dpy);
    Visual *vis = DefaultVisual(dpy, scr);
    int depth = DefaultDepth(dpy, scr);
    Colormap cmap = DefaultColormap(dpy, scr);

    if (vis->class == TrueColor || vis->class == DirectColor) {
        truecolor = 1;
        r_mask = vis->red_mask;   g_mask = vis->green_mask;  b_mask = vis->blue_mask;
        r_shift = mask_shift(r_mask); g_shift = mask_shift(g_mask); b_shift = mask_shift(b_mask);
        r_bits  = mask_bits(r_mask);  g_bits  = mask_bits(g_mask);  b_bits  = mask_bits(b_mask);
    } else {
        truecolor = 0;
        alloc_ramp(dpy, cmap);
    }

    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, w, h, 0,
                                     BlackPixel(dpy, scr), BlackPixel(dpy, scr));
    XStoreName(dpy, win, "Mandelbrot");
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    GC gc = XCreateGC(dpy, win, 0, NULL);
    XMapWindow(dpy, win);

    /* Allocate the backing image; XPutPixel handles depth/byte-order for us. */
    char *data = malloc((size_t)w * h * 4);
    XImage *img = XCreateImage(dpy, vis, depth, ZPixmap, 0, data, w, h, 32, 0);
    if (!img) { fprintf(stderr, "XCreateImage failed\n"); return 1; }

    /* View state, in the complex plane. */
    real cx = -0.5, cy = 0.0;
    real span0 = 3.0;                   /* initial horizontal extent */
    real span = span0;
    real scale = span / w;
    int dirty = 1;

    /* Auto-zoom animation: ping-pong in and out of a detail-rich point so the
     * fractal keeps unfolding.  We stop zooming in at the precision floor of
     * the chosen number type, then zoom back out and repeat. */
    int  animate = 0;
    real zoom = 0.97;                   /* <1 zoom in, >1 zoom out, per frame */
#ifdef USE_FLOAT
    const real min_span = 1e-4;         /* float runs out of bits shallow */
#else
    const real min_span = 1e-13;        /* double dives much deeper */
#endif
    struct timespec nap = { 0, 30 * 1000 * 1000 };  /* ~30 ms cap between frames */

    for (;;) {
        if (dirty) {
            render(img, w, h, cx, cy, scale, maxit);
            XPutImage(dpy, win, gc, img, 0, 0, 0, 0, w, h);
            dirty = 0;
        }

        /* While animating with no input queued, advance the zoom instead of
         * blocking; otherwise fall through to a normal blocking wait. */
        if (animate && !XPending(dpy)) {
            span *= zoom;
            if (span <= min_span) zoom = 1.0 / 0.97;         /* bottomed out: rise */
            if (span >= span0) { span = span0; zoom = 0.97; } /* back up top: dive */
            scale = span / w;
            /* More iterations as we dive, so fresh detail keeps resolving. */
            {
                double depth = log((double)(span0 / span)) / log(2.0);
                maxit = DEF_ITER + (int)(depth * 32);
                if (maxit > MAX_ITER) maxit = MAX_ITER;
            }
            dirty = 1;
            nanosleep(&nap, NULL);       /* keep fast hosts from spinning */
            continue;
        }

        XEvent ev;
        XNextEvent(dpy, &ev);
        switch (ev.type) {
        case Expose:
            XPutImage(dpy, win, gc, img, 0, 0, 0, 0, w, h);
            break;

        case ConfigureNotify: {
            int nw = ev.xconfigure.width, nh = ev.xconfigure.height;
            if (nw > 0 && nh > 0 && (nw != w || nh != h)) {
                w = nw; h = nh;
                XDestroyImage(img);                 /* frees data too */
                data = malloc((size_t)w * h * 4);
                img = XCreateImage(dpy, vis, depth, ZPixmap, 0, data, w, h, 32, 0);
                scale = span / w;
                dirty = 1;
            }
            break;
        }

        case ButtonPress: {
            /* Recenter on the cursor, then zoom. */
            real mx = (cx - scale * w / 2) + scale * ev.xbutton.x;
            real my = (cy - scale * h / 2) + scale * ev.xbutton.y;
            cx = mx; cy = my;
            if (ev.xbutton.button == Button1)      { span *= 0.5; }   /* zoom in  */
            else if (ev.xbutton.button == Button3) { span *= 2.0; }   /* zoom out */
            scale = span / w;
            dirty = 1;
            break;
        }

        case KeyPress: {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            real pan = span * 0.1;
            switch (ks) {
            case XK_Left:  cx -= pan; dirty = 1; break;
            case XK_Right: cx += pan; dirty = 1; break;
            case XK_Up:    cy -= pan; dirty = 1; break;
            case XK_Down:  cy += pan; dirty = 1; break;
            case XK_plus:
            case XK_equal:      maxit += 64; dirty = 1; break;
            case XK_minus:      if (maxit > 64) maxit -= 64; dirty = 1; break;
            case XK_space:
                animate = !animate;
                if (animate) {
                    /* If we're still at the dull default view, seed the
                     * detail-rich point; otherwise dive wherever you are. */
                    if (cx == (real)-0.5 && cy == (real)0.0) { cx = FAM_CX; cy = FAM_CY; }
                    zoom = 0.97;
                }
                break;
            case XK_r:          cx = -0.5; cy = 0.0; span = span0; scale = span / w; maxit = DEF_ITER; animate = 0; dirty = 1; break;
            case XK_q:
            case XK_Escape:     goto done;
            }
            break;
        }
        }
    }

done:
    XDestroyImage(img);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
