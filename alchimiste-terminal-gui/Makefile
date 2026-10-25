# alchimiste-terminal-gui — émulateur de terminal graphique X11
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -D_XOPEN_SOURCE=700
XFLAGS  = $(shell pkg-config --cflags --libs x11)

SRC     = main.c
OBJ     = $(SRC:.c=.o)
TARGET  = alchimiste-terminal-gui

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(XFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags x11) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
