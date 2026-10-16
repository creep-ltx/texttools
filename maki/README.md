# Maki - pager

*Maki* (巻) is Japanese for a scroll. Sumi (ink) writes the text, Maki
unrolls it, and Ahiru the duck keeps the files.

Maki is **less** for the Amiga. It **lives inside the terminal**: you start
it from a Shell in CTerm (or any console window) and it draws there, in
that tab or pane. It replaces More:

- **Forwards and back**: a line, half a page, a page, to the start or the
  end, to line N or N percent. The mouse wheel works too (in CTerm).
- **Searching**: `/` forward, `?` back, `n`/`N` the next and the one
  before. The text is a regular expression (Sumi's), and every match on
  screen is marked. A capital letter makes the search exact.
- **Pipes**: `List | Maki`, `Type foo | Maki`. The screen shows at once
  and fills as the bytes come; the keys come from the console.
- **Colours**: ANSI colours show as colours (16, 256 and 24-bit, the last
  two as the nearest of CTerm's sixteen). Both ESC [ and the Amiga's 0x9B
  sequences work. man-style overstrike (`b\bb`, `_\bx`) shows as bold
  and underline.
- **Big files**: never read whole. The lines are indexed only as far as
  they are needed, and further while you read, so a 10 MB file opens
  as fast as a small one.
- **Follow** (`F`): the end of a growing file or pipe, as `tail -f`.
- **Characters**: Latin-1, UTF-8 or the PC's CP437 (.ans .nfo .diz),
  guessed by looking. `F7` changes it.
- **Several files**: `]` and `[` (or F10/F9) go between them.
- **Drawers**: `maki DRAWER` shows what is in it, as `ls -l` (bits, size,
  date, name), the names coloured by kind from `ENVARC:FileColours` -
  the same colours as `ls` and Ahiru.
- `v` opens Sumi (or ENV:EDITOR) at the line you are reading. Marks
  (`m` + a letter, `'` + the letter), line numbers (F4), long lines folded
  or cut (F8, then Left/Right look aside), `=` describes the file.
- **Every key is yours**: `ENVARC:Maki/Maki.keys`, written with the
  defaults on the first start. `h` shows every key as bound now.
- Output not a terminal (`Maki foo >file`): written out as it is, like
  `cat`.

## Usage

```
maki [-N] [-S] [+LINE | +/TEXT] [FILE | DRAWER ...]
command | maki
```

`-N` line numbers, `-S` long lines cut, `+LINE` start at that line,
`+/TEXT` start at the first match. `ENV:MAKI` holds options to start with
(as less's `LESS`).

## Keys (the defaults)

| Keys | |
|---|---|
| Space, f, PgDn, Alt+Down | a page down |
| b, Backspace, PgUp, Alt+Up | a page up |
| Return, j, Down / k, Up | a line down / up |
| d / u | half a page down / up |
| g, <, Ctrl+Up / G, >, Ctrl+Down | the start / the end (with a number: that line) |
| N % | N percent of the way |
| : , Ctrl+L | go to a line |
| Left / Right | look aside (long lines cut) |
| / ? n N | search forward, back, the next, the one before |
| F | follow the end (any key stops) |
| ] [ | the next / the previous file |
| v | edit at this line |
| m a, ' a | mark a place, back to it |
| F4, F7, F8 | line numbers, the characters, folded/cut |
| =, Ctrl+G | about the file |
| h, Help, F1 | the keys |
| q | quit |
| Esc, Ctrl+C | stop (following, a search, a number) |

A number typed first counts: `5 Space` goes five pages on, `120 g` goes
to line 120.

## Building

`make` with bebbo's gcc in `~/opt/amiga` (`-fbbb=-`, as Sumi). AmigaOS
3.x, 68020+. The screen and keys come from **tui.library**, the terminal
programs' shared toolkit (`~/Projects/CUtils/Libs/tui`, built by `make` here
when needed). Maki needs `LIBS:tui.library` 0.2 or newer; without it, it
says so and quits.

## Known limits

- When a pipe's writer never stops (`Type SER:` and the like), quitting
  leaves one small read buffer behind. The pipe may still write into it,
  so it cannot be freed safely.
- ANSI art that moves the cursor up or sideways (other than forward) is
  shown as a stream, not drawn in place.
