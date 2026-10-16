# Markdown - a Markdown viewer and editor that lives inside the terminal. Cross-built with
# Bebbo's gcc (in ~/opt/amiga on this PC). The screen and keys come from
# tui.library, the terminal programs' shared toolkit (~/Projects/CUtils/
# Libs/tui): its tui.h keeps the tui_* names, libtuiglue.a opens the
# library when Markdown first needs a terminal. Needs LIBS:tui.library.
#
# -fbbb=-: bebbo's backend pass off. With it, tui_flush's cell copy (then in
# src/tui.c) became
# cmp.l (a2)+ and then a2 used as if unmoved: every cell 4 bytes too far,
# the grid's end overrun (MuGuardianAngel, 12.10.26; toolchain-and-testing.md).

AMIGA   = $(HOME)/opt/amiga
CC      = $(AMIGA)/bin/m68k-amigaos-gcc
CFLAGS  = -Os -m68020 -noixemul -fomit-frame-pointer -fno-common -fbbb=- -Wall -Wno-pointer-sign
LDFLAGS = -noixemul -s
TUI     = ../../CUtils/Libs/tui
CFLAGS += -I$(TUI)/include
SRC     = src/main.c src/view.c src/md.c src/mdedit.c src/buf.c src/file.c src/syntax.c \
          src/clip.c src/keys.c
OBJ     = $(patsubst src/%.c,build/%.o,$(SRC))

all: Markdown

Markdown: $(OBJ) $(TUI)/build/libtuiglue.a
	$(CC) $(LDFLAGS) $(OBJ) -L$(TUI)/build -ltuiglue -o $@

$(TUI)/build/libtuiglue.a: $(TUI)/src/glue.c $(TUI)/include/tui.h
	$(MAKE) -C $(TUI) build/libtuiglue.a

build/%.o: src/%.c src/markdown.h src/md.h $(TUI)/include/tui.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build Markdown

.PHONY: all clean
