# Maki - a pager that lives inside the terminal. Cross-built with Bebbo's
# gcc (in ~/opt/amiga on this PC). src/tui.c is the toolkit the terminal
# programs share (Ideas/terminal-programs.md), taken from Sumi with one
# addition: tui_console(), the keys from "*" when Input() is a pipe.
#
# -fbbb=-: bebbo's backend pass off - it miscompiled tui_flush in Sumi
# (Knowledge/amiga/toolchain-and-testing.md).

AMIGA   = $(HOME)/opt/amiga
CC      = $(AMIGA)/bin/m68k-amigaos-gcc
CFLAGS  = -Os -m68020 -noixemul -fomit-frame-pointer -fno-common -fbbb=- -Wall -Wno-pointer-sign
LDFLAGS = -noixemul -s
SRC     = src/main.c src/src.c src/keys.c src/tui.c
OBJ     = $(patsubst src/%.c,build/%.o,$(SRC))

all: Maki

Maki: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o $@

build/%.o: src/%.c src/maki.h src/tui.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build Maki

.PHONY: all clean
