# Markdown - a Markdown viewer and editor

Markdown shows Markdown files the way they read, and edits them, **inside
the terminal** (a sibling of Sumi the editor and Maki the pager): you start it from a Shell in CTerm (or any console window) and
it draws there, in that tab or pane. It is for AmigaOS 3.2 and a fast
Amiga (PiStorm, accelerator).

- **The view**: headings, **bold**, *italic*, `code`, ~~strikethrough~~,
  lists (nested, numbered, task boxes), quotes and GitHub's
  `> [!NOTE]` alerts, code blocks in a box with Sumi's colours for the
  language (C, asm, ARexx, E, Python, shell, ...), tables with box lines
  and their alignment, rules, links, entities, and the HTML
  that READMEs use (`<img>`, `<br>`, `<b>`, `<a href>`, comments).
- **Pictures** show in the text, in CTerm (PNG, JPEG, GIF, IFF, BMP:
  whatever your datatypes read). Elsewhere a line names them.
- **Links**: Tab chooses one, Return follows it. Another Markdown file
  opens in a tab, `#heading` jumps there, an address goes to your
  browser through OpenURL (or to the clipboard). Backspace goes back.
- **Task boxes**: Tab to one, `x` ticks it; save with Ctrl+S.
- **The outline** (`o`): every heading, Return goes there.
- **Find** (`/`, `n`, `N`), as in Maki.
- **Editing**: `e` (or Ctrl+E) turns the view into text to edit, and back.
  The editor is Sumi's, whole: tabs, undo, the clipboard, search and
  replace, the mouse, macros.
- **Split** (Ctrl+P): the text and the view together, the view following
  the cursor as you type: one above the other on 80 columns (640x256, most
  Amigas), side by side on 120 columns or more (`set split auto|side|under`).
- **Writing Markdown**: Return goes on with a list or a quote (on an
  empty item it ends it), Ctrl+B makes **bold**, Ctrl+N is the format
  menu: italic, code, strike, link, picture, heading, quote, list,
  numbers, task box, code block, rule, table. The commands `table` (make
  the table at the cursor neat) and `toc` (a list of the headings, as
  links).
- **Every key is yours**: `ENVARC:Markdown/Markdown.keys`, with an `[edit]` and a
  `[view]` part, written with the defaults on the first start. `h` (in
  the view) or Help shows every key as bound now.

## Usage

```
markdown [-e] [+LINE] [FILE ...]
```

A `.md` file opens in the view; `-e` opens it to edit. Other files open
to edit, as in Sumi.

## Keys (the defaults)

In the view:

| Keys | |
|---|---|
| Space, f, PgDn / b, PgUp | a page down / up |
| Down, j / Up, k | a line |
| d / u | half a page |
| g, < / G, > | the start / the end |
| ] / [ | the next heading / the one before |
| o, t | the outline |
| Tab / Shift+Tab | the next link or task box / the one before |
| Return, Right | follow the link (or tick the box) |
| Backspace, Left | back |
| x | tick or untick a task box |
| c | copy the link's address |
| / ? n N | find, the next, the one before |
| e, Ctrl+E | edit |
| p, Ctrl+P | edit with the view beside |
| i | pictures on/off |
| q / Ctrl+W | close the tab / quit |
| h, Help | the keys |

While editing: Sumi's keys (Ctrl+S save, Ctrl+Q close, Ctrl+F find,
Ctrl+Z undo, ...), and Ctrl+E the view, Ctrl+P split, Ctrl+B bold,
Ctrl+N format, Shift+F1 the outline.

## Settings

The command key (Ctrl+U) takes `set width N` (the view's text at most so
wide, 0 the whole width; 100 at first), `set pictures on|off` and all of
Sumi's settings, and `set split auto|side|under`. They are kept in `ENVARC:Markdown/Markdown.prefs`.

## Building

`make` with bebbo's gcc in `~/opt/amiga` (`-fbbb=-`, as Sumi). 68020+. Needs
`LIBS:tui.library` 0.2 or newer, the terminal programs' shared toolkit
(`tui/` in the AmigaLibs repo; `make` builds its part when needed).
`tests/run.sh` runs the renderer on the PC and as the Amiga build under
vamos, and compares them.

## Known limits

- Wide characters (CJK, emoji) show as `?`; a font without a character
  shows it as its own blank.
- A picture shows only when the whole of it is in sight; until then a
  line names it.
- Footnotes (`[^1]`) show as written.
