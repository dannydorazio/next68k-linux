/*
 * Show a test image through the DRM API, as a userspace program would:
 * a dumb buffer, a framebuffer and a mode set on the first connector.
 *
 *   drmtest [xrgb|rgb565|c2|c4] [SECONDS]
 *
 * xrgb (the default): XRGB8888, a grey ramp on top, then bands of red,
 * green, blue and white ramps.  rgb565: the same in RGB565, in the CPU's
 * byte order through the legacy ADDFB ioctl, as X does at depth 16.
 * c2 (monochrome) and c4 (Color): the native indexed formats, vertical
 * bars of every colour, with a palette loaded through the legacy gamma
 * ioctl that runs white to black, the reverse of the default.
 *
 * Build: m68k-linux-gnu-gcc -static -O2 -o drmtest drmtest.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

static int fd;

static void xioctl(unsigned long req, void *arg, const char *what)
{
	if (ioctl(fd, req, arg)) {
		perror(what);
		exit(1);
	}
}

int main(int argc, char **argv)
{
	const char *what = argc > 1 ? argv[1] : "xrgb";
	int rgb565 = !strcmp(what, "rgb565");
	int cbits = !strcmp(what, "c2") ? 2 : !strcmp(what, "c4") ? 4 : 0;
	int ncolors = 1 << cbits;
	int secs = argc > 2 ? atoi(argv[2]) : 10;
	struct drm_mode_card_res res = { 0 };
	struct drm_mode_get_connector conn = { 0 };
	struct drm_mode_modeinfo mode;
	struct drm_mode_create_dumb dumb = { 0 };
	struct drm_mode_fb_cmd2 fb = { 0 };
	struct drm_mode_fb_cmd fb1 = { 0 };
	struct drm_mode_map_dumb map = { 0 };
	struct drm_mode_crtc crtc = { 0 };
	uint32_t crtc_id, conn_id;
	uint8_t *p;
	unsigned int x, y;

	/* "showing" has to appear while the image is up, even in a file */
	setvbuf(stdout, NULL, _IOLBF, 0);

	fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		perror("/dev/dri/card0");
		return 1;
	}
	xioctl(DRM_IOCTL_SET_MASTER, NULL, "SET_MASTER");

	xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res, "GETRESOURCES");
	res.crtc_id_ptr = (uintptr_t)&crtc_id;
	res.connector_id_ptr = (uintptr_t)&conn_id;
	res.count_crtcs = res.count_connectors = 1;
	res.count_fbs = res.count_encoders = 0;
	xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res, "GETRESOURCES");

	conn.connector_id = conn_id;
	xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &conn, "GETCONNECTOR");
	if (!conn.count_modes) {
		fprintf(stderr, "no modes\n");
		return 1;
	}
	conn.modes_ptr = (uintptr_t)&mode;
	conn.count_modes = 1;
	conn.count_props = conn.count_encoders = 0;
	xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &conn, "GETCONNECTOR");
	printf("connector %u, crtc %u, mode %s\n", conn_id, crtc_id, mode.name);

	dumb.width = mode.hdisplay;
	dumb.height = mode.vdisplay;
	dumb.bpp = cbits ? cbits : rgb565 ? 16 : 32;
	xioctl(DRM_IOCTL_MODE_CREATE_DUMB, &dumb, "CREATE_DUMB");

	if (rgb565) {
		fb1.width = dumb.width;
		fb1.height = dumb.height;
		fb1.pitch = dumb.pitch;
		fb1.bpp = fb1.depth = 16;
		fb1.handle = dumb.handle;
		xioctl(DRM_IOCTL_MODE_ADDFB, &fb1, "ADDFB");
		fb.fb_id = fb1.fb_id;
	} else {
		fb.width = dumb.width;
		fb.height = dumb.height;
		fb.pixel_format = cbits == 2 ? DRM_FORMAT_C2 :
				  cbits == 4 ? DRM_FORMAT_C4 :
				  DRM_FORMAT_XRGB8888;
		fb.handles[0] = dumb.handle;
		fb.pitches[0] = dumb.pitch;
		xioctl(DRM_IOCTL_MODE_ADDFB2, &fb, "ADDFB2");
	}

	map.handle = dumb.handle;
	xioctl(DRM_IOCTL_MODE_MAP_DUMB, &map, "MAP_DUMB");
	p = mmap(NULL, dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		 map.offset);
	if (p == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	for (y = 0; y < dumb.height; y++) {
		uint8_t *line = p + y * dumb.pitch;

		for (x = 0; x < dumb.width; x++) {
			unsigned int v = x * 256 / dumb.width;
			unsigned int band = y * 5 / dumb.height;
			uint32_t px;

			if (cbits) {
				unsigned int ppb = 8 / cbits;	/* per byte */
				unsigned int shift = 8 - cbits * (x % ppb + 1);

				v = x * ncolors / dumb.width;
				line[x / ppb] = (line[x / ppb] &
						 ~((ncolors - 1) << shift)) |
						v << shift;
				continue;
			}
			switch (band) {
			case 0:	px = v << 16 | v << 8 | v; break;
			case 1:	px = v << 16; break;
			case 2:	px = v << 8; break;
			case 3:	px = v; break;
			default: px = 0xffffff; break;
			}
			if (rgb565) {
				/* the CPU's byte order */
				((uint16_t *)line)[x] = (px >> 8 & 0xf800) |
							(px >> 5 & 0x07e0) |
							(px >> 3 & 0x001f);
				continue;
			}
			/* little-endian, whatever the CPU */
			line[4 * x] = px;
			line[4 * x + 1] = px >> 8;
			line[4 * x + 2] = px >> 16;
			line[4 * x + 3] = 0;
		}
	}

	crtc.crtc_id = crtc_id;
	crtc.fb_id = fb.fb_id;
	crtc.set_connectors_ptr = (uintptr_t)&conn_id;
	crtc.count_connectors = 1;
	crtc.mode = mode;
	crtc.mode_valid = 1;
	xioctl(DRM_IOCTL_MODE_SETCRTC, &crtc, "SETCRTC");

	if (cbits) {
		uint16_t lut[3][16];
		struct drm_mode_crtc_lut gamma = {
			.crtc_id = crtc_id,
			.gamma_size = ncolors,
			.red = (uintptr_t)lut[0],
			.green = (uintptr_t)lut[1],
			.blue = (uintptr_t)lut[2],
		};
		int i;

		for (i = 0; i < ncolors; i++)
			lut[0][i] = lut[1][i] = lut[2][i] =
				0xffff - i * (0xffff / (ncolors - 1));
		xioctl(DRM_IOCTL_MODE_SETGAMMA, &gamma, "SETGAMMA");
	}

	printf("showing %s for %d seconds\n", what, secs);
	sleep(secs);
	return 0;
}
