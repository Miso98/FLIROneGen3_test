CC=gcc
CFLAGS=-O2 -Wall `pkg-config --cflags gtk+-3.0`
LIBS=`pkg-config --libs gtk+-3.0`

OBJ=flirgtk.o
PRG=flirgtk

all: $(PRG)

$(PRG): $(OBJ)
	$(CC) $(OBJ) -o $(PRG) $(LIBS)

clean:
	rm $(OBJ)
