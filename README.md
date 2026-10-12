# Sumi - text editor

*Sumi* (墨) is Japanese for ink - Ahiru the duck keeps the files, Sumi
writes them.

A text editor for the Amiga that **lives inside the terminal**: you start
it from a Shell in CTerm (or any console window) and it draws there, in
that tab or pane. Simple but complete, in the way of micro and nano:

- **Keys of today**: Ctrl+S saves, Ctrl+Q closes, Ctrl+C/X/V copy and
  paste, Ctrl+Z/Y undo and redo, Ctrl+F finds, Ctrl+R replaces, Ctrl+L
  goes to a line, Shift with the moving keys selects. **Every key is
  yours** to change (`ENVARC:Sumi/Sumi.keys`).
- **The mouse** (in CTerm): a click places the cursor, a drag selects, a
  double click selects a word, the wheel scrolls; a click on a tab shows it.
- **The Amiga clipboard**, shared with every Workbench program; pasting
  through CTerm (its Amiga+V) works too, as pasted text (no indenting).
- **Colours for code**: C, 68k assembly, ARexx, Amiga E, AmigaGuide,
  AmigaDOS scripts (S:Startup-Sequence...), Python, Markdown.
- **Several files in tabs.**
- **Files kept as they are**: Latin-1 or UTF-8 (found by looking), LF or
  CR LF line ends, the last new line or not; saving goes to a new copy
  first and keeps the file's protection bits and comment.
- Line numbers, auto-indent, tabs or spaces, undo for everything.

## Usage

```
sumi [+LINE] [FILE ...]
```

`+LINE` jumps to that line of the next file. With no file, an empty one.

## Keys (the defaults)

| Keys | |
|---|---|
| Ctrl+S, F2 | save, save as |
| Ctrl+O, Ctrl+T, Ctrl+Q | open a file in a new tab, a new empty tab, close the tab |
| F9, F10 | the previous, the next tab |
| Ctrl+W | quit (asks for what is not saved) |
| Ctrl+Z, Ctrl+Y | undo, redo |
| Ctrl+C, Ctrl+X, Ctrl+V | copy, cut, paste (nothing selected: the line) |
| Ctrl+K, Ctrl+D, Ctrl+A | cut the line (again: they gather), duplicate, select all |
| Tab, Shift+Tab | indent, unindent (the selected lines too) |
| Ctrl+F, F3 / Ctrl+G, Shift+F3 | find, find the next, the previous |
| Ctrl+R, Ctrl+L | replace, go to a line |
| Ctrl+Left/Right | the line's start (twice: the very start) / end |
| Alt+Left/Right | a word back / on |
| Alt+Up/Down, Ctrl+P/N | a page up / down |
| Ctrl+Up/Down | the file's start / end |
| Shift + any of those | select |
| F1, Help | the help: every key as bound now |
| Ctrl+U | a command: `save NAME`, `open NAME`, `goto N`, `set tabsize N`, `set tabs spaces`, `set numbers off`, `set autoindent off`, `set encoding utf8`, `set lineends crlf`, `lang asm`, `quit` |
| F4 | line numbers on/off |

Why these: in a terminal the **Right Amiga** keys are CTerm's menus and
the **Left Amiga** keys its panes - a program inside never sees them.
**Ctrl-C, D, E and F** reach a program as the Amiga's break signals;
Sumi takes them as keys (Ctrl+C copies, it does not stop Sumi). The A1200
has no Home, End or Page keys, so Ctrl and Alt with the arrows do that
work - CTerm (0.3b29 and later) sends those combinations to Sumi; a plain
console window sends them as plain arrows, so Ctrl+P/N, Ctrl+B/E are
there as well.

## Settings

`ENVARC:Sumi/Sumi.prefs` (set from the command line, Ctrl+U `set ...`):
`tabsize`, `tabspaces`, `numbers`, `autoindent`.

## Inside other terminals

Made for CTerm (colours from its theme, the mouse, UTF-8, the modern key
combinations). In a plain console window it works with the keyboard, in
Workbench's colours. See `~/Projects/Ideas/terminal-programs.md` for the
family of programs that live inside the terminal; `src/tui.c` is the
toolkit they share.

## Building

`make` (Bebbo's gcc in `~/opt/amiga`). Built with `-fbbb=-`: the compiler's
own backend pass miscompiled the toolkit's drawing (see the Makefile).
Needs AmigaOS 3.x; made for 3.2 with fast RAM.
