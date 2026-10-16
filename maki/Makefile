# Maki - a pager that lives inside the terminal. Cross-built with Bebbo's
# gcc (in ~/opt/amiga on this PC). The screen and keys come from
# tui.library, the terminal programs' shared toolkit (~/Projects/CUtils/
# Libs/tui): its tui.h keeps the tui_* names, libtuiglue.a opens the
# library when Maki first needs a terminal. Maki needs LIBS:tui.library.
#
# -fbbb=-: bebbo's backend pass off - it miscompiled tui_flush in Sumi
# (Knowledge/amiga/toolchain-and-testing.md).

AMIGA   = $(HOME)/opt/amiga
CC      = $(AMIGA)/bin/m68k-amigaos-gcc
CFLAGS  = -Os -m68020 -noixemul -fomit-frame-pointer -fno-common -fbbb=- -Wall -Wno-pointer-sign
LDFLAGS = -noixemul -s
TUI     = ../CUtils/Libs/tui
CFLAGS += -I$(TUI)/include
SRC     = src/main.c src/src.c src/keys.c src/drawer.c src/filecol.c
OBJ     = $(patsubst src/%.c,build/%.o,$(SRC))

all: Maki

Maki: $(OBJ) $(TUI)/build/libtuiglue.a
	$(CC) $(LDFLAGS) $(OBJ) -L$(TUI)/build -ltuiglue -o $@

$(TUI)/build/libtuiglue.a: $(TUI)/src/glue.c $(TUI)/include/tui.h
	$(MAKE) -C $(TUI) build/libtuiglue.a

build/%.o: src/%.c src/maki.h $(TUI)/include/tui.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build Maki

.PHONY: all clean
