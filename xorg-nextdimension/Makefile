# The NeXTdimension's X driver, built on the NeXT or in a Debian m68k
# chroot with xserver-xorg-dev (scripts/build-xdriver.sh).

CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -Wno-unused-parameter -fPIC -fvisibility=hidden \
	$(shell pkg-config --cflags xorg-server)
moduledir = $(shell pkg-config --variable=moduledir xorg-server)

nextdimension_drv.so: nextdimension_drv.c
	$(CC) $(CFLAGS) -shared -o $@ $<

install: nextdimension_drv.so
	install -D -m 644 nextdimension_drv.so $(DESTDIR)$(moduledir)/drivers/nextdimension_drv.so

clean:
	rm -f nextdimension_drv.so

.PHONY: install clean
