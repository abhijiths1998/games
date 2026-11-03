CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
CFLAGS  += -I../common
PREFIX  ?= $(HOME)/.local
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

ember-isle: src/main.c ../common/retro.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) -lm

run: ember-isle
	./ember-isle

install: ember-isle
	install -Dm755 ember-isle $(PREFIX)/bin/ember-isle
	sed 's|@BIN@|$(PREFIX)/bin/ember-isle|' ember-isle.desktop.in > ember-isle.desktop
	install -Dm644 ember-isle.desktop $(PREFIX)/share/applications/ember-isle.desktop
	rm -f ember-isle.desktop

uninstall:
	rm -f $(PREFIX)/bin/ember-isle $(PREFIX)/share/applications/ember-isle.desktop

clean:
	rm -f ember-isle

.PHONY: run install uninstall clean
