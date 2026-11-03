CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
CFLAGS  += -I../common
PREFIX  ?= $(HOME)/.local
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

nova-lancer: src/main.c ../common/retro.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) -lm

run: nova-lancer
	./nova-lancer

install: nova-lancer
	install -Dm755 nova-lancer $(PREFIX)/bin/nova-lancer
	sed 's|@BIN@|$(PREFIX)/bin/nova-lancer|' nova-lancer.desktop.in > nova-lancer.desktop
	install -Dm644 nova-lancer.desktop $(PREFIX)/share/applications/nova-lancer.desktop
	rm -f nova-lancer.desktop

uninstall:
	rm -f $(PREFIX)/bin/nova-lancer $(PREFIX)/share/applications/nova-lancer.desktop

clean:
	rm -f nova-lancer

.PHONY: run install uninstall clean
