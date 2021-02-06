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

clean:
	rm -f $(PRG) $(OBJ)

deb:
	dpkg-buildpackage -rfakeroot -b -uc -us -ui -i -i
