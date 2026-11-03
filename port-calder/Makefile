CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
PREFIX  ?= $(HOME)/.local
DEPS_CFLAGS := $(shell pkg-config --cflags sdl2 gl)
DEPS_LIBS   := $(shell pkg-config --libs sdl2 gl)
SRC := src/main.c src/gfx.c src/world.c src/actors.c src/game.c

port-calder: $(SRC) src/audio.c src/audio.h
	$(CC) $(CFLAGS) $(DEPS_CFLAGS) -o $@ src/main.c src/audio.c $(DEPS_LIBS) -lm

run: port-calder
	./port-calder

install: port-calder
	install -Dm755 port-calder $(PREFIX)/bin/port-calder
	sed 's|@BIN@|$(PREFIX)/bin/port-calder|' port-calder.desktop.in > port-calder.desktop
	install -Dm644 port-calder.desktop $(PREFIX)/share/applications/port-calder.desktop
	rm -f port-calder.desktop

uninstall:
	rm -f $(PREFIX)/bin/port-calder $(PREFIX)/share/applications/port-calder.desktop

clean:
	rm -f port-calder

.PHONY: run install uninstall clean
