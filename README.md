# TextTools

Three text programs for AmigaOS that run inside a terminal (CTerm, or any console):

| Program | What it is |
|---|---|
| [Sumi](sumi/) | Editor in the style of nano and micro |
| [Maki](maki/) | Pager in the style of less |
| [Markdown](markdown/) | Markdown viewer and editor |

Each program has its own folder, Makefile, README and version. They share one screen toolkit
(`tui.c`); for now each folder still has its own copy, which will become one shared copy.

Build: `make` inside a program's folder (Bebbo's m68k-amigaos-gcc).
