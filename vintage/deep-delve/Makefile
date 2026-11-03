CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
CFLAGS  += -I../common
PREFIX  ?= $(HOME)/.local
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

deep-delve: src/main.c ../common/retro.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) -lm

run: deep-delve
	./deep-delve

install: deep-delve
	install -Dm755 deep-delve $(PREFIX)/bin/deep-delve
	sed 's|@BIN@|$(PREFIX)/bin/deep-delve|' deep-delve.desktop.in > deep-delve.desktop
	install -Dm644 deep-delve.desktop $(PREFIX)/share/applications/deep-delve.desktop
	rm -f deep-delve.desktop

uninstall:
	rm -f $(PREFIX)/bin/deep-delve $(PREFIX)/share/applications/deep-delve.desktop

clean:
	rm -f deep-delve

.PHONY: run install uninstall clean
