# Sumi - a text editor that lives inside the terminal. Cross-built with
# Bebbo's gcc (in ~/opt/amiga on this PC). src/tui.c is the toolkit the
# terminal programs share (Ideas/terminal-programs.md).
#
# -fbbb=-: bebbo's backend pass off. With it, tui_flush's cell copy became
# cmp.l (a2)+ and then a2 used as if unmoved: every cell 4 bytes too far,
# the grid's end overrun (MuGuardianAngel, 12.10.26; toolchain-and-testing.md).

AMIGA   = $(HOME)/opt/amiga
CC      = $(AMIGA)/bin/m68k-amigaos-gcc
CFLAGS  = -Os -m68020 -noixemul -fomit-frame-pointer -fno-common -fbbb=- -Wall -Wno-pointer-sign
LDFLAGS = -noixemul -s
SRC     = src/main.c src/buf.c src/file.c src/syntax.c src/clip.c src/keys.c src/tui.c
OBJ     = $(patsubst src/%.c,build/%.o,$(SRC))

all: Sumi

Sumi: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o $@

build/%.o: src/%.c src/sumi.h src/tui.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build Sumi

.PHONY: all clean
