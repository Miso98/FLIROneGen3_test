CC=gcc
CFLAGS=-g -O2 -Wall -D_REENTRANT `pkg-config --cflags gtk+-3.0 libusb-1.0 libjpeg libcjson`
LIBS=`pkg-config --libs gtk+-3.0 libusb-1.0 libjpeg libcjson` -lm

OBJ=flirgtk.o cam-thread.o cairo_jpg/src/cairo_jpg.o
PRG=flirgtk

all: $(PRG)

$(PRG): $(OBJ)
	$(CC) $(OBJ) -o $(PRG) $(LIBS)

clean:
	rm $(OBJ)
