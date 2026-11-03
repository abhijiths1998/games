CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
PREFIX  ?= $(HOME)/.local
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

lumen-drift: src/main.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) -lm

run: lumen-drift
	./lumen-drift

install: lumen-drift
	install -Dm755 lumen-drift $(PREFIX)/bin/lumen-drift
	sed 's|@BIN@|$(PREFIX)/bin/lumen-drift|' lumen-drift.desktop.in > lumen-drift.desktop
	install -Dm644 lumen-drift.desktop $(PREFIX)/share/applications/lumen-drift.desktop
	rm -f lumen-drift.desktop

uninstall:
	rm -f $(PREFIX)/bin/lumen-drift $(PREFIX)/share/applications/lumen-drift.desktop

clean:
	rm -f lumen-drift

.PHONY: run install uninstall clean
