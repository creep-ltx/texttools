# TextTools

Three text programs for AmigaOS that run inside a terminal (CTerm, or any console):

| Program | What it is |
|---|---|
| [Sumi](sumi/) | Editor in the style of nano and micro |
| [Maki](maki/) | Pager in the style of less |
| [Markdown](markdown/) | Markdown viewer and editor |

Each program has its own folder, Makefile, README and version. All three draw the screen and read
keys through tui.library, the shared toolkit, which lives in the AmigaLibs repo (`tui/`); they need
`LIBS:tui.library` installed.

Build: `make` inside a program's folder (Bebbo's m68k-amigaos-gcc), with the AmigaLibs repo beside this
one (`../../AmigaLibs/tui` from a program's folder).

markdown.datatype (the AmigaDatatypes repo) builds Markdown's `md.c` and `syntax.c` into itself, so it shows
a file the same way as the viewer.

## License

MIT, see [LICENSE](LICENSE).
