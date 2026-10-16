# Markdown: design and state

(Called Fude until 2026-10-14; the user preferred the plain name "Markdown".)

## Start here (2026-10-14, 0.1b1)

**What it is:** a Markdown viewer and editor that lives inside the terminal (a CTerm tab or pane,
or any console window), for AmigaOS 3.2 on a fast Amiga. Asked for by the user on 2026-10-14
("I need a real Markdown viewer/editor"); they chose a new program over growing Sumi and Maki.
It started as a copy of Sumi 0.2b2 (the whole editor), with the Markdown view added.

**State:** 0.1b1 built and tested on the A1200 in CTerm on 2026-10-14 (by me, through Wasabi):
the view (headings, emphasis, lists, task boxes, quotes and alerts, code boxes with colours,
tables, pictures), links followed between files and to headings, Back, the outline, find, edit
and split with the view following the typing, list continuation on Return, Ctrl+B, quitting with
the save question. Installed on the A1200 as C:Markdown (2026-10-14, the user asked; nothing was there before).
Not yet tried by the user. Not on the NAS or GitHub: a local git repository only.

**80 columns first** (the user, 2026-10-14: most Amigas are 640x256): split stacks the text over
the view below 120 columns (`layout()` in main.c: ew x eh for the editor, vx/vy/vw/vh for the
view); tested in a 640x256 Topaz 8 window on the A1200.

**Next:**
- the user's own try; a NAS remote like the others (ask).
- also tested 2026-10-14: CTerm with TrueType (Noto Sans Mono 14: reads best; bold, underline,
  strike show; no italic in that font) and a plain AmigaOS Shell window with Topaz 8 (`SetFont
  topaz 8` in it): Workbench's pens, ASCII lines, `#` for pictures; strikethrough does not show
  there (console.device has no SGR 9).
- not tried yet on the A1200: the format menu (Ctrl+N), `table`, `toc`, ticking a box with `x`,
  OpenURL (is openurl.library installed?), a Latin-1 file.
- ideas: `.man` pages (CUtils' format, MAN:english/...: the CUtils session offered it),
  footnotes, a dim front-matter block is there but untested, mouse selection in the view.

## Others on Aminet (checked 2026-10-14)

Aminet's search for "markdown" finds three:
- **Kickdown 1.0** (text/edit, 2026-10-03, André Gewert): OS 3.2, 68k, a ReAction window with the
  editor and an HTML preview side by side (html.gadget, md4c), plus `mdtohtml` for the Shell.
  The nearest one to this program: a GUI program, not a terminal one; needs html.gadget and AISS.
- **Earmark 1.1** (text/edit, 2021): AmigaOS 4.1 PPC, MUI; converts to HTML and opens a browser.
- **smu** (text/misc, 2008): MorphOS, a Markdown-to-HTML converter.

So this one is not the first Markdown program for the classic Amiga, but it is the only one
that runs inside a terminal or Shell window, needs nothing installed beside it, works on 80
columns, and shows the text itself (no HTML step). No Aminet package is called "Markdown".

## The files

| File | What |
|---|---|
| `src/md.c`, `md.h` | the renderer: a Buf into rows of cells (Doc), with links, headings, pictures, task boxes beside them. No screen code: tested on the PC (`tests/mdtest.c`) |
| `src/view.c` | the view: drawing a Doc, the pager keys, links and Back, find, the outline, pictures, split |
| `src/mdedit.c` | writing helpers: Return in lists/quotes, bold and the format menu, `table`, `toc` |
| `src/main.c` | Sumi's editor, with the view hooked in (drawall, key, action, mouse, help) |
| `src/keys.c` | the keys file, two sets: `[edit]` and `[view]` (`keys_find(key, view)`) |
| (tui.library) | the terminal toolkit, shared: `tui/` in the AmigaLibs repo (was `src/tui.c` until 0.1b3; italic/strike/dim, `tui_dirty`, `tui_raw` went into the library) |
| others | Sumi's: buf.c (+ `rev`, a change counter), file.c, syntax.c, clip.c |

## How the renderer works (md.c)

- **Blocks** container by container (CommonMark's way, simplified): a quote's or a list item's
  lines are taken out with their markers stripped (`SL` views into the Buf's lines, no copies) and
  read again as blocks; what goes in front of their rows (the quote bar, the bullet then spaces)
  is a stack of levels (`Lev`), the first row of an item getting its marker.
- **Gaps** (the blank row between blocks) are owed (`gap = 1`) and paid by the next row made, so a
  block that makes no rows (an HTML comment) leaves no gap; a gap row shows the bars of the levels
  already begun only.
- **Inlines**: the text of a paragraph (its lines joined by '\n') into cells (`IC`); `* _ ~` runs
  and brackets go on stacks; CommonMark's "process emphasis" matches them; markup characters stay
  in the cell list marked deleted. Links: inline, reference (definitions read first, all of
  them), autolinks, bare http/www addresses. HTML tags: b/i/s/code/kbd/u/a/img/br, the rest
  dropped, comments hidden.
- **Wrapping** at spaces; a word longer than the width is cut; a space between two parts of one
  link stays underlined.
- **Each row knows its source line** (`RRow.src`): split mode and the edit/view switch use
  `md_rowof()` to keep the same place.
- **Pictures**: a paragraph that is only pictures becomes picture rows: as many rows as the
  picture needs at its width in CTerm's cell pixels (size read from the file's header: PNG, GIF,
  JPEG, ILBM, BMP), at most the screen's height less two. The alt words go on the middle row
  (seen if the picture cannot be shown).

## Pictures in a terminal program (view.c)

- Sent as iTerm2's inline file (`OSC 1337;File=inline=1;width=Nc;height=Mc:` + base64 + BEL)
  after the cells are flushed, at the cell where it goes (`tui_raw`). CTerm decodes it with
  datatypes and places it as picture cells.
- **Only when the whole picture is in sight**: placing one moves the cursor down a line per row and
  a line feed on the last row would scroll the screen. A partly seen picture gets a line naming it.
- The toolkit does not know the picture is there: when one goes out of sight (or anything draws
  over it - help, the outline, editing), its cells are marked unknown (`tui_dirty`) so the next
  flush writes them again, which takes the picture away. `view_forget()` does that for all.

## Testing

- `tests/run.sh [N]`: the PC harness under ASAN/UBSAN on the sample and N random files, and the
  68020 build of the same harness under vamos, output compared byte for byte.
- On the A1200: `wasabi deploy Markdown RAM:Markdown`, start CTerm (`Run >NIL: CTerm`, its own screen),
  `wasabi key type 'RAM:Markdown file.md' --enter`, `wasabi grab`. `wasabi key press slash` does not
  exist: `wasabi key type '/'`. Typed letters in the view are commands: a mistyped search typed
  into the file once (e, then letters). ARexx `QUIT` did not close CTerm with a Shell tab open;
  `endcli` in the tab did.

## Traps met

- **bebbo's gcc miscompiled `static SL trimall(SL l) { ...; return l; }`** (a 12-byte struct in
  and out by value) at -O1 and -Os: it gave back an empty line, so no table was ever found, and
  the -Os harness read wild memory - running it froze the A1200 (reset by hand, 2026-10-14).
  -O0 was right. Found by building the PC harness for the Amiga and running it under vamos.
  Rule here: **no struct passed or returned by value in md.c**; and run `tests/run.sh` after
  changes.
- vamos: give `--cpu 68020` (else Alert 68020); it sees relative paths only (an absolute Linux
  path is "not found"); a 4 MB static buffer is too much for its memory.
- Fonts: CTerm draws box lines (U+2500-257F) and block elements (U+2580-259F) itself; other
  symbols come from the font: U+25E6, U+24D8, U+25A3 were missing in MicroKnight 8 and U+2713
  in Noto Sans Mono. Use Latin-1, those two ranges, and U+2022 (seen working in both).
- Testing a different CTerm font: CTerm reads ENV:CTerm.prefs at start (truetype=1, ttfile=,
  ttsize=); change the ENV: copy only, start CTerm, put the saved copy back after.
