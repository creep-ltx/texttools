# Maki - a pager that lives inside the terminal. Cross-built with Bebbo's
# gcc. The screen and keys come from
# tui.library, the terminal programs' shared toolkit (the CUtils repo,
# AmigaLibs/tui): its tui.h keeps the tui_* names, libtuiglue.a opens the
# library when Maki first needs a terminal. Maki needs LIBS:tui.library.
#
# -fbbb=-: bebbo's backend pass off - it miscompiled tui_flush in Sumi
# (Knowledge/amiga/toolchain-and-testing.md).

# Bebbo's m68k-amigaos-gcc: set AMIGA to where it is installed, otherwise it is
# looked for in the usual places (/opt/amiga, its own default, then ~/opt/amiga).
AMIGA   ?= $(firstword $(wildcard /opt/amiga $(HOME)/opt/amiga))
CC      = $(AMIGA)/bin/m68k-amigaos-gcc
CFLAGS  = -Os -m68000 -noixemul -fomit-frame-pointer -fno-common -fbbb=- -Wall -Wno-pointer-sign
LDFLAGS = -noixemul -s
TUI     ?= ../../AmigaLibs/tui
CFLAGS += -I$(TUI)/include -I$(TUI)/../utf8/include
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

# clangd's include paths, written from $(AMIGA) (not tracked: they are this
# machine's paths). `make compile_flags.txt` recreates src/compile_flags.txt.
compile_flags.txt: src/compile_flags.txt
src/compile_flags.txt:
	printf '%s\n' -I$(AMIGA)/m68k-amigaos/ndk-include -I$(AMIGA)/m68k-amigaos/sys-include \
	    -I$(AMIGA)/m68k-amigaos/include -D__amigaos__ -m32 > $@

.PHONY: compile_flags.txt
