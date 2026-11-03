CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
PREFIX  ?= $(HOME)/.local
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

bramble-bound: src/main.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) -lm

run: bramble-bound
	./bramble-bound

install: bramble-bound
	install -Dm755 bramble-bound $(PREFIX)/bin/bramble-bound
	sed 's|@BIN@|$(PREFIX)/bin/bramble-bound|' bramble-bound.desktop.in > bramble-bound.desktop
	install -Dm644 bramble-bound.desktop $(PREFIX)/share/applications/bramble-bound.desktop
	rm -f bramble-bound.desktop

uninstall:
	rm -f $(PREFIX)/bin/bramble-bound $(PREFIX)/share/applications/bramble-bound.desktop

clean:
	rm -f bramble-bound

.PHONY: run install uninstall clean
