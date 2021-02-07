prefix=/usr/local

CC=gcc
CFLAGS=-g -O2 -Wall -D_REENTRANT `pkg-config --cflags gtk+-3.0 libusb-1.0 libjpeg libcjson`
LIBS=`pkg-config --libs gtk+-3.0 libusb-1.0 libjpeg libcjson` -lm

OBJ=flirgtk.o cam-thread.o cairo_jpg/src/cairo_jpg.o
PRG=flirgtk

all: $(PRG)

$(PRG): $(OBJ) cam-thread.h planck.h
	$(CC) $(OBJ) -o $(PRG) $(LIBS)

install:
	install -D $(PRG) $(DESTDIR)$(prefix)/bin/$(PRG)
	install -D flirgtk.desktop $(DESTDIR)$(prefix)/share/applications/flirgtk.desktop
	install -D flirgtk.png $(DESTDIR)$(prefix)/share/icons/hicolor/256x256/apps/flirgtk.png
	install -D 77-flirone-lusb.rules $(DESTDIR)/lib/udev/rules.d/77-flirone-lusb.rules

clean:
	rm -f $(PRG) $(OBJ)
	rm -rf debian/.debhelper
	rm -f debian/files
	rm -rf debian/flirgtk
	rm -f debian/debhelper-build-stamp
	rm -f debian/flirgtk.debhelper.log
	rm -f debian/flirgtk.substvars

deb:
	dpkg-buildpackage -rfakeroot -b -uc -us -ui -i
