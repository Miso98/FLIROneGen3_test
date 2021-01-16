CC=gcc
CFLAGS=-O2 -Wall -D_REENTRANT `pkg-config --cflags gtk+-3.0` `pkg-config --cflags libusb-1.0`
LIBS=`pkg-config --libs gtk+-3.0` `pkg-config --libs libusb-1.0` -lm

OBJ=flirgtk.o cam-thread.o
PRG=flirgtk

all: $(PRG)

$(PRG): $(OBJ)
	$(CC) $(OBJ) -o $(PRG) $(LIBS)

clean:
	rm $(OBJ)
