CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
PREFIX  ?= $(HOME)/.local
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

orbit-golf: src/main.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) -lm

build: orbit-golf

run: orbit-golf
	./orbit-golf

install: orbit-golf
	install -Dm755 orbit-golf $(PREFIX)/bin/orbit-golf
	sed 's|@BIN@|$(PREFIX)/bin/orbit-golf|' orbit-golf.desktop.in > orbit-golf.desktop
	install -Dm644 orbit-golf.desktop $(PREFIX)/share/applications/orbit-golf.desktop
	rm -f orbit-golf.desktop

uninstall:
	rm -f $(PREFIX)/bin/orbit-golf $(PREFIX)/share/applications/orbit-golf.desktop

clean:
	rm -f orbit-golf

.PHONY: build run install uninstall clean
