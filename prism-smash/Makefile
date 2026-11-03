CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
PREFIX  ?= $(HOME)/.local
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

prism-smash: src/main.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) -lm

run: prism-smash
	./prism-smash

install: prism-smash
	install -Dm755 prism-smash $(PREFIX)/bin/prism-smash
	sed 's|@BIN@|$(PREFIX)/bin/prism-smash|' prism-smash.desktop.in > prism-smash.desktop
	install -Dm644 prism-smash.desktop $(PREFIX)/share/applications/prism-smash.desktop
	rm -f prism-smash.desktop

uninstall:
	rm -f $(PREFIX)/bin/prism-smash $(PREFIX)/share/applications/prism-smash.desktop

clean:
	rm -f prism-smash

.PHONY: run install uninstall clean
