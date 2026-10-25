# alchimiste-shell — Makefile
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L

SRC_DIR = src
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=build/%.o)
TARGET  = alchimiste-shell

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

build/%.o: $(SRC_DIR)/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

# Suite complète : compilation + tests unitaires + fonctionnels + interactifs.
test: all
	bash tests/run_all.sh

# Version de débogage : AddressSanitizer + UndefinedBehaviorSanitizer.
debug: CFLAGS += -g -O0 -fsanitize=address,undefined
debug: clean all

clean:
	rm -rf build $(TARGET) tests/test_parser

.PHONY: all run test debug clean
