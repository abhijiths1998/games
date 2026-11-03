CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
PREFIX  ?= $(HOME)/.local
DEPS_CFLAGS := $(shell pkg-config --cflags sdl2 gl)
DEPS_LIBS   := $(shell pkg-config --libs sdl2 gl)

hardlight: src/main.c
	$(CC) $(CFLAGS) $(DEPS_CFLAGS) -o $@ $< $(DEPS_LIBS) -lm

run: hardlight
	./hardlight

install: hardlight
	install -Dm755 hardlight $(PREFIX)/bin/hardlight
	sed 's|@BIN@|$(PREFIX)/bin/hardlight|' hardlight.desktop.in > hardlight.desktop
	install -Dm644 hardlight.desktop $(PREFIX)/share/applications/hardlight.desktop
	rm -f hardlight.desktop

uninstall:
	rm -f $(PREFIX)/bin/hardlight $(PREFIX)/share/applications/hardlight.desktop

clean:
	rm -f hardlight

.PHONY: run install uninstall clean
