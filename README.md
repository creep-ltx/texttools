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
- **Colours for code**: C, 68k assembly, ARexx, Amiga E, Blitz Basic 2
  and AmiBlitz, Installer scripts, AmigaGuide, AmigaDOS scripts
  (S:Startup-Sequence...), Python, Markdown, shell scripts, Makefiles,
  JSON, HTML/XML, Lua.
- **Several files in tabs.**
- **Files kept as they are**: Latin-1 or UTF-8 (found by looking), LF or
  CR LF line ends, the last new line or not; saving goes to a new copy
  first and keeps the file's protection bits and comment.
- **Finding**: plain or **regular expressions**, smart case (a capital
  makes it exact), every match on screen marked, find and replace with a
  question for each (or all), a history of what was sought (Up/Down).
- **Soft wrap** (long lines folded on screen, the cursor moving by screen
  rows) and **Justify** (a paragraph reflowed to a width, as nano's).
- **For code**: the matching bracket shown and jumped to, lines commented
  on and off in the file's own way, lines moved up and down, auto-indent,
  tabs or spaces, closing brackets and quotes typed for you (optional).
- **Macros**: keys recorded and played back.
- **Remembers** where you were in each file; warns before writing over a
  file that changed on disk since it was read; keeps a NAME.bak if asked.
- `run` an AmigaDOS command right in the terminal (its output stays until a
  key), `insert` a file, `count` lines, words and characters.
- Line numbers, tabs and trailing spaces shown if wanted, undo for
  everything.

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
| Ctrl+R, Ctrl+L | replace, go to a line (LINE or LINE:COLUMN) |
| F5 | to the matching bracket |
| F6 | comment the line (or the selected lines) on / off |
| Ctrl+Alt+Up/Down | move the line (or the selected lines) up / down |
| F7 | Justify: the paragraph reflowed to the wrap column (72) |
| F8 | soft wrap on / off |
| Shift+F4 | show tabs and trailing spaces on / off |
| Shift+F2 | insert a file at the cursor |
| Shift+F5, Shift+F6 | record keys (again: stop), play them |
| Ctrl+Left/Right | the line's start (twice: the very start) / end |
| Alt+Left/Right | a word back / on |
| Alt+Up/Down, Ctrl+P/N | a page up / down |
| Ctrl+Up/Down | the file's start / end |
| Shift + any of those | select |
| F1, Help | the help: every key as bound now |
| Ctrl+U | a command (below) |
| F4 | line numbers on/off |

Why these: in a terminal the **Right Amiga** keys are CTerm's menus and
the **Left Amiga** keys its panes - a program inside never sees them.
**Ctrl-C, D, E and F** reach a program as the Amiga's break signals;
Sumi takes them as keys (Ctrl+C copies, it does not stop Sumi). The A1200
has no Home, End or Page keys, so Ctrl and Alt with the arrows do that
work - CTerm (0.3b29 and later) sends those combinations to Sumi; a plain
console window sends them as plain arrows, so Ctrl+P/N, Ctrl+B/E are
there as well.

## Commands (Ctrl+U)

`save [NAME]`, `open [NAME]`, `insert [NAME]`, `goto N[:C]`, `run COMMAND`
(an AmigaDOS command in the terminal), `count`, `lang NAME` (text, C, asm,
rexx, e, bb, install, guide, script, py, md, sh, make, json, html, lua), `quit`, and
`set`:

| Setting | |
|---|---|
| `set tabsize N` | 1-16 (8) |
| `set tabs spaces` / `tabs` | what Tab puts in |
| `set numbers on/off` | line numbers |
| `set autoindent on/off` | a new line indented as the one before |
| `set wrap on/off` | soft wrap |
| `set wrapcol N` | Justify's width (72) |
| `set autoclose on/off` | brackets and quotes closed as you type them |
| `set whitespace on/off` | tabs and trailing spaces shown |
| `set regex on/off` | finding with regular expressions |
| `set case smart/exact/any` | finding's case |
| `set backup on/off` | the old version kept as NAME.bak on saving |
| `set encoding utf8/latin1` | this file's characters, converted |
| `set lineends lf/crlf` | this file's line ends |

Settings are kept in `ENVARC:Sumi/Sumi.prefs`; where each file was left in
`ENVARC:Sumi/Sumi.places`. Regular expressions: `.` `*` `+` `?` `[a-z]`
`[^...]` `^` `$` `\d` `\w` `\s` `\b`, one line at a time.

## Blitz Basic and Installer scripts

Blitz Basic 2 / AmiBlitz files (`.bb`, `.bb2`, `.ab2`, `.ab3`) must be
saved as text (ASCII) from the Blitz editor; its own tokenized files are
not text. Coloured: the language's words (If, While, Statement, NEWTYPE...),
compiler directives (WBStartup, INCLUDE, CNIF...), `#constants` and
`!macros`, `.labels`, types (`.w` `.l` `.q` `.f` `.s` `.b`, NEWTYPEs),
`$hex` and `%binary`, `;` comments.

Installer scripts are known by a name starting with `Install` (no
ending), `.install`/`.installer`, or by their first line being an
Installer call such as `(set` or `(welcome`. Coloured: the function after
each `(`, `@variables`, strings with their `\n` escapes, `;` comments.

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
