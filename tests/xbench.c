// SPDX-License-Identifier: GPL-2.0-only
/*
 * Time some of what X does most on the NeXT: fills, scrolling a window's
 * contents (a copy within it), images, core and Xft text, and moving a
 * window over the screen.  Each test ends by reading a pixel back, which
 * waits for X and for whatever draws for it (the NeXTdimension's i860).
 * Prints milliseconds per operation.
 *
 * Built by scripts/build-xdriver.sh (it needs Xlib and Xft).
 *
 * usage: DISPLAY=:0 xbench [SCALE]   (SCALE multiplies the counts, default 1)
 * XBENCH_HOLD=SECONDS keeps the windows up at the end, to look at them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#define W 800
#define H 600

static Display *dpy;
static Window win;
static GC gc;

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* wait until X has drawn everything */
static void finish(Window w)
{
	XImage *img;

	XSync(dpy, False);
	img = XGetImage(dpy, w, 0, 0, 1, 1, AllPlanes, ZPixmap);
	if (img)
		XDestroyImage(img);
}

static void report(const char *name, int n, double t)
{
	printf("%-28s %6d ops %8.2f ms/op\n", name, n, t * 1000 / n);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	int scale = argc > 1 ? atoi(argv[1]) : 1;
	XSetWindowAttributes swa = { .override_redirect = True,
				     .background_pixel = 0 };
	const char *text = "The quick brown fox jumps over the lazy dog 0123";
	XftFont *xft;
	XftDraw *draw;
	XftColor color;
	XImage *img;
	XEvent ev;
	Window mover;
	double t;
	int i, n;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "no display\n");
		return 1;
	}
	win = XCreateWindow(dpy, DefaultRootWindow(dpy), 50, 50, W, H, 0,
			    CopyFromParent, InputOutput, CopyFromParent,
			    CWOverrideRedirect | CWBackPixel, &swa);
	XSelectInput(dpy, win, ExposureMask);
	XMapRaised(dpy, win);
	do
		XNextEvent(dpy, &ev);
	while (ev.type != Expose);
	gc = XCreateGC(dpy, win, 0, NULL);
	finish(win);

	n = 400 * scale;
	t = now();
	for (i = 0; i < n; i++) {
		XSetForeground(dpy, gc, (i * 0x10305) & 0xffffff);
		XFillRectangle(dpy, win, gc, (i * 7) % 400, (i * 5) % 300,
			       400, 300);
	}
	finish(win);
	report("fill 400x300", n, now() - t);

	n = 200 * scale;
	t = now();
	for (i = 0; i < n; i++)
		XCopyArea(dpy, win, win, gc, 0, 16, W, H - 16, 0, 0);
	finish(win);
	report("scroll 800x584 by 16", n, now() - t);

	img = XCreateImage(dpy, DefaultVisual(dpy, 0), 24, ZPixmap, 0,
			   malloc(256 * 256 * 4), 256, 256, 32, 0);
	for (i = 0; i < 256 * 256; i++)
		((unsigned int *)img->data)[i] = (unsigned int)i * 0x10101;
	n = 50 * scale;
	t = now();
	for (i = 0; i < n; i++)
		XPutImage(dpy, win, gc, img, 0, 0, (i * 13) % 500,
			  (i * 11) % 300, 256, 256);
	finish(win);
	report("putimage 256x256", n, now() - t);

	XSetForeground(dpy, gc, 0xffffff);
	XSetBackground(dpy, gc, 0x000080);
	n = 1000 * scale;
	t = now();
	for (i = 0; i < n; i++)
		XDrawImageString(dpy, win, gc, 10, 20 + (i % 40) * 14, text,
				 strlen(text));
	finish(win);
	report("core text, 48 chars", n, now() - t);

	xft = XftFontOpenName(dpy, 0, "monospace:size=10");
	draw = XftDrawCreate(dpy, win, DefaultVisual(dpy, 0),
			     DefaultColormap(dpy, 0));
	XftColorAllocName(dpy, DefaultVisual(dpy, 0), DefaultColormap(dpy, 0),
			  "white", &color);
	n = 300 * scale;
	t = now();
	for (i = 0; i < n; i++)
		XftDrawString8(draw, &color, xft, 10, 20 + (i % 40) * 14,
			       (const FcChar8 *)text, strlen(text));
	finish(win);
	report("Xft text, 48 chars", n, now() - t);

	swa.background_pixel = 0x808000;
	mover = XCreateWindow(dpy, DefaultRootWindow(dpy), 100, 100, 400, 300,
			      0, CopyFromParent, InputOutput, CopyFromParent,
			      CWOverrideRedirect | CWBackPixel, &swa);
	XMapRaised(dpy, mover);
	finish(mover);
	n = 100 * scale;
	t = now();
	for (i = 0; i < n; i++)
		XMoveWindow(dpy, mover, 100 + (i % 50) * 8, 100 + (i % 50) * 4);
	finish(mover);
	report("move window 400x300", n, now() - t);

	if (getenv("XBENCH_HOLD")) {
		XUnmapWindow(dpy, mover);
		XClearWindow(dpy, win);
		XDrawImageString(dpy, win, gc, 10, 20, text, strlen(text));
		for (i = 0; i < 8; i++) {
			XftColor c;
			XRenderColor rc = { (i & 1) * 0xffff, (i & 2) * 0x7fff,
					    (i & 4) * 0x3fff, 0xffff };

			XftColorAllocValue(dpy, DefaultVisual(dpy, 0),
					   DefaultColormap(dpy, 0), &rc, &c);
			XftDrawString8(draw, &c, xft, 10, 50 + i * 16,
				       (const FcChar8 *)text, strlen(text));
		}
		XSync(dpy, False);
		sleep(atoi(getenv("XBENCH_HOLD")));
	}
	XCloseDisplay(dpy);
	return 0;
}
