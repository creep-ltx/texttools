# TextTools

Three text programs for AmigaOS that run inside a terminal (CTerm, or any console):

| Program | What it is |
|---|---|
| [Sumi](sumi/) | Editor in the style of nano and micro |
| [Maki](maki/) | Pager in the style of less |
| [Markdown](markdown/) | Markdown viewer and editor |

Each program has its own folder, Makefile, README and version. All three draw the screen and read
keys through tui.library, the shared toolkit, which lives in CUtils (`Libs/tui`); they need
`LIBS:tui.library` installed.

Build: `make` inside a program's folder (Bebbo's m68k-amigaos-gcc), with the CUtils repo beside this
one (`../../CUtils/Libs/tui` from a program's folder).
