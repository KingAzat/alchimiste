# alchimiste-terminal — émulateur de terminal en C
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -D_XOPEN_SOURCE=700

SRC = main.c terminal.c relay.c
OBJ = $(SRC:.c=.o)
TARGET = alchimiste-terminal

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
