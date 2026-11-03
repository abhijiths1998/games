CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
CFLAGS  += -I../common
PREFIX  ?= $(HOME)/.local
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

relic-runner: src/main.c ../common/retro.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) -lm

run: relic-runner
	./relic-runner

install: relic-runner
	install -Dm755 relic-runner $(PREFIX)/bin/relic-runner
	sed 's|@BIN@|$(PREFIX)/bin/relic-runner|' relic-runner.desktop.in > relic-runner.desktop
	install -Dm644 relic-runner.desktop $(PREFIX)/share/applications/relic-runner.desktop
	rm -f relic-runner.desktop

uninstall:
	rm -f $(PREFIX)/bin/relic-runner $(PREFIX)/share/applications/relic-runner.desktop

clean:
	rm -f relic-runner

.PHONY: run install uninstall clean
