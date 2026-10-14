/* Markdown - a Markdown viewer and editor that lives inside the terminal.
 * A Markdown file opens as it reads (view.c, md.c): headings, lists,
 * tables, code in boxes, links to follow, pictures (in CTerm). Ctrl+E
 * turns it into text to edit - Sumi's editor, simple but complete in the
 * way of micro and nano: Ctrl+S saves, Ctrl+Q closes, Ctrl+C/X/V copy
 * and paste (the Amiga clipboard), Ctrl+Z undoes, Shift+arrows select,
 * the mouse places and selects; several files in tabs, Latin-1 and UTF-8
 * kept as they are. Ctrl+P edits with the view beside, following the
 * cursor. Every key is the person's (keys.c).
 *
 * Usage: markdown [-e] [+LINE] [FILE ...]      (-e: edit, not view)
 *
 * The screen, top to bottom: the tabs, the text (line numbers on the
 * left) or the view, the status line, and a line for messages,
 * questions and the keys' reminder. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <string.h>
#include <stdlib.h>
#include "markdown.h"
#include "md.h"

static const char version[] = "$VER: Markdown 0.1b1 (14.10.2026) - Markdown viewer and editor";

#define MAXTABS 24
#define PREFS   "ENVARC:Markdown/Markdown.prefs"
#define PREFENV "ENV:Markdown/Markdown.prefs"

int tabsize = 8, tabspaces = 0, numbers = 1, autoindent = 1;
int backups = 0;
int pictures = 1, mdwidth = 100;   /* the view: pictures shown, its text at most so wide (0: all) */
static int startedit;
int softwrap = 0, wrapcol = 72, autoclose = 0, showws = 0, casemode = 0, regex = 0;
/* casemode: 0 smart (a capital makes it exact), 1 exact, 2 any case */

Buf *tabs[MAXTABS];
int ntabs, ct;                   /* the tabs, the shown one */
#define B (tabs[ct])
static char msg[200];
static int msgerr;
static int follow = 1;           /* the view follows the cursor (not after the wheel) */
static int lastact;              /* the action before (cutline appends) */
char findtext[128];
static char repltext[128];
static int quitting;
static UBYTE *hlbuf;             /* a line's colours */
static long hlcap;

/* the colours: the ANSI sixteen, so CTerm's theme decides */
static const UBYTE hlcol[] = { TC_DEFAULT, 8, 2, 12, 6, 3, 5, 11 };
#define C_DIM    8
#define C_BAR    7

char *convert_text(const char *t, long n, int from, int to, long *out);
static char ftext[256];         /* what is sought, in the file's encoding */
static void action(int act);
static void selectlines(Buf *b, long a, long z);
static void doinsert(Buf *b);
char insertname[PATHMAX];
static long ftl;
static int showfound;           /* the matches on screen are marked */
static int matchin(const char *s, long len, long from, int back, const char *f, long fl, long *at, long *ml);
static int ftext_for(Buf *b);
static int isword(ULONG c);
static long bm_line = -1, bm_col, bm_line2 = -1, bm_col2;  /* a bracket and its match */

/* ---- settings ----------------------------------------------------------- */

static void prefs_load(void)
{
    BPTR fh = Open((STRPTR)PREFENV, MODE_OLDFILE);
    char line[80];
    if (!fh) fh = Open((STRPTR)PREFS, MODE_OLDFILE);
    if (!fh) return;
    while (FGets(fh, (STRPTR)line, sizeof(line))) {
        char *v = strchr(line, '=');
        if (!v) continue;
        *v++ = 0;
        if (!strcmp(line, "tabsize")) tabsize = atoi(v) >= 1 && atoi(v) <= 16 ? atoi(v) : 8;
        else if (!strcmp(line, "tabspaces")) tabspaces = *v == '1';
        else if (!strcmp(line, "numbers")) numbers = *v == '1';
        else if (!strcmp(line, "autoindent")) autoindent = *v == '1';
        else if (!strcmp(line, "softwrap")) softwrap = *v == '1';
        else if (!strcmp(line, "wrapcol")) wrapcol = atoi(v) >= 10 && atoi(v) <= 400 ? atoi(v) : 72;
        else if (!strcmp(line, "autoclose")) autoclose = *v == '1';
        else if (!strcmp(line, "whitespace")) showws = *v == '1';
        else if (!strcmp(line, "case")) casemode = atoi(v) >= 0 && atoi(v) <= 2 ? atoi(v) : 0;
        else if (!strcmp(line, "regex")) regex = *v == '1';
        else if (!strcmp(line, "backup")) backups = *v == '1';
        else if (!strcmp(line, "pictures")) pictures = *v == '1';
        else if (!strcmp(line, "split")) split = *v == '1';
        else if (!strcmp(line, "mdwidth")) mdwidth = atoi(v) == 0 || (atoi(v) >= 20 && atoi(v) <= 400) ? atoi(v) : 100;
    }
    Close(fh);
}

static void prefs_write(const char *dir, const char *path)
{
    BPTR l = Lock((STRPTR)dir, SHARED_LOCK), fh;
    char t[300];
    if (!l) l = CreateDir((STRPTR)dir);
    if (l) UnLock(l);
    if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) return;
    strcpy(t, "tabsize=");
    {
        char n[4];
        n[0] = (char)('0' + tabsize / 10);
        n[1] = (char)('0' + tabsize % 10);
        n[2] = 0;
        strcat(t, tabsize >= 10 ? n : n + 1);
    }
    strcat(t, tabspaces ? "\ntabspaces=1" : "\ntabspaces=0");
    strcat(t, numbers ? "\nnumbers=1" : "\nnumbers=0");
    strcat(t, autoindent ? "\nautoindent=1" : "\nautoindent=0");
    strcat(t, softwrap ? "\nsoftwrap=1" : "\nsoftwrap=0");
    strcat(t, autoclose ? "\nautoclose=1" : "\nautoclose=0");
    strcat(t, showws ? "\nwhitespace=1" : "\nwhitespace=0");
    strcat(t, regex ? "\nregex=1" : "\nregex=0");
    strcat(t, backups ? "\nbackup=1" : "\nbackup=0");
    strcat(t, casemode == 1 ? "\ncase=1" : casemode == 2 ? "\ncase=2" : "\ncase=0");
    strcat(t, pictures ? "\npictures=1" : "\npictures=0");
    strcat(t, split ? "\nsplit=1" : "\nsplit=0");
    {
        char n[8];
        int v = mdwidth, k = 7;
        n[7] = 0;
        do n[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
        strcat(t, "\nmdwidth=");
        strcat(t, n + k);
    }
    {
        char n[8];
        int v = wrapcol, k = 7;
        n[7] = 0;
        do n[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
        strcat(t, "\nwrapcol=");
        strcat(t, n + k);
    }
    strcat(t, "\n");
    Write(fh, t, (LONG)strlen(t));
    Close(fh);
}

void prefs_save(void)
{
    prefs_write("ENVARC:Markdown", PREFS);
    prefs_write("ENV:Markdown", PREFENV);
}

void say(const char *m, int err)
{
    strncpy(msg, m, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = 0;
    msgerr = err;
}

/* ---- the layout --------------------------------------------------------- */

int textrows(void)
{
    return tui_rows - 3 > 1 ? tui_rows - 3 : 1;
}

static int gutter(Buf *b)
{
    long n = b->n;
    int d = 1;
    if (!numbers) return 0;
    while (n >= 10) {
        n /= 10;
        d++;
    }
    return (d < 3 ? 3 : d) + 1;
}

static int textcols(Buf *b)
{
    int w = ew - gutter(b);
    return w > 1 ? w : 1;
}

/* soft wrap: how many screen rows line ln takes (a line exactly as wide
 * as the screen gets a row more, where the cursor at its end stands) */
static long nseg(Buf *b, long ln)
{
    return buf_xof(b, ln, b->l[ln].len) / textcols(b) + 1;
}

/* soft wrap: the view one screen row back (d -1) or on (d 1) */
static void vstep(Buf *b, int d)
{
    if (d < 0) {
        if (b->topseg > 0) b->topseg--;
        else if (b->top > 0) {
            b->top--;
            b->topseg = nseg(b, b->top) - 1;
        }
    } else {
        if (b->topseg < nseg(b, b->top) - 1) b->topseg++;
        else if (b->top < b->n - 1) {
            b->top++;
            b->topseg = 0;
        }
    }
}

/* the view so the cursor shows */
static void scrollto(Buf *b)
{
    long x = buf_xof(b, b->cur.line, b->cur.col);
    int h = textrows(), w = textcols(b);
    if (softwrap) {
        long cs = x / w, rows, ln;
        b->left = 0;
        if (b->top >= b->n) b->top = b->n - 1, b->topseg = 0;
        if (b->topseg >= nseg(b, b->top)) b->topseg = nseg(b, b->top) - 1;
        if (b->cur.line < b->top || (b->cur.line == b->top && cs < b->topseg)) {
            b->top = b->cur.line;
            b->topseg = cs;
            return;
        }
        if (b->cur.line - b->top > h) {     /* far below: the cursor on the last row */
            int k;
            b->top = b->cur.line;
            b->topseg = cs;
            for (k = 0; k < h - 1; k++) vstep(b, -1);
            return;
        }
        if (b->cur.line == b->top) rows = cs - b->topseg + 1;
        else {
            rows = nseg(b, b->top) - b->topseg + cs + 1;
            for (ln = b->top + 1; ln < b->cur.line; ln++) rows += nseg(b, ln);
        }
        while (rows > h) {
            vstep(b, 1);
            rows--;
        }
        return;
    }
    if (b->cur.line < b->top) b->top = b->cur.line;
    if (b->cur.line >= b->top + h) b->top = b->cur.line - h + 1;
    if (x < b->left) b->left = x > 4 ? x - 4 : 0;
    if (x >= b->left + w) b->left = x - w + 8;
    if (b->top < 0) b->top = 0;
    b->topseg = 0;
}

/* ---- drawing ------------------------------------------------------------ */

static void hs_upto(Buf *b, long line)
{
    /* the states where lines start, known from the top down */
    if (b->hsok < 1) {
        b->l[0].hs = 0;
        b->hsok = 1;
    }
    while (b->hsok <= line && b->hsok < b->n) {
        Line *p = &b->l[b->hsok - 1];
        if (hlcap < p->len + 1) {
            if (hlbuf) FreeVec(hlbuf);
            hlcap = p->len + 256;
            if (!(hlbuf = AllocVec(hlcap, MEMF_ANY))) {
                hlcap = 0;
                return;
            }
        }
        b->l[b->hsok].hs = (UBYTE)syn_line(b->lang, p->s, p->len, p->hs, hlbuf);
        b->hsok++;
    }
}

static int insel(Buf *b, long line, long col)
{
    Pos p, a, z;
    if (!b->sel) return 0;
    p.line = line;
    p.col = col;
    a = pos_cmp(b->anchor, b->cur) < 0 ? b->anchor : b->cur;
    z = pos_cmp(b->anchor, b->cur) < 0 ? b->cur : b->anchor;
    return pos_cmp(p, a) >= 0 && pos_cmp(p, z) < 0;
}

/* the matches of what was sought in a line: their starts and ends */
#define MAXMARK 48
static long mk_a[MAXMARK], mk_z[MAXMARK];
static int nmk;

static void findmarks(Buf *b, long ln)
{
    Line *l = &b->l[ln];
    long from = 0, at, ml;
    nmk = 0;
    if (!showfound || !ftl) return;
    while (nmk < MAXMARK && from <= l->len && matchin(l->s, l->len, from, 0, ftext, ftl, &at, &ml)) {
        mk_a[nmk] = at;
        mk_z[nmk++] = at + (ml ? ml : 1);
        from = at + (ml ? ml : 1);
    }
}

static int marked(long i)
{
    int k;
    for (k = 0; k < nmk; k++)
        if (i >= mk_a[k] && i < mk_z[k]) return 1;
    return 0;
}

/* line ln on screen row row, from screen column xs (the view's left, or
 * a soft-wrapped row's start); first: its first row (the number shows) */
static void drawline(Buf *b, long ln, int row, long xs, int first)
{
    Line *l = &b->l[ln];
    int g = gutter(b), w = textcols(b), sx;
    long x = 0, i = 0, trail = l->len;
    if (g) {
        char num[12];
        long v = ln + 1;
        int k = 11;
        num[11] = 0;
        do num[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
        tui_fill(0, row, g, ' ', C_DIM, TC_DEFAULT, 0);
        if (first)
            tui_put(g - 1 - (11 - k), row, num + k, 11 - k,
                    ln == b->cur.line ? TC_DEFAULT : C_DIM, TC_DEFAULT, 0);
    }
    tui_fill(g, row, w, ' ', TC_DEFAULT, TC_DEFAULT, 0);
    if (hlbuf) hs_upto(b, ln);
    if (hlcap < l->len + 1) {
        if (hlbuf) FreeVec(hlbuf);
        hlcap = l->len + 256;
        if (!(hlbuf = AllocVec(hlcap, MEMF_ANY))) hlcap = 0;
        if (hlbuf) hs_upto(b, ln);
    }
    if (hlbuf) syn_line(b->lang, l->s, l->len, l->hs, hlbuf);
    findmarks(b, ln);
    if (showws)
        while (trail > 0 && (l->s[trail - 1] == ' ' || l->s[trail - 1] == '\t')) trail--;
    while (i < l->len && x < xs + w) {
        int n, attr, fg, bg = TC_DEFAULT;
        ULONG c = buf_char(b, ln, i, &n), shown = c;
        long nx = c == '\t' ? (x / tabsize + 1) * tabsize : x + 1;
        attr = insel(b, ln, i) ? TA_REV : 0;
        fg = hlbuf ? hlcol[hlbuf[i]] : TC_DEFAULT;
        if (nmk && marked(i)) {
            fg = 0;
            bg = 3;
        }
        if ((ln == bm_line && i == bm_col) || (ln == bm_line2 && i == bm_col2)) {
            attr |= TA_BOLD | TA_UNDER;
            if (!(attr & TA_REV)) bg = 8;
        }
        if (c == '\t') shown = ' ';
        if (showws && (c == '\t' || (c == ' ' && i >= trail))) {
            shown = c == '\t' ? 0xBB : 0xB7;    /* >> and the middle dot: Latin-1 has both */
            fg = C_DIM;
        }
        for (; x < nx; x++) {
            sx = (int)(x - xs);
            if (sx >= 0 && sx < w) tui_putc(g + sx, row, shown, fg, bg, attr);
            if (c == '\t') shown = ' ';
        }
        i += n;
    }
    /* a selected line end shows as one cell */
    if (b->sel && ln < b->n - 1 && insel(b, ln, l->len) && x < xs + w) {
        sx = (int)(x - xs);
        if (sx >= 0 && sx < w) tui_putc(g + sx, row, ' ', TC_DEFAULT, TC_DEFAULT, TA_REV);
    }
}

/* the bracket at (or just before) the cursor and its partner */
static void bracketmatch(Buf *b)
{
    static const char open_[] = "([{", close_[] = ")]}";
    long ln = b->cur.line, col = b->cur.col, steps = 0;
    int depth = 0, dir, k;
    char c = 0, want, me;
    Line *l = &b->l[ln];
    bm_line = bm_line2 = -1;
    if (col < l->len && strchr("()[]{}", l->s[col])) c = l->s[col];
    else if (col > 0 && strchr("()[]{}", l->s[col - 1])) c = l->s[--col];
    if (!c) return;
    for (k = 0; k < 3 && open_[k] != c && close_[k] != c; k++) ;
    dir = open_[k] == c ? 1 : -1;
    me = c;
    want = dir > 0 ? close_[k] : open_[k];
    bm_line = ln;
    bm_col = col;
    for (;;) {
        if (dir > 0) {
            if (++col >= b->l[ln].len) {
                do {
                    if (++ln >= b->n) return;
                } while (!b->l[ln].len);
                col = 0;
            }
        } else {
            if (--col < 0) {
                do {
                    if (--ln < 0) return;
                } while (!b->l[ln].len);
                col = b->l[ln].len - 1;
            }
        }
        if (++steps > 200000) return;
        c = b->l[ln].s[col];
        if (c == me) depth++;
        else if (c == want) {
            if (!depth) {
                bm_line2 = ln;
                bm_col2 = col;
                return;
            }
            depth--;
        }
    }
}

static void drawtabs(void)
{
    int x = 0, i, from = 0;
    tui_fill(0, 0, tui_cols, ' ', C_BAR, TC_DEFAULT, 0);
    /* the shown tab must fit: start late enough */
    for (;;) {
        int w = 0;
        for (i = from; i <= ct; i++) {
            const char *n = tabs[i]->path[0] ? basename_(tabs[i]->path) : "new";
            w += (int)strlen(n) + 3;
        }
        if (w <= tui_cols - 10 || from >= ct) break;
        from++;
    }
    tui_put(x, 0, "Markdown", 8, 4, TC_DEFAULT, TA_BOLD);
    x += 9;
    if (from > 0) {
        tui_putc(x, 0, '<', C_DIM, TC_DEFAULT, 0);
        x += 2;
    }
    for (i = from; i < ntabs && x < tui_cols; i++) {
        const char *n = tabs[i]->path[0] ? basename_(tabs[i]->path) : "new";
        int l = (int)strlen(n), at = i == ct ? TA_REV | TA_BOLD : 0;
        if (x + l + 3 > tui_cols) break;
        tui_putc(x, 0, ' ', TC_DEFAULT, TC_DEFAULT, at);
        tui_put(x + 1, 0, n, l, i == ct ? TC_DEFAULT : C_DIM, TC_DEFAULT, at);
        tui_putc(x + 1 + l, 0, buf_dirty(tabs[i]) ? '*' : ' ', i == ct ? TC_DEFAULT : C_DIM,
                 TC_DEFAULT, at);
        x += l + 3;
    }
}

static char hintbuf[256];

static void hints(void)
{
    static const int acts[] = { A_SAVE, A_VIEW, A_SPLIT, A_CLOSE, A_FIND, A_UNDO, A_FORMAT,
                                A_GOTO, A_COMMAND, A_HELP };
    static const char *const words[] = { "Save", "View", "Split", "Close", "Find", "Undo",
                                         "Format", "Line", "Command", "Help" };
    int x = 0, i;
    char k[24];
    tui_fill(0, tui_rows - 1, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
    for (i = 0; i < (int)(sizeof(acts) / sizeof(acts[0])); i++) {
        int kl, wl;
        if (!keys_first(acts[i], k, 0)) continue;
        kl = (int)strlen(k);
        wl = (int)strlen(words[i]);
        if (x + kl + wl + 1 > tui_cols) break;
        tui_put(x, tui_rows - 1, k, kl, TC_DEFAULT, TC_DEFAULT, TA_REV);
        tui_put(x + kl + 1, tui_rows - 1, words[i], wl, C_DIM, TC_DEFAULT, 0);
        x += kl + wl + 3;
    }
    (void)hintbuf;
}

static void drawstatus(void)
{
    Buf *b = B;
    char s[200], n[16];
    int row = tui_rows - 2, l;
    long v;
    const char *name = b->path[0] ? b->path : "(new file)";
    tui_fill(0, row, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, TA_REV);
    strcpy(s, "line ");
    v = b->cur.line + 1;
    {
        int k = 15;
        n[15] = 0;
        do n[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
        strcat(s, n + k);
        strcat(s, "/");
        v = b->n;
        k = 15;
        do n[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
        strcat(s, n + k);
        strcat(s, "  col ");
        v = buf_xof(b, b->cur.line, b->cur.col) + 1;
        k = 15;
        do n[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
        strcat(s, n + k);
    }
    strcat(s, "   ");
    strcat(s, b->enc == ENC_UTF8 ? "UTF-8" : "Latin-1");
    if (b->crlf) strcat(s, " CRLF");
    strcat(s, "   ");
    strcat(s, langnames[b->lang]);
    strcat(s, " ");
    l = (int)strlen(s);
    if (l > tui_cols - 12) {                /* narrow: the position only */
        char *sp = strstr(s, "   ");
        if (sp) {
            sp[1] = 0;
            l = (int)strlen(s);
        }
    }
    if (l < tui_cols) tui_put(tui_cols - l, row, s, l, TC_DEFAULT, TC_DEFAULT, TA_REV);
    {   /* the name in the room left, its end kept (the end says most) */
        char t[160];
        int room = tui_cols - l - 2, nl;
        t[0] = 0;
        if (b->prot & FIBF_WRITE && b->hadfile) strcat(t, "[read-only] ");
        strncat(t, name, 120);
        if (buf_dirty(b)) strcat(t, " [changed]");
        nl = (int)strlen(t);
        if (room > 3) {
            const char *p = t;
            if (nl > room) {
                p = t + nl - room + 1;
                tui_putc(1, row, '<', TC_DEFAULT, TC_DEFAULT, TA_REV);
                tui_put(2, row, p, room - 1, TC_DEFAULT, TC_DEFAULT, TA_REV | TA_BOLD);
            } else
                tui_put(1, row, p, nl, TC_DEFAULT, TC_DEFAULT, TA_REV | TA_BOLD);
        }
    }
}

void drawall(void)
{
    Buf *b = B;
    int r, h = textrows();
    long ln, seg, cy = -1, cx = 0;
    if (b->view) {
        ew = tui_cols;
        drawtabs();
        view_draw(b, 0, tui_cols, 1, h, 1);
        view_status();
        if (msg[0]) {
            int l = (int)strlen(msg);
            tui_fill(0, tui_rows - 1, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
            tui_put(0, tui_rows - 1, msg, l < tui_cols ? l : tui_cols, msgerr ? 9 : 10, TC_DEFAULT, 0);
        } else
            view_hints();
        tui_cursor(0, 0, 0);
        tui_flush();
        view_pictures();
        return;
    }
    ew = split && tui_cols >= 40 ? tui_cols / 2 : tui_cols;
    if (ew >= tui_cols) view_forget();      /* no view: no pictures */
    if (follow) scrollto(b);
    if (!hlbuf && (hlbuf = AllocVec(256, MEMF_ANY)) != NULL) hlcap = 256;
    bracketmatch(b);
    drawtabs();
    ln = b->top;
    seg = softwrap ? b->topseg : 0;
    for (r = 0; r < h; r++) {
        if (ln < b->n) {
            long w = textcols(b);
            drawline(b, ln, r + 1, softwrap ? seg * w : b->left, seg == 0);
            if (ln == b->cur.line) {
                long x = buf_xof(b, b->cur.line, b->cur.col);
                if (!softwrap) cy = r, cx = x - b->left;
                else if (x / w == seg) cy = r, cx = x - seg * w;
            }
            if (softwrap && seg + 1 < nseg(b, ln)) seg++;
            else {
                ln++;
                seg = 0;
            }
        } else {
            tui_fill(0, r + 1, ew, ' ', TC_DEFAULT, TC_DEFAULT, 0);
            if (gutter(b)) tui_putc(gutter(b) - 2, r + 1, '~', C_DIM, TC_DEFAULT, 0);
        }
    }
    if (ew < tui_cols) {   /* the view beside, at the cursor's line */
        for (r = 0; r < h; r++) tui_putc(ew, r + 1, tui_box(BX_V), C_DIM, TC_DEFAULT, 0);
        view_follow_cursor(b, cy >= 0 ? (int)cy : 0);
        view_draw(b, ew + 1, tui_cols - ew - 1, 1, h, 0);
    }
    drawstatus();
    if (msg[0]) {
        int l = (int)strlen(msg);
        tui_fill(0, tui_rows - 1, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
        tui_put(0, tui_rows - 1, msg, l < tui_cols ? l : tui_cols, msgerr ? 9 : 10, TC_DEFAULT, 0);
    } else
        hints();
    tui_cursor(gutter(b) + (int)cx, (int)cy + 1, cy >= 0 && cx >= 0 && cx < textcols(b));
    tui_flush();
    if (ew < tui_cols) view_pictures();
}

/* ---- a question on the bottom line -------------------------------------- */

static int filecomplete(char *buf, int max, int *len, int cmd);

static int lowc(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c >= 0xC0 && c <= 0xDE && c != 0xD7 ? c + 32 : c;
}

/* 0 when the same, any case (n < 0: whole strings) */
static int nocase(const char *a, const char *b, long n)
{
    while (n-- != 0) {
        int x = lowc((unsigned char)*a), y = lowc((unsigned char)*b);
        if (x != y) return 1;
        if (!x) return 0;
        a++, b++;
    }
    return 0;
}

/* buf edited in place; 1 Return, 0 Esc. file: Tab completes names */
/* the questions' kinds (markdown.h), each with its own history (Up/Down) */
#define PHIST 12
static char phist[P_KINDS][PHIST][PATHMAX];
static int phn[P_KINDS];

static void phist_add(int kind, const char *t)
{
    if (!t[0] || (phn[kind] && !strcmp(phist[kind][phn[kind] - 1], t))) return;
    if (phn[kind] == PHIST) {
        memmove(phist[kind][0], phist[kind][1], (PHIST - 1) * PATHMAX);
        phn[kind]--;
    }
    strncpy(phist[kind][phn[kind]], t, PATHMAX - 1);
    phist[kind][phn[kind]++][PATHMAX - 1] = 0;
}

int prompt(const char *q, char *buf, int max, int kind)
{
    int len = (int)strlen(buf), pos = len, ql = (int)strlen(q);
    int fresh = len > 0;            /* a suggestion: Return takes it, typing replaces it */
    int file = kind == P_FILE || kind == P_CMD, hp = phn[kind];
    TuiEv e;
    for (;;) {
        int row = tui_rows - 1, field = tui_cols - ql - 1, start, i;
        drawall();
        tui_fill(0, row, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
        tui_put(0, row, q, ql, 14, TC_DEFAULT, TA_BOLD);
        start = pos >= field ? pos - field + 1 : 0;
        for (i = start; i < len && ql + 1 + i - start < tui_cols; i++)
            tui_putc(ql + 1 + i - start, row, (unsigned char)buf[i], TC_DEFAULT, TC_DEFAULT,
                     fresh ? TA_REV : 0);
        tui_cursor(ql + 1 + pos - start, row, 1);
        tui_flush();
        if (!tui_wait(&e, -1, 0, NULL)) continue;
        if (e.type == TE_RESIZE) {
            tui_resized();
            view_reset();
            continue;
        }
        if (e.type == TE_PASTE) {
            long k;
            if (fresh) buf[0] = 0, len = pos = 0;
            fresh = 0;
            for (k = 0; k < e.len && len < max - 1; k++)
                if (e.text[k] >= 32) {
                    memmove(buf + pos + 1, buf + pos, len - pos + 1);
                    buf[pos++] = (char)e.text[k];
                    len++;
                }
            continue;
        }
        if (e.type != TE_KEY) continue;
        if (fresh && e.key >= 32 && e.key <= 255 && !(e.key & TM_MASK)) {
            buf[0] = 0;                 /* a new text instead of the suggestion */
            len = pos = 0;
        }
        if (e.key != K_ENTER) fresh = 0;
        switch (e.key) {
        case K_ENTER: msg[0] = 0; phist_add(kind, buf); return 1;
        case K_UP: case K_DOWN:
            if (e.key == K_UP && hp > 0) hp--;
            else if (e.key == K_DOWN && hp < phn[kind]) hp++;
            else break;
            if (hp < phn[kind]) strncpy(buf, phist[kind][hp], max - 1);
            else buf[0] = 0;
            buf[max - 1] = 0;
            len = pos = (int)strlen(buf);
            break;
        case K_ESC: case TM_CTRL | 'c': case TM_CTRL | 'q': msg[0] = 0; return 0;
        case K_LEFT: if (pos > 0) pos--; break;
        case K_RIGHT: if (pos < len) pos++; break;
        case K_HOME: case K_LEFT | TM_CTRL: case K_LEFT | TM_SHIFT: pos = 0; break;
        case K_END: case K_RIGHT | TM_CTRL: case K_RIGHT | TM_SHIFT: pos = len; break;
        case K_BS:
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, len - pos + 1);
                pos--;
                len--;
            }
            break;
        case K_DEL:
            if (pos < len) {
                memmove(buf + pos, buf + pos + 1, len - pos);
                len--;
            }
            break;
        case TM_CTRL | 'u': buf[0] = 0; len = pos = 0; break;
        case K_TAB:
            if (file) {
                filecomplete(buf, max, &len, kind == P_CMD);
                pos = len;
            }
            break;
        default:
            if (e.key >= 32 && e.key <= 255 && len < max - 1) {
                memmove(buf + pos + 1, buf + pos, len - pos + 1);
                buf[pos++] = (char)e.key;
                len++;
            }
        }
    }
}

/* a key from the choices ("yn", "ynaq"): Esc gives 0 */
int ask(const char *q, const char *choices)
{
    TuiEv e;
    for (;;) {
        int l = (int)strlen(q);
        drawall();
        tui_fill(0, tui_rows - 1, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
        tui_put(0, tui_rows - 1, q, l < tui_cols ? l : tui_cols, 14, TC_DEFAULT, TA_BOLD);
        tui_cursor(l < tui_cols ? l : tui_cols - 1, tui_rows - 1, 1);
        tui_flush();
        if (!tui_wait(&e, -1, 0, NULL)) continue;
        if (e.type == TE_RESIZE) {
            tui_resized();
            view_reset();
            continue;
        }
        if (e.type != TE_KEY) continue;
        if (e.key == K_ESC || e.key == (TM_CTRL | 'c')) return 0;
        if (e.key < 128) {
            int k = e.key >= 'A' && e.key <= 'Z' ? e.key + 32 : e.key;
            if (strchr(choices, k)) return k;
        }
    }
}

/* Tab in a file name: the one name that starts so, or as much as all share */
static int filecomplete(char *buf, int max, int *len, int cmd)
{
    char dir[PATHMAX], part[108], best[108];
    char *word = buf, *sp = cmd ? strrchr(buf, ' ') : NULL;
    const char *base;
    if (sp) word = sp + 1;                      /* a command: its last word */
    base = basename_(word);
    BPTR lock;
    struct FileInfoBlock *fib;
    int pl, n = 0, bl = 0, isdir = 0;
    strncpy(dir, word, base - word);
    dir[base - word] = 0;
    strncpy(part, base, sizeof(part) - 1);
    part[sizeof(part) - 1] = 0;
    pl = (int)strlen(part);
    if (!(lock = Lock((STRPTR)dir, SHARED_LOCK))) return 0;
    if (!(fib = AllocDosObject(DOS_FIB, NULL))) {
        UnLock(lock);
        return 0;
    }
    best[0] = 0;
    if (Examine(lock, fib))
        while (ExNext(lock, fib)) {
            const char *nm = (char *)fib->fib_FileName;
            if (nocase(nm, part, pl)) continue;
            if (!n) {
                strcpy(best, nm);
                bl = (int)strlen(best);
                isdir = fib->fib_DirEntryType > 0;
            } else {
                int k = 0;
                while (k < bl && lowc((unsigned char)best[k]) == lowc((unsigned char)nm[k])) k++;
                bl = k;
                best[bl] = 0;
            }
            n++;
        }
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    if (!n) {
        tui_beep();
        return 0;
    }
    if ((int)(base - buf) + bl + 2 < max) {
        strcpy(buf + (base - buf), best);
        if (n == 1 && isdir) strcat(buf, "/");
    }
    *len = (int)strlen(buf);
    if (n > 1) tui_beep();
    return n;
}

/* ---- moving -------------------------------------------------------------- */

static int isword(ULONG c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || (c >= 0xC0 && c != 0xD7 && c != 0xF7);
}

static void setwant(Buf *b)
{
    b->wantx = buf_xof(b, b->cur.line, b->cur.col);
}

static void mv_left(Buf *b)
{
    if (b->cur.col > 0) b->cur.col = buf_prev(b, b->cur.line, b->cur.col);
    else if (b->cur.line > 0) {
        b->cur.line--;
        b->cur.col = b->l[b->cur.line].len;
    }
    setwant(b);
}

static void mv_right(Buf *b)
{
    if (b->cur.col < b->l[b->cur.line].len) b->cur.col = buf_next(b, b->cur.line, b->cur.col);
    else if (b->cur.line < b->n - 1) {
        b->cur.line++;
        b->cur.col = 0;
    }
    setwant(b);
}

/* soft wrap: up or down by screen rows, keeping the column on the row */
static void mv_vwrap(Buf *b, long d)
{
    long w = textcols(b), vx = b->wantx % w;
    while (d) {
        long x = buf_xof(b, b->cur.line, b->cur.col), sg = x / w, t;
        if (d < 0) {
            if (sg > 0) t = (sg - 1) * w + vx;
            else if (b->cur.line > 0) {
                b->cur.line--;
                t = (nseg(b, b->cur.line) - 1) * w + vx;
            } else {
                b->cur.col = 0;
                break;
            }
            d++;
        } else {
            if (sg < nseg(b, b->cur.line) - 1) t = (sg + 1) * w + vx;
            else if (b->cur.line < b->n - 1) {
                b->cur.line++;
                t = vx;
            } else {
                b->cur.col = b->l[b->cur.line].len;
                break;
            }
            d--;
        }
        b->cur.col = buf_colat(b, b->cur.line, t);
    }
}

static void mv_vert(Buf *b, long d)
{
    if (softwrap) {
        mv_vwrap(b, d);
        return;
    }
    long ln = b->cur.line + d;
    if (ln < 0) ln = 0;
    if (ln >= b->n) ln = b->n - 1;
    if (ln == b->cur.line && d) {       /* past the top or bottom: to the end */
        b->cur.col = d < 0 ? 0 : b->l[ln].len;
        setwant(b);
        return;
    }
    b->cur.line = ln;
    b->cur.col = buf_colat(b, ln, b->wantx);
}

static void mv_word(Buf *b, int d)
{
    int n;
    if (d < 0) {
        if (b->cur.col == 0) {
            mv_left(b);
            return;
        }
        while (b->cur.col > 0) {
            long p = buf_prev(b, b->cur.line, b->cur.col);
            if (isword(buf_char(b, b->cur.line, p, &n))) break;
            b->cur.col = p;
        }
        while (b->cur.col > 0) {
            long p = buf_prev(b, b->cur.line, b->cur.col);
            if (!isword(buf_char(b, b->cur.line, p, &n))) break;
            b->cur.col = p;
        }
    } else {
        Line *l = &b->l[b->cur.line];
        if (b->cur.col >= l->len) {
            mv_right(b);
            return;
        }
        while (b->cur.col < l->len && !isword(buf_char(b, b->cur.line, b->cur.col, &n)))
            b->cur.col += n;
        while (b->cur.col < l->len && isword(buf_char(b, b->cur.line, b->cur.col, &n)))
            b->cur.col += n;
    }
    setwant(b);
}

static void mv_home(Buf *b)
{
    /* the first character that is not a space; again: the very start */
    Line *l = &b->l[b->cur.line];
    long i = 0;
    while (i < l->len && (l->s[i] == ' ' || l->s[i] == '\t')) i++;
    b->cur.col = b->cur.col == i ? 0 : i;
    setwant(b);
}

static void move(int act)
{
    Buf *b = B;
    switch (act) {
    case A_UP: case A_SELUP: mv_vert(b, -1); break;
    case A_DOWN: case A_SELDOWN: mv_vert(b, 1); break;
    case A_LEFT: case A_SELLEFT: mv_left(b); break;
    case A_RIGHT: case A_SELRIGHT: mv_right(b); break;
    case A_WORDLEFT: case A_SELWORDLEFT: mv_word(b, -1); break;
    case A_WORDRIGHT: case A_SELWORDRIGHT: mv_word(b, 1); break;
    case A_LINESTART: case A_SELLINESTART: mv_home(b); break;
    case A_LINEEND: case A_SELLINEEND: b->cur.col = b->l[b->cur.line].len; setwant(b); break;
    case A_PAGEUP: case A_SELPAGEUP:
        if (softwrap) {
            int k;
            for (k = 0; k < textrows() - 1; k++) vstep(b, -1);
        } else {
            b->top -= textrows() - 1;
            if (b->top < 0) b->top = 0;
        }
        mv_vert(b, -(textrows() - 1));
        break;
    case A_PAGEDOWN: case A_SELPAGEDOWN:
        if (softwrap) {
            int k;
            for (k = 0; k < textrows() - 1; k++) vstep(b, 1);
        } else {
            b->top += textrows() - 1;
            if (b->top > b->n - 1) b->top = b->n - 1;
        }
        mv_vert(b, textrows() - 1);
        break;
    case A_FILESTART: case A_SELFILESTART: b->cur.line = 0; b->cur.col = 0; setwant(b); break;
    case A_FILEEND: case A_SELFILEEND:
        b->cur.line = b->n - 1;
        b->cur.col = b->l[b->n - 1].len;
        setwant(b);
        break;
    }
}

/* ---- changing ----------------------------------------------------------- */

static void selrange(Buf *b, Pos *a, Pos *z)
{
    if (pos_cmp(b->anchor, b->cur) < 0) {
        *a = b->anchor;
        *z = b->cur;
    } else {
        *a = b->cur;
        *z = b->anchor;
    }
}

static int delsel(Buf *b)
{
    Pos a, z;
    if (!b->sel) return 0;
    selrange(b, &a, &z);
    b->sel = 0;
    if (pos_cmp(a, z) == 0) return 0;
    buf_delete(b, a, z);
    b->cur = a;
    setwant(b);
    return 1;
}

void instext(Buf *b, const char *t, long n)
{
    b->cur = buf_insert(b, b->cur, t, n);
    setwant(b);
}

/* a character typed: in the file's own encoding */
static void typechar(Buf *b, ULONG c)
{
    char u[4];
    int n;
    if (autoclose && !b->sel) {
        Line *l = &b->l[b->cur.line];
        int next = b->cur.col < l->len ? (unsigned char)l->s[b->cur.col] : 0;
        static const char opens[] = "([{\"'", closes[] = ")]}\"'";
        const char *o = c < 128 ? strchr(closes, (int)c) : NULL;
        if (c && o && next == (int)c) {         /* over the closing one already there */
            b->cur.col++;
            setwant(b);
            return;
        }
        o = c < 128 && c ? strchr(opens, (int)c) : NULL;
        if (o && (c == '(' || c == '[' || c == '{' || !isword((ULONG)next))) {
            char pair[2];
            pair[0] = (char)c;
            pair[1] = closes[o - opens];
            if (b->typing != 1) {
                buf_group(b);
                b->typing = 1;
            }
            b->cur = buf_insert(b, b->cur, pair, 2);
            b->cur.col--;
            setwant(b);
            return;
        }
    }
    if (b->enc == ENC_LATIN1 && c > 0xFF) {
        say("this file is Latin-1: that character is not in it (Ctrl+U: set encoding utf8)", 1);
        return;
    }
    if (b->typing != 1 || b->sel) {
        buf_group(b);
        b->typing = 1;
    }
    delsel(b);
    if (b->enc == ENC_LATIN1 || c < 0x80) {
        u[0] = (char)c;
        n = 1;
    } else if (c < 0x800) {
        u[0] = (char)(0xC0 | (c >> 6));
        u[1] = (char)(0x80 | (c & 0x3F));
        n = 2;
    } else if (c < 0x10000) {
        u[0] = (char)(0xE0 | (c >> 12));
        u[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        u[2] = (char)(0x80 | (c & 0x3F));
        n = 3;
    } else {
        u[0] = (char)(0xF0 | (c >> 18));
        u[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        u[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        u[3] = (char)(0x80 | (c & 0x3F));
        n = 4;
    }
    instext(b, u, n);
}

static void indentline(Buf *b, long ln)
{
    Pos p;
    p.line = ln;
    p.col = 0;
    if (tabspaces) {
        static const char sp[] = "                ";
        buf_insert(b, p, sp, tabsize);
    } else
        buf_insert(b, p, "\t", 1);
}

static long unindentline(Buf *b, long ln)
{
    Line *l = &b->l[ln];
    Pos a, z;
    long n = 0;
    if (l->len && l->s[0] == '\t') n = 1;
    else
        while (n < tabsize && n < l->len && l->s[n] == ' ') n++;
    if (!n) return 0;
    a.line = z.line = ln;
    a.col = 0;
    z.col = n;
    buf_delete(b, a, z);
    return n;
}

static void dotab(Buf *b, int un)
{
    Pos a, z;
    long ln, last;
    buf_group(b);
    if (b->sel || un) {
        if (b->sel) selrange(b, &a, &z);
        else a = z = b->cur;
        last = z.line;
        if (b->sel && z.col == 0 && z.line > a.line) last--;   /* a line just reached is not in */
        for (ln = a.line; ln <= last; ln++) {
            if (un) {
                long n = unindentline(b, ln);
                if (ln == b->cur.line) b->cur.col = b->cur.col > n ? b->cur.col - n : 0;
                if (b->sel && ln == b->anchor.line)
                    b->anchor.col = b->anchor.col > n ? b->anchor.col - n : 0;
            } else if (b->l[ln].len) {
                indentline(b, ln);
                if (ln == b->cur.line && b->cur.col > 0) b->cur.col += tabspaces ? tabsize : 1;
                if (b->sel && ln == b->anchor.line && b->anchor.col > 0)
                    b->anchor.col += tabspaces ? tabsize : 1;
            }
        }
        setwant(b);
        return;
    }
    if (tabspaces) {
        static const char sp[] = "                ";
        long x = buf_xof(b, b->cur.line, b->cur.col);
        instext(b, sp, tabsize - x % tabsize);
    } else
        instext(b, "\t", 1);
}

static void donewline(Buf *b)
{
    Line *l;
    long i = 0;
    char *ind = NULL;
    buf_group(b);
    delsel(b);
    l = &b->l[b->cur.line];
    if (autoindent) {
        while (i < l->len && i < b->cur.col && (l->s[i] == ' ' || l->s[i] == '\t')) i++;
        if (i && (ind = AllocVec(i + 1, MEMF_ANY)) != NULL) memcpy(ind + 1, l->s, i);
    }
    if (ind) {
        ind[0] = '\n';
        instext(b, ind, i + 1);
        FreeVec(ind);
    } else
        instext(b, "\n", 1);
    buf_group(b);
}

static void dobackspace(Buf *b, int word)
{
    Pos a, z;
    if (b->sel) {
        buf_group(b);
        delsel(b);
        return;
    }
    if (b->typing != 2) {
        buf_group(b);
        b->typing = 2;
    }
    z = b->cur;
    if (word) mv_word(b, -1);
    else mv_left(b);
    a = b->cur;
    buf_delete(b, a, z);
    b->cur = a;
}

static void dodelete(Buf *b, int word)
{
    Pos a = b->cur, z;
    if (b->sel) {
        buf_group(b);
        delsel(b);
        return;
    }
    if (b->typing != 2) {
        buf_group(b);
        b->typing = 2;
    }
    if (word) mv_word(b, 1);
    else mv_right(b);
    z = b->cur;
    b->cur = a;
    buf_delete(b, a, z);
    setwant(b);
}

/* the selection's text, or (none) the whole line with its new line */
static char *grab(Buf *b, long *n, Pos *a, Pos *z, int *wholeline)
{
    *wholeline = 0;
    if (b->sel) selrange(b, a, z);
    else {
        a->line = b->cur.line;
        a->col = 0;
        if (b->cur.line < b->n - 1) {
            z->line = b->cur.line + 1;
            z->col = 0;
        } else {
            z->line = b->cur.line;
            z->col = b->l[b->cur.line].len;
        }
        *wholeline = 1;
    }
    return buf_text(b, *a, *z, n);
}

static char *cutacc;             /* Ctrl+K in a row: the lines gather */
static long cutacclen;

static void docopycut(Buf *b, int cut, int line)
{
    Pos a, z;
    long n;
    int whole;
    char *t;
    if (line) b->sel = 0;
    t = grab(b, &n, &a, &z, &whole);
    if (!t) return;
    if (whole && z.line == a.line && a.line == b->n - 1 && n) {
        /* the last line has no new line of its own: give it one */
        char *t2 = AllocVec(n + 2, MEMF_ANY);
        if (t2) {
            memcpy(t2, t, n);
            t2[n++] = '\n';
            FreeVec(t);
            t = t2;
        }
    }
    if (line && lastact == A_CUTLINE && cutacc) {
        char *j = AllocVec(cutacclen + n + 1, MEMF_ANY);
        if (j) {
            memcpy(j, cutacc, cutacclen);
            memcpy(j + cutacclen, t, n);
            FreeVec(t);
            t = j;
            n += cutacclen;
        }
    }
    clip_put(t, n, b->enc);
    if (cutacc) FreeVec(cutacc);
    cutacc = t;
    cutacclen = n;
    if (cut) {
        buf_group(b);
        if (whole && pos_cmp(a, z) == 0 && b->n > 1 && b->cur.line == b->n - 1) {
            a.line--;                   /* the last line, empty: its line goes */
            a.col = b->l[a.line].len;
        } else if (whole && z.line == a.line && a.line == b->n - 1 && a.line > 0) {
            a.line--;
            a.col = b->l[a.line].len;
        }
        buf_delete(b, a, z);
        b->cur = a;
        if (whole) b->cur.col = 0;
        if (b->cur.line >= b->n) b->cur.line = b->n - 1;
        b->sel = 0;
        setwant(b);
        buf_group(b);
    }
    {
        char m[60];
        long k = 0, i;
        for (i = 0; i < n; i++)
            if (t[i] == '\n') k++;
        strcpy(m, cut ? "cut" : "copied");
        strcat(m, whole ? " the line" : " the selection");
        if (line && lastact == A_CUTLINE) strcpy(m, "cut lines (gathering)");
        say(m, 0);
        (void)k;
    }
}

static void dopaste(Buf *b, const char *t, long n, int from)
{
    char *c;
    long cn;
    buf_group(b);
    delsel(b);
    if (from >= 0) {                    /* a terminal paste: its own characters */
        if (!(c = clip_mine(t, n, b->enc, &cn)) && !(c = convert_text(t, n, from, b->enc, &cn)))
            return;
    } else {
        if (!(c = clip_get(&cn, b->enc))) {
            say("the clipboard has no text", 1);
            return;
        }
    }
    instext(b, c, cn);
    FreeVec(c);
    buf_group(b);
}

static void doduplicate(Buf *b)
{
    Pos a, z;
    long n;
    int whole;
    char *t = grab(b, &n, &a, &z, &whole);
    if (!t) return;
    buf_group(b);
    if (whole) {
        if (z.line == a.line) {         /* the last line: a new line first */
            Pos e = z;
            char *t2 = AllocVec(n + 2, MEMF_ANY);
            if (t2) {
                t2[0] = '\n';
                memcpy(t2 + 1, t, n);
                buf_insert(b, e, t2, n + 1);
                FreeVec(t2);
            }
        } else
            buf_insert(b, a, t, n);
        b->cur.line++;
    } else {
        Pos e = buf_insert(b, z, t, n);
        b->anchor = z;
        b->cur = e;
    }
    FreeVec(t);
    setwant(b);
    buf_group(b);
}

/* ---- finding ------------------------------------------------------------ */

static int ci(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c >= 0xC0 && c <= 0xDE && c != 0xD7 ? c + 32 : c;
}

/* ---- finding: plain or a regular expression -------------------------------
 * The expressions: . any character, * + ? repeats (greedy), [abc] [^a-z]
 * classes, ^ $ the line's ends, \d \w \s digits/word/space (\D \W \S the
 * others), \b a word's edge, \x the character x. One line at a time. */

static int fexact;              /* this search's case: exact or not */

static int fci(int c)
{
    return fexact ? c : ci(c);
}

static const char *re_classend(const char *p)
{
    if (*p == '^') p++;
    if (*p == ']') p++;
    while (*p && *p != ']') p++;
    return p;
}

/* one element of the pattern at p matches the byte c */
static int re_one(const char *p, const char *pe, int c)
{
    if (*p == '.') return 1;
    if (*p == '\\' && p + 1 < pe) {
        int k = (unsigned char)p[1];
        switch (k) {
        case 'd': return c >= '0' && c <= '9';
        case 'D': return !(c >= '0' && c <= '9');
        case 'w': return isword((ULONG)c);
        case 'W': return !isword((ULONG)c);
        case 's': return c == ' ' || c == '\t';
        case 'S': return !(c == ' ' || c == '\t');
        case 't': return c == '\t';
        default: return fci(c) == fci(k);
        }
    }
    if (*p == '[') {
        const char *q = p + 1;
        int neg = 0, in = 0;
        if (*q == '^') neg = 1, q++;
        if (*q == ']') {
            in |= c == ']';
            q++;
        }
        while (q < pe && *q != ']') {
            int lo = (unsigned char)*q;
            if (*q == '\\' && q + 1 < pe) {
                char tmp[3] = { '\\', q[1], 0 };
                in |= re_one(tmp, tmp + 2, c);
                q += 2;
                continue;
            }
            if (q + 2 < pe && q[1] == '-' && q[2] != ']') {
                int hi = (unsigned char)q[2];
                if (fexact ? (c >= lo && c <= hi) : ((ci(c) >= ci(lo) && ci(c) <= ci(hi)) || (c >= lo && c <= hi)))
                    in = 1;
                q += 3;
                continue;
            }
            if (fci(c) == fci(lo)) in = 1;
            q++;
        }
        return neg ? !in : in;
    }
    return fci(c) == fci((unsigned char)*p);
}

/* the length of the element at p */
static const char *re_next(const char *p)
{
    if (*p == '\\' && p[1]) return p + 2;
    if (*p == '[') {
        const char *e = re_classend(p + 1);
        return *e ? e + 1 : e;
    }
    return p + 1;
}

/* the pattern p..pe at s[i]; the end of the match in *end */
static int re_here(const char *p, const char *pe, const char *s, long len, long i, long *end)
{
    while (p < pe) {
        const char *n;
        int rep;
        if (*p == '$' && p + 1 == pe) {
            if (i != len) return 0;
            p++;
            continue;
        }
        if (*p == '\\' && p + 1 < pe && p[1] == 'b') {
            int a = i > 0 && isword((unsigned char)s[i - 1]), c = i < len && isword((unsigned char)s[i]);
            if (a == c) return 0;
            p += 2;
            continue;
        }
        n = re_next(p);
        rep = n < pe ? *n : 0;
        if (rep == '*' || rep == '+' || rep == '?') {
            long k = i, most;
            while (k < len && (rep != '?' || k == i) && re_one(p, n, (unsigned char)s[k])) k++;
            most = k;
            if (rep == '+' && most == i) return 0;
            for (k = most; k >= i + (rep == '+'); k--)  /* the longest first */
                if (re_here(n + 1, pe, s, len, k, end)) return 1;
            return 0;
        }
        if (i >= len || !re_one(p, n, (unsigned char)s[i])) return 0;
        i++;
        p = n;
    }
    *end = i;
    return 1;
}

/* a match in s at or after from (or before, back): its start, its length */
static int matchin(const char *s, long len, long from, int back, const char *f, long fl, long *at, long *ml)
{
    long i;
    if (!regex) {
        long k;
        for (i = from; back ? i >= 0 : i + fl <= len; back ? i-- : i++) {
            if (i + fl > len) continue;
            for (k = 0; k < fl; k++)
                if (fci((unsigned char)s[i + k]) != fci((unsigned char)f[k])) break;
            if (k == fl) {
                *at = i;
                *ml = fl;
                return 1;
            }
        }
        return 0;
    }
    {
        const char *p = f, *pe = f + fl;
        int anchored = *p == '^';
        long end;
        if (anchored) p++;
        for (i = from; back ? i >= 0 : i <= len; back ? i-- : i++) {
            if (anchored && i != 0) {
                if (back) { i = 0; if (re_here(p, pe, s, len, 0, &end)) { *at = 0; *ml = end; return 1; } }
                return 0;
            }
            if (re_here(p, pe, s, len, i, &end) && (end > i || fl == 1 + anchored)) {
                *at = i;
                *ml = end - i;
                return 1;
            }
        }
        return 0;
    }
}

static void findsetup(void)
{
    long k, fl = (long)strlen(findtext);
    fexact = casemode == 1;
    if (casemode == 0)
        for (k = 0; k < fl; k++)
            if ((findtext[k] >= 'A' && findtext[k] <= 'Z') ||
                ((unsigned char)findtext[k] >= 0xC0 && (unsigned char)findtext[k] <= 0xDE)) fexact = 1;
}


/* a row of the view (Latin-1) searched for findtext */
int findrow(const char *s, long len, long from, int back, long *at, long *ml)
{
    long fl = (long)strlen(findtext);
    if (!fl) return 0;
    findsetup();
    return matchin(s, len, from, back, findtext, fl, at, ml);
}

static int ftext_for(Buf *b)
{
    long fl = (long)strlen(findtext);
    char *c = convert_text(findtext, fl, ENC_LATIN1, b->enc, &ftl);
    if (!c) return 0;
    if (ftl >= (long)sizeof(ftext)) ftl = sizeof(ftext) - 1;
    memcpy(ftext, c, ftl);
    ftext[ftl] = 0;
    FreeVec(c);
    findsetup();
    return ftl > 0;
}

static int find(Buf *b, int dir, int quiet)
{
    long ln, col, k, at, ml;
    int wrapped = 0;
    Pos a, z;
    if (!findtext[0] || !ftext_for(b)) return 0;
    if (b->sel) selrange(b, &a, &z);
    else a = z = b->cur;
    ln = dir > 0 ? z.line : a.line;
    col = dir > 0 ? z.col : a.col - 1;
    if (dir > 0 && b->sel && pos_cmp(a, z) == 0) col++;
    for (k = 0; k <= b->n + 1; k++) {
        Line *l = &b->l[ln];
        if (col >= 0 && col <= l->len && matchin(l->s, l->len, col, dir < 0, ftext, ftl, &at, &ml)) {
            b->anchor.line = ln;
            b->anchor.col = at;
            b->cur.line = ln;
            b->cur.col = at + ml;
            b->sel = ml > 0;
            setwant(b);
            follow = 1;
            showfound = 1;
            if (!quiet) say(wrapped ? "found (from the other end)" : "", 0);
            return 1;
        }
        if (dir > 0) {
            ln++;
            col = 0;
            if (ln >= b->n) {
                ln = 0;
                wrapped = 1;
            }
        } else {
            ln--;
            if (ln < 0) {
                ln = b->n - 1;
                wrapped = 1;
            }
            col = b->l[ln].len;
        }
    }
    if (!quiet) {
        char m[180];
        strcpy(m, regex ? "not found (expression): " : "not found: ");
        strncat(m, findtext, 120);
        say(m, 1);
    }
    return 0;
}

static void doreplace(Buf *b)
{
    int all = 0, n = 0;
    long rl;
    char *rtext;
    char m[60];
    if (!prompt("Find:", findtext, sizeof(findtext), P_FIND) || !findtext[0]) return;
    if (!prompt("Replace with:", repltext, sizeof(repltext), P_REPL)) return;
    if (!(rtext = convert_text(repltext, strlen(repltext), ENC_LATIN1, b->enc, &rl))) return;
    buf_group(b);
    b->sel = 0;
    while (find(b, 1, 1)) {
        int k = 'y';
        Pos a, z;
        if (!all) {
            follow = 1;
            k = ask("Replace this one? (y)es (n)o (a)ll (q)uit", "ynaq");
            if (!k || k == 'q') break;
            if (k == 'a') all = 1;
        }
        selrange(b, &a, &z);
        if (k == 'n') {
            b->cur = z;
            b->sel = 0;
            continue;
        }
        buf_delete(b, a, z);
        b->cur = buf_insert(b, a, rtext, rl);
        b->sel = 0;
        n++;
        if (n > 100000) break;
    }
    FreeVec(rtext);
    buf_group(b);
    b->sel = 0;
    strcpy(m, "replaced ");
    {
        char d[12];
        int k = 11;
        long v = n;
        d[11] = 0;
        do d[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
        strcat(m, d + k);
    }
    say(m, 0);
}

static void dogoto(Buf *b)
{
    static char t[16];
    long ln;
    t[0] = 0;
    if (!prompt("Go to line:", t, sizeof(t), P_GOTO) || !t[0]) return;
    ln = atol(t);
    if (ln < 1) ln = 1;
    if (ln > b->n) ln = b->n;
    b->cur.line = ln - 1;
    b->cur.col = 0;
    {   /* LINE:COLUMN (or LINE,COLUMN) */
        char *c = strchr(t, ':');
        if (!c) c = strchr(t, ',');
        if (c && atol(c + 1) > 1) b->cur.col = buf_colat(b, b->cur.line, atol(c + 1) - 1);
    }
    b->sel = 0;
    setwant(b);
    follow = 1;
    b->top = b->cur.line - textrows() / 2;
    if (b->top < 0) b->top = 0;
}

/* ---- files and tabs ----------------------------------------------------- */

/* ---- where each file was left (ENVARC:Markdown/Markdown.places) ---------------- */

#define PLACES "ENVARC:Markdown/Markdown.places"
#define MAXPLACES 100
static struct { long line, col; char path[PATHMAX]; } *places;
static int nplaces, placesread;

static void places_read(void)
{
    BPTR fh;
    char l[PATHMAX + 30];
    placesread = 1;
    if (!places && !(places = AllocVec(MAXPLACES * sizeof(*places), MEMF_ANY | MEMF_CLEAR))) return;
    if (!(fh = Open((STRPTR)PLACES, MODE_OLDFILE))) return;
    while (nplaces < MAXPLACES && FGets(fh, (STRPTR)l, sizeof(l))) {
        char *p = l, *e;
        long ln = strtol(p, &e, 10), co;
        if (e == p) continue;
        p = e;
        co = strtol(p, &e, 10);
        if (e == p || *e != ' ') continue;
        p = e + 1;
        e = p + strlen(p);
        while (e > p && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
        places[nplaces].line = ln;
        places[nplaces].col = co;
        strncpy(places[nplaces].path, p, PATHMAX - 1);
        nplaces++;
    }
    Close(fh);
}

static void places_note(Buf *b)
{
    int i;
    if (!b->path[0]) return;
    if (!placesread) places_read();
    if (!places) return;
    for (i = 0; i < nplaces && nocase(places[i].path, b->path, -1); i++) ;
    if (i == nplaces) {
        if (nplaces == MAXPLACES) i = nplaces - 1;
        else nplaces++;
    }
    memmove(&places[1], &places[0], i * sizeof(*places));   /* the newest first */
    places[0].line = b->cur.line;
    places[0].col = b->cur.col;
    strncpy(places[0].path, b->path, PATHMAX - 1);
    places[0].path[PATHMAX - 1] = 0;
}

static void places_save(void)
{
    BPTR fh, l;
    int i;
    char t[PATHMAX + 30], n[24];
    if (!places || !nplaces) return;
    if (!(l = Lock((STRPTR)"ENVARC:Markdown", SHARED_LOCK))) l = CreateDir((STRPTR)"ENVARC:Markdown");
    if (l) UnLock(l);
    if (!(fh = Open((STRPTR)PLACES, MODE_NEWFILE))) return;
    for (i = 0; i < nplaces; i++) {
        long v;
        int k;
        t[0] = 0;
        v = places[i].line; k = 23; n[23] = 0;
        do n[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
        strcat(t, n + k);
        strcat(t, " ");
        v = places[i].col; k = 23;
        do n[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
        strcat(t, n + k);
        strcat(t, " ");
        strcat(t, places[i].path);
        strcat(t, "\n");
        Write(fh, t, (LONG)strlen(t));
    }
    Close(fh);
}

static int places_recall(Buf *b)
{
    int i;
    if (!placesread) places_read();
    for (i = 0; places && i < nplaces; i++)
        if (!nocase(places[i].path, b->path, -1)) {
            b->cur.line = places[i].line < b->n ? places[i].line : b->n - 1;
            b->cur.col = places[i].col <= b->l[b->cur.line].len ? places[i].col : 0;
            b->top = b->cur.line > textrows() / 2 ? b->cur.line - textrows() / 2 : 0;
            return 1;
        }
    return 0;
}

/* a file's date now (0: none) */
static int filedate(const char *path, struct DateStamp *ds)
{
    BPTR l = Lock((STRPTR)path, SHARED_LOCK);
    struct FileInfoBlock *fib;
    int ok = 0;
    if (!l) return 0;
    if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(l, fib)) {
            *ds = fib->fib_Date;
            ok = 1;
        }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(l);
    return ok;
}

int addtab(const char *path)
{
    Buf *b;
    int r;
    if (ntabs >= MAXTABS) {
        say("too many tabs", 1);
        return 0;
    }
    if (!(b = buf_new())) {
        say("out of memory", 1);
        return 0;
    }
    b->frow = -1;
    b->lang = L_NONE;
    if (path && path[0]) {
        r = file_load(b, path);
        if (r < 0) {
            char m[200];
            strcpy(m, basename_(path));
            strcat(m, ": ");
            strcat(m, file_err());
            say(m, 1);
            buf_free(b);
            return 0;
        }
        b->lang = syn_guess(path, b);
        if (r > 0) places_recall(b);
        if (r > 0 && b->lang == L_MD && !startedit) {
            b->view = 1;
            view_fromfile(b);
        }
        if (!r) say("a new file", 0);
        else if (b->prot & FIBF_WRITE) say("this file is protected against writing", 1);
    }
    tabs[ntabs] = b;
    ct = ntabs++;
    return 1;
}

static void closetab(int i)
{
    places_note(tabs[i]);
    places_save();
    view_close(tabs[i]);
    buf_free(tabs[i]);
    memmove(&tabs[i], &tabs[i + 1], (ntabs - i - 1) * sizeof(Buf *));
    ntabs--;
    if (ct >= ntabs) ct = ntabs - 1;
    if (ct < 0) ct = 0;
}

int samefile(const char *a, const char *b)
{
    BPTR la = Lock((STRPTR)a, SHARED_LOCK), lb;
    int r = 0;
    if (!la) return 0;
    if ((lb = Lock((STRPTR)b, SHARED_LOCK)) != 0) {
        r = SameLock(la, lb) == LOCK_SAME;
        UnLock(lb);
    }
    UnLock(la);
    return r;
}

static int dosave(Buf *b, int as)
{
    static char path[PATHMAX];
    char m[200];
    long i, k;
    if (as || !b->path[0]) {
        strcpy(path, b->path);
        if (!prompt("Save as:", path, sizeof(path), P_FILE) || !path[0]) return 0;
        if (strcmp(path, b->path)) {
            BPTR l = Lock((STRPTR)path, SHARED_LOCK);
            if (l) {
                UnLock(l);
                if (ask("That file is there already. Write over it? (y/n)", "yn") != 'y') return 0;
            }
        }
    } else
        strcpy(path, b->path);
    if (b->hadfile && !strcmp(path, b->path)) {
        struct DateStamp now;
        if (filedate(path, &now) && CompareDates(&now, &b->disk) != 0 &&
            ask("The file changed on disk since it was read. Write over it? (y/n)", "yn") != 'y')
            return 0;
    }
    if (b->hadfile && (b->prot & (FIBF_WRITE | FIBF_DELETE)) && !strcmp(path, b->path)) {
        say("the file is protected against writing or deleting (Protect it +wd first)", 1);
        return 0;
    }
    if (!file_save(b, path)) {
        strcpy(m, "not saved: ");
        strcat(m, file_err());
        say(m, 1);
        return 0;
    }
    if (b->lang == L_NONE) b->lang = syn_guess(path, b);
    filedate(path, &b->disk);
    for (i = 0, k = 0; i < b->n; i++) k += b->l[i].len + 1;
    strcpy(m, "saved ");
    strncat(m, basename_(path), 100);
    say(m, 0);
    return 1;
}

/* a tab with changes: save them? 1 go on, 0 stop */
static int keepornot(int i)
{
    Buf *b = tabs[i];
    char q[200];
    int k;
    if (!buf_dirty(b)) return 1;
    ct = i;
    strcpy(q, "Save the changes to ");
    strncat(q, b->path[0] ? basename_(b->path) : "the new file", 100);
    strcat(q, "? (y)es (n)o, Esc: back");
    k = ask(q, "yn");
    if (!k) return 0;
    if (k == 'y') return dosave(b, 0);
    return 1;
}

static void doopen(void)
{
    static char path[PATHMAX];
    int i;
    if (!path[0] && B->path[0]) {
        strcpy(path, B->path);
        path[basename_(path) - path] = 0;
    }
    if (!prompt("Open:", path, sizeof(path), P_FILE) || !path[0]) return;
    for (i = 0; i < ntabs; i++)
        if (tabs[i]->path[0] && samefile(tabs[i]->path, path)) {
            ct = i;
            say("it was open already", 0);
            return;
        }
    if (ntabs == 1 && !B->path[0] && !buf_dirty(B) && B->n == 1 && !B->l[0].len) {
        /* the one empty new tab: the file takes its place */
        if (addtab(path)) {
            closetab(0);
            ct = ntabs - 1;
        }
        return;
    }
    addtab(path);
}

/* ---- help ---------------------------------------------------------------- */

/* the keys bound to an action, as nano writes them ("^S F2") */
static void keysfor(int act, int view, char *names)
{
    static const int mods[] = { 0, TM_CTRL, TM_ALT, TM_SHIFT, TM_SHIFT | TM_CTRL,
                                TM_SHIFT | TM_ALT, TM_CTRL | TM_ALT };
    int m, c, any = 0;
    char k[24];
    names[0] = 0;
    for (m = 0; m < 7 && strlen(names) < 30; m++) {
        for (c = K_UP; c <= K_F(12) && strlen(names) < 30; c++)
            if (keys_find(c | mods[m], view) == act) {
                keyname(c | mods[m], k, 1);
                if (any) strcat(names, " ");
                strcat(names, k);
                any = 1;
            }
        for (c = ' '; c < 127 && strlen(names) < 30; c++)
            if ((m || view) && keys_find(c | mods[m], view) == act) {
                if (c == ' ' && !m) strcpy(k, "Space");
                else keyname(c | mods[m], k, 1);
                if (any) strcat(names, " ");
                strcat(names, k);
                any = 1;
            }
    }
}

static void help(void)
{
    static const int vorder[] = {
        A_LINEDOWN, A_LINEUP, A_PAGEDOWN, A_PAGEUP, A_HALFDOWN, A_HALFUP, A_FILESTART,
        A_FILEEND, A_NEXTHEAD, A_PREVHEAD, A_OUTLINE, 0,
        A_NEXTLINK, A_PREVLINK, A_FOLLOW, A_BACK, A_TASK, A_COPYLINK, 0,
        A_FIND, A_FINDNEXT, A_FINDPREV, 0, A_VIEW, A_SPLIT, A_PICTURES, A_SAVE, A_OPEN,
        A_NEWTAB, A_CLOSE, A_NEXTTAB, A_PREVTAB, A_QUIT, A_COMMAND, A_HELP, -1
    };
    static const int eorder[] = {
        A_VIEW, A_SPLIT, A_BOLD, A_FORMAT, A_OUTLINE, 0,
        A_SAVE, A_SAVEAS, A_OPEN, A_NEWTAB, A_CLOSE, A_NEXTTAB, A_PREVTAB, A_QUIT, 0,
        A_UNDO, A_REDO, A_CUT, A_COPY, A_PASTE, A_CUTLINE, A_DUPLICATE, A_SELECTALL,
        A_TAB, A_UNTAB, A_DELWORDLEFT, A_DELWORDRIGHT, 0,
        A_FIND, A_FINDNEXT, A_FINDPREV, A_REPLACE, A_GOTO, 0,
        A_WORDLEFT, A_WORDRIGHT, A_LINESTART, A_LINEEND, A_PAGEUP, A_PAGEDOWN,
        A_FILESTART, A_FILEEND, 0, A_MATCH, A_COMMENT, A_MOVEUP, A_MOVEDOWN, A_JUSTIFY,
        A_INSERT, A_MACROREC, A_MACROPLAY, 0, A_SOFTWRAP, A_WHITESPACE, A_NUMBERS,
        A_COMMAND, A_ESCAPE, A_HELP, -1
    };
    static const char *const intro[] = {
        "Markdown - a Markdown viewer and editor inside the terminal",
        "",
        "A Markdown file opens as it reads; the view key turns it into text to edit",
        "and back, the split key edits with the view beside. Links: Tab to one,",
        "Return follows (another file opens in a tab, an address goes to the browser",
        "through OpenURL or to the clipboard), Backspace goes back. Task boxes: Tab",
        "to one, x ticks it (then save). Pictures show in CTerm.",
        "While editing: Return goes on with a list or quote; the format key makes",
        "italic, code, links, headings, lists, quotes, tasks, tables neat.",
        "Commands (the command key): save NAME, open NAME, insert NAME, goto N,",
        "run COMMAND, count, table, toc, lang NAME, quit; set: width N (the view's",
        "text, 0 all), pictures on|off, tabsize N, tabs spaces|tabs, numbers/",
        "autoindent/wrap/autoclose/whitespace/regex/backup on|off, wrapcol N,",
        "case smart|exact|any, encoding utf8|latin1, lineends lf|crlf.",
        "Keys: ENVARC:Markdown/Markdown.keys ([edit] and [view])",
        NULL
    };
    int top = 0, total, r, i;
    TuiEv e;
    for (;;) {
        int row = 0, line = 0, sec;
        tui_clear();
        for (i = 0; intro[i]; i++, line++)
            if (line >= top && row < tui_rows - 1) {
                tui_put(1, row, intro[i], (int)strlen(intro[i]), i == 0 ? 12 : TC_DEFAULT,
                        TC_DEFAULT, i == 0 ? TA_BOLD : 0);
                row++;
            }
        for (sec = 0; sec < 2; sec++) {
            const int *order = sec ? eorder : vorder;
            const char *head = sec ? "Editing" : "Reading (the view)";
            line++;
            if (line >= top && row < tui_rows - 1) row++;
            if (line >= top && row < tui_rows - 1) {
                tui_put(1, row, head, (int)strlen(head), 14, TC_DEFAULT, TA_BOLD);
                row++;
            }
            line++;
            for (i = 0; order[i] >= 0; i++, line++) {
                char names[64];
                if (line < top || row >= tui_rows - 1) continue;
                if (!order[i]) {
                    row++;
                    continue;
                }
                keysfor(order[i], !sec, names);
                tui_put(2, row, names, (int)strlen(names), 11, TC_DEFAULT, 0);
                tui_put(24, row, actdesc[order[i]], (int)strlen(actdesc[order[i]]), TC_DEFAULT,
                        TC_DEFAULT, 0);
                row++;
            }
        }
        total = line;
        tui_fill(0, tui_rows - 1, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, TA_REV);
        tui_put(1, tui_rows - 1, "Help - arrows scroll, any other key goes back", 45, TC_DEFAULT,
                TC_DEFAULT, TA_REV);
        tui_cursor(0, 0, 0);
        tui_flush();
        if (!tui_wait(&e, -1, 0, NULL)) continue;
        if (e.type == TE_RESIZE) {
            tui_resized();
            view_reset();
            continue;
        }
        if (e.type == TE_MOUSE) {
            if (e.mact == TB_WHEELDOWN && top < total - 3) top += 3;
            if (e.mact == TB_WHEELUP) top = top > 3 ? top - 3 : 0;
            continue;
        }
        if (e.type != TE_KEY) continue;
        r = K_BASE(e.key);
        if (r == K_DOWN && top < total - 1) top++;
        else if (r == K_UP && top > 0) top--;
        else if (r == K_PGDN || r == ' ') top = top + tui_rows - 2 < total ? top + tui_rows - 2 : top;
        else if (r == K_PGUP) top = top > tui_rows - 2 ? top - (tui_rows - 2) : 0;
        else if (r != K_DOWN && r != K_UP) break;
    }
    view_forget();
}

/* ---- the command line ----------------------------------------------------- */

static int onoff(const char *v, int *val)
{
    if (!strcmp(v, "on") || !strcmp(v, "yes") || !strcmp(v, "1")) *val = 1;
    else if (!strcmp(v, "off") || !strcmp(v, "no") || !strcmp(v, "0")) *val = 0;
    else return 0;
    return 1;
}

static void command(void)
{
    static char cmd[PATHMAX + 20];
    char *w, *a;
    int i;
    cmd[0] = 0;
    if (!prompt("Command:", cmd, sizeof(cmd), P_CMD) || !cmd[0]) return;
    for (w = cmd; *w == ' '; w++) ;
    for (a = w; *a && *a != ' '; a++) ;
    if (*a) *a++ = 0;
    while (*a == ' ') a++;
    for (i = 0; w[i]; i++) w[i] = (char)(w[i] >= 'A' && w[i] <= 'Z' ? w[i] + 32 : w[i]);
    if (!strcmp(w, "save") || !strcmp(w, "w")) {
        if (*a) {
            strncpy(B->path, a, PATHMAX - 1);
            dosave(B, 0);
        } else
            dosave(B, 0);
    } else if (!strcmp(w, "open") || !strcmp(w, "o") || !strcmp(w, "e")) {
        if (*a) addtab(a);
        else doopen();
    } else if (!strcmp(w, "goto") || !strcmp(w, "line")) {
        long ln = atol(a);
        if (ln >= 1) {
            B->cur.line = ln > B->n ? B->n - 1 : ln - 1;
            B->cur.col = 0;
            setwant(B);
            if (B->view) view_fromfile(B);
        }
    } else if (!strcmp(w, "lang")) {
        for (i = 0; i < L_COUNT; i++)
            if (!nocase(a, langnames[i], -1) ||
                (i == L_ASM && !nocase(a, "asm", -1)) ||
                (i == L_E && !nocase(a, "e", -1)) ||
                (i == L_SCRIPT && !nocase(a, "script", -1)) ||
                (i == L_BLITZ && (!nocase(a, "bb", -1) || !nocase(a, "amiblitz", -1))) ||
                (i == L_INSTALL && !nocase(a, "install", -1)) ||
                (i == L_GUIDE && !nocase(a, "guide", -1)) ||
                (i == L_REXX && !nocase(a, "rexx", -1)) ||
                (i == L_PY && !nocase(a, "py", -1)) ||
                (i == L_MD && !nocase(a, "md", -1)) || (i == L_SH && !nocase(a, "sh", -1)) ||
                (i == L_MAKE && !nocase(a, "make", -1)) || (i == L_HTML && (!nocase(a, "html", -1) ||
                !nocase(a, "xml", -1))) || (i == L_NONE && !nocase(a, "none", -1))) {
                B->lang = i;
                B->hsok = 0;
                return;
            }
        say("languages: text C asm rexx e guide script py md sh make json html lua", 1);
    } else if (!strcmp(w, "quit") || !strcmp(w, "q")) {
        action(A_QUIT);
    } else if (!strcmp(w, "insert")) {
        if (*a) {
            /* (the question's own way, with the name given) */
            strncpy(insertname, a, PATHMAX - 1);
        }
        doinsert(B);
    } else if (!strcmp(w, "count")) {
        Pos p0 = { 0, 0 }, z;
        long n, i, words = 0, chars = 0, lines = 1;
        char *t, m[120], d[16];
        int inw = 0;
        if (B->sel) selrange(B, &p0, &z);
        else {
            z.line = B->n - 1;
            z.col = B->l[z.line].len;
        }
        if (!(t = buf_text(B, p0, z, &n))) return;
        for (i = 0; i < n; i++) {
            unsigned char c = (unsigned char)t[i];
            if (B->enc != ENC_UTF8 || (c & 0xC0) != 0x80) chars++;
            if (c == '\n') lines++;
            if (c == ' ' || c == '\t' || c == '\n') inw = 0;
            else if (!inw) inw = 1, words++;
        }
        FreeVec(t);
        m[0] = 0;
        strcat(m, B->sel ? "the selection: " : "the file: ");
#define NUM(v) do { long vv = (v); int k = 15; d[15] = 0; do d[--k] = (char)('0' + vv % 10); while ((vv /= 10) && k); strcat(m, d + k); } while (0)
        NUM(lines); strcat(m, " lines, ");
        NUM(words); strcat(m, " words, ");
        NUM(chars); strcat(m, " characters");
#undef NUM
        say(m, 0);
    } else if (!strcmp(w, "run")) {
        BPTR in, out;
        LONG rc;
        if (!*a) {
            say("run COMMAND: an AmigaDOS command, here in the terminal", 1);
            return;
        }
        tui_suspend();
        in = Open((STRPTR)"*", MODE_OLDFILE);
        out = Open((STRPTR)"*", MODE_NEWFILE);
        rc = SystemTags((STRPTR)a, SYS_Input, in, SYS_Output, out, NP_StackSize, 32768, TAG_DONE);
        if (in) Close(in);
        if (out) Close(out);
        tui_keyafter();
        tui_resume();
        if (rc == -1) say("could not run it", 1);
        else if (rc) {
            char m[40], d[12];
            int k = 11;
            long v = rc;
            d[11] = 0;
            do d[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
            strcpy(m, "it returned ");
            strcat(m, d + k);
            say(m, 1);
        }
    } else if (!strcmp(w, "set")) {
        char *v = a;
        while (*v && *v != ' ') v++;
        if (*v) *v++ = 0;
        while (*v == ' ') v++;
        if (!strcmp(a, "tabsize") && atoi(v) >= 1 && atoi(v) <= 16) tabsize = atoi(v);
        else if (!strcmp(a, "tabs") && (!strcmp(v, "spaces") || !strcmp(v, "tabs")))
            tabspaces = !strcmp(v, "spaces");
        else if (!strcmp(a, "numbers") && onoff(v, &numbers)) ;
        else if (!strcmp(a, "autoindent") && onoff(v, &autoindent)) ;
        else if (!strcmp(a, "wrap") && onoff(v, &softwrap)) B->topseg = 0;
        else if (!strcmp(a, "autoclose") && onoff(v, &autoclose)) ;
        else if (!strcmp(a, "whitespace") && onoff(v, &showws)) ;
        else if (!strcmp(a, "regex") && onoff(v, &regex)) ;
        else if (!strcmp(a, "backup") && onoff(v, &backups)) ;
        else if (!strcmp(a, "wrapcol") && atoi(v) >= 10 && atoi(v) <= 400) wrapcol = atoi(v);
        else if (!strcmp(a, "width") && (atoi(v) == 0 || (atoi(v) >= 20 && atoi(v) <= 400)) &&
                 (*v >= '0' && *v <= '9')) mdwidth = atoi(v);
        else if (!strcmp(a, "pictures") && onoff(v, &pictures)) view_forget();
        else if (!strcmp(a, "case") && (!strcmp(v, "smart") || !strcmp(v, "exact") || !strcmp(v, "any")))
            casemode = !strcmp(v, "smart") ? 0 : !strcmp(v, "exact") ? 1 : 2;
        else if (!strcmp(a, "encoding")) {
            int to = !nocase(v, "utf8", -1) || !nocase(v, "utf-8", -1) ? ENC_UTF8 :
                     !nocase(v, "latin1", -1) || !nocase(v, "latin-1", -1) ? ENC_LATIN1 : -1;
            if (to < 0) {
                say("set encoding utf8 or latin1", 1);
                return;
            }
            if (to != B->enc) {
                /* the whole text converted, as one undo step */
                Pos a0 = { 0, 0 }, z;
                long n, cn;
                char *t, *c;
                z.line = B->n - 1;
                z.col = B->l[z.line].len;
                if ((t = buf_text(B, a0, z, &n)) != NULL) {
                    if ((c = convert_text(t, n, B->enc, to, &cn)) != NULL) {
                        buf_group(B);
                        buf_delete(B, a0, z);
                        B->enc = to;
                        buf_insert(B, a0, c, cn);
                        buf_group(B);
                        FreeVec(c);
                    }
                    FreeVec(t);
                }
                B->cur.line = B->cur.col = 0;
                B->sel = 0;
            }
            return;
        } else if (!strcmp(a, "lineends")) {
            if (!strcmp(v, "crlf")) B->crlf = 1;
            else if (!strcmp(v, "lf")) B->crlf = 0;
            else say("set lineends lf or crlf", 1);
            return;
        } else {
            say("set width N, pictures on|off, tabsize N, tabs spaces|tabs, numbers|autoindent|wrap|autoclose|whitespace|regex|backup on|off, wrapcol N, case smart|exact|any, encoding utf8|latin1, lineends lf|crlf", 1);
            return;
        }
        prefs_save();
        say("set (and kept)", 0);
    } else if (!strcmp(w, "table")) {
        md_table(B);
    } else if (!strcmp(w, "toc")) {
        md_toc(B);
    } else
        say("commands: save [NAME], open [NAME], insert [NAME], goto N[:C], run CMD, count, table, toc, set ..., lang NAME, quit", 1);
}

/* ---- the mouse ------------------------------------------------------------- */

static int mousepos(Buf *b, int col, int row, Pos *p)
{
    long ln = b->top + row - 1, x;
    if (softwrap && row >= 1) {
        /* walk the screen rows down from the view's top */
        long seg = b->topseg, r = 1, w = textcols(b);
        ln = b->top;
        while (r < row && ln < b->n) {
            if (seg + 1 < nseg(b, ln)) seg++;
            else {
                if (ln + 1 >= b->n) break;
                ln++;
                seg = 0;
            }
            r++;
        }
        x = col - gutter(b);
        if (x < 0) x = 0;
        if (x >= w) x = w - 1;
        p->line = ln;
        p->col = buf_colat(b, ln, seg * w + x);
        return 1;
    }
    if (row < 1) ln = b->top - 1;
    if (ln < 0) ln = 0;
    if (ln >= b->n) ln = b->n - 1;
    x = col - gutter(b) + b->left;
    if (x < 0) x = 0;
    p->line = ln;
    p->col = buf_colat(b, ln, x);
    return 1;
}

static void mouse(TuiEv *e)
{
    static ULONG ls, lm;
    static int lc = -1, lr = -1, dragging, clicks;
    Buf *b = B;
    Pos p;
    if (e->row >= 1 && e->row <= textrows() && (b->view || (ew < tui_cols && e->col > ew)) &&
        !dragging) {
        if (b->view) view_mouse(e, 0, tui_cols, 1, textrows());
        else view_mouse(e, ew + 1, tui_cols - ew - 1, 1, textrows());
        return;
    }
    if (e->mact == TB_WHEELUP || e->mact == TB_WHEELDOWN) {
        if (softwrap) {
            int k;
            for (k = 0; k < 3; k++) vstep(b, e->mact == TB_WHEELUP ? -1 : 1);
        } else {
            b->top += e->mact == TB_WHEELUP ? -3 : 3;
            if (b->top > b->n - 1) b->top = b->n - 1;
            if (b->top < 0) b->top = 0;
        }
        follow = 0;
        return;
    }
    if (e->mact == TB_PRESS && e->row == 0) {   /* the tab bar: which tab */
        int x = 9, i, from = 0;
        for (i = from; i < ntabs; i++) {
            const char *n = tabs[i]->path[0] ? basename_(tabs[i]->path) : "new";
            int w = (int)strlen(n) + 3;
            if (e->col >= x && e->col < x + w) {
                ct = i;
                return;
            }
            x += w;
        }
        return;
    }
    if (e->row >= tui_rows - 2 && !dragging) return;
    follow = 1;
    mousepos(b, e->col, e->row, &p);
    if (e->mact == TB_PRESS) {
        buf_group(b);
        if (e->mods & TM_SHIFT) {
            if (!b->sel) {
                b->anchor = b->cur;
                b->sel = 1;
            }
            b->cur = p;
        } else if (e->col == lc && e->row == lr && clicks == 2 && DoubleClick(ls, lm, e->secs, e->mics)) {
            /* a third click: the whole line */
            selectlines(b, p.line, p.line);
            if (p.line < b->n - 1) {
                b->cur.line = p.line + 1;
                b->cur.col = 0;
            }
            clicks = 0;
            lc = -1;
            return;
        } else if (e->col == lc && e->row == lr && DoubleClick(ls, lm, e->secs, e->mics)) {
            /* a double click: the word */
            int n;
            clicks = 2;
            ls = e->secs;
            lm = e->mics;
            b->cur = p;
            while (b->cur.col > 0) {
                long q = buf_prev(b, p.line, b->cur.col);
                if (!isword(buf_char(b, p.line, q, &n))) break;
                b->cur.col = q;
            }
            b->anchor = b->cur;
            b->cur = p;
            while (b->cur.col < b->l[p.line].len && isword(buf_char(b, p.line, b->cur.col, &n)))
                b->cur.col += n;
            b->sel = pos_cmp(b->anchor, b->cur) != 0;
            setwant(b);
            return;
        } else {
            b->cur = p;
            b->anchor = p;
            b->sel = 0;
            clicks = 1;
        }
        lc = e->col;
        lr = e->row;
        ls = e->secs;
        lm = e->mics;
        dragging = 1;
        setwant(b);
    } else if (e->mact == TB_DRAG && dragging) {
        /* past the text's top or bottom: the view goes along */
        if (e->row < 1) {
            if (softwrap) vstep(b, -1);
            else if (b->top > 0) b->top--;
            mousepos(b, e->col, 1, &p);
        } else if (e->row > textrows()) {
            if (softwrap) vstep(b, 1);
            else if (b->top < b->n - 1) b->top++;
            mousepos(b, e->col, textrows(), &p);
        }
        b->cur = p;
        b->sel = pos_cmp(b->anchor, b->cur) != 0;
        setwant(b);
    } else if (e->mact == TB_RELEASE) {
        dragging = 0;
    }
}

/* ---- the bigger edits ------------------------------------------------------------ */

/* the lines a command works on: the selection's (a line only reached at
 * its start is not in), or the cursor's */
static void linerange(Buf *b, long *a, long *z)
{
    Pos p, q;
    if (!b->sel) {
        *a = *z = b->cur.line;
        return;
    }
    selrange(b, &p, &q);
    *a = p.line;
    *z = q.line;
    if (q.col == 0 && q.line > p.line) (*z)--;
}

static void selectlines(Buf *b, long a, long z)
{
    b->anchor.line = a;
    b->anchor.col = 0;
    b->cur.line = z;
    b->cur.col = b->l[z].len;
    b->sel = 1;
    setwant(b);
}

static const char *cprefix(int lang, const char **suf)
{
    *suf = NULL;
    switch (lang) {
    case L_C: return "//";
    case L_ASM: case L_SCRIPT: case L_BLITZ: case L_INSTALL: return ";";
    case L_REXX: *suf = "*/"; return "/*";
    case L_E: return "->";
    case L_PY: case L_SH: case L_MAKE: return "#";
    case L_LUA: return "--";
    case L_HTML: case L_MD: *suf = "-->"; return "<!--";
    }
    return NULL;
}

static void docomment(Buf *b)
{
    const char *suf, *pre = cprefix(b->lang, &suf);
    long a, z, ln, minind = 1L << 30;
    int all = 1, any = 0, pl, sl;
    if (!pre) {
        say("no comments in this kind of file (Ctrl+U: lang NAME)", 1);
        return;
    }
    pl = (int)strlen(pre);
    sl = suf ? (int)strlen(suf) : 0;
    linerange(b, &a, &z);
    for (ln = a; ln <= z; ln++) {
        Line *l = &b->l[ln];
        long i = 0;
        while (i < l->len && (l->s[i] == ' ' || l->s[i] == '\t')) i++;
        if (i == l->len) continue;              /* a blank line: not counted */
        any = 1;
        if (i < minind) minind = i;
        if (l->len - i < pl || memcmp(l->s + i, pre, pl)) all = 0;
    }
    if (!any) return;
    buf_group(b);
    for (ln = a; ln <= z; ln++) {
        Line *l = &b->l[ln];
        long i = 0;
        Pos p, q;
        while (i < l->len && (l->s[i] == ' ' || l->s[i] == '\t')) i++;
        if (i == l->len) continue;
        if (all) {                              /* off: the mark (and one space) goes */
            if (sl) {
                long e = l->len;
                while (e > 0 && (l->s[e - 1] == ' ' || l->s[e - 1] == '\t')) e--;
                if (e - sl >= i + pl && !memcmp(l->s + e - sl, suf, sl)) {
                    p.line = q.line = ln;
                    p.col = e - sl;
                    if (p.col > i + pl && l->s[p.col - 1] == ' ') p.col--;
                    q.col = e;
                    buf_delete(b, p, q);
                }
            }
            p.line = q.line = ln;
            p.col = i;
            q.col = i + pl;
            if (q.col < b->l[ln].len && b->l[ln].s[q.col] == ' ') q.col++;
            buf_delete(b, p, q);
        } else {                                /* on: at the shallowest indent */
            char t[16];
            p.line = ln;
            p.col = minind;
            strcpy(t, pre);
            strcat(t, " ");
            buf_insert(b, p, t, pl + 1);
            if (sl) {
                p.col = b->l[ln].len;
                strcpy(t, " ");
                strcat(t, suf);
                buf_insert(b, p, t, sl + 1);
            }
        }
    }
    buf_group(b);
    if (b->sel || a != z) selectlines(b, a, z);
    else {
        if (b->cur.col > b->l[b->cur.line].len) b->cur.col = b->l[b->cur.line].len;
        setwant(b);
    }
    say(all ? "comment off" : "comment on", 0);
}

static void domove(Buf *b, int d)
{
    long a, z, n;
    char *t;
    Pos p, q;
    int hadsel = b->sel;
    long cl = b->cur.line, ccol = b->cur.col, al = b->anchor.line, acol = b->anchor.col;
    linerange(b, &a, &z);
    if ((d < 0 && a == 0) || (d > 0 && z >= b->n - 1)) return;
    buf_group(b);
    if (d < 0) {                                /* the line above goes below the block */
        p.line = a - 1;
        p.col = 0;
        q.line = a - 1;
        q.col = b->l[a - 1].len;
        if (!(t = buf_text(b, p, q, &n))) return;
        q.line = a;
        q.col = 0;
        buf_delete(b, p, q);                    /* the block is now a-1 .. z-1 */
        p.line = z - 1;
        p.col = b->l[z - 1].len;
        {
            char *u = AllocVec(n + 2, MEMF_ANY);
            if (u) {
                u[0] = '\n';
                memcpy(u + 1, t, n);
                buf_insert(b, p, u, n + 1);
                FreeVec(u);
            }
        }
    } else {                                    /* the line below goes above it */
        p.line = z + 1;
        p.col = 0;
        q.line = z + 1;
        q.col = b->l[z + 1].len;
        if (!(t = buf_text(b, p, q, &n))) return;
        p.line = z;
        p.col = b->l[z].len;
        buf_delete(b, p, q);
        p.line = a;
        p.col = 0;
        {
            char *u = AllocVec(n + 2, MEMF_ANY);
            if (u) {
                memcpy(u, t, n);
                u[n] = '\n';
                buf_insert(b, p, u, n + 1);
                FreeVec(u);
            }
        }
    }
    FreeVec(t);
    buf_group(b);
    b->cur.line = cl + d;
    b->cur.col = ccol;
    if (hadsel) {
        b->anchor.line = al + d;
        b->anchor.col = acol;
        b->sel = 1;
    }
    if (b->cur.col > b->l[b->cur.line].len) b->cur.col = b->l[b->cur.line].len;
    setwant(b);
}

/* nano's Justify: the paragraph (or the selected lines) made into lines
 * no wider than wrapcol, the first line's indent kept */
static void dojustify(Buf *b)
{
    long a, z, i, total = 0, ln, ind = 0, col;
    char *out, *o;
    Pos p, q;
    if (b->sel) linerange(b, &a, &z);
    else {
        Line *l = &b->l[b->cur.line];
        for (i = 0; i < l->len && (l->s[i] == ' ' || l->s[i] == '\t'); i++) ;
        if (i == l->len) {
            say("not in a paragraph", 1);
            return;
        }
        a = z = b->cur.line;
        while (a > 0) {
            Line *m = &b->l[a - 1];
            for (i = 0; i < m->len && (m->s[i] == ' ' || m->s[i] == '\t'); i++) ;
            if (i == m->len) break;
            a--;
        }
        while (z < b->n - 1) {
            Line *m = &b->l[z + 1];
            for (i = 0; i < m->len && (m->s[i] == ' ' || m->s[i] == '\t'); i++) ;
            if (i == m->len) break;
            z++;
        }
    }
    for (ln = a; ln <= z; ln++) total += b->l[ln].len + 1;
    while (ind < b->l[a].len && (b->l[a].s[ind] == ' ' || b->l[a].s[ind] == '\t')) ind++;
    if (!(out = AllocVec(total * 2 + ind * (total / 2 + 2) + 16, MEMF_ANY))) return;
    o = out;
    memcpy(o, b->l[a].s, ind);
    o += ind;
    col = buf_xof(b, a, ind);
    {
        int firstword = 1;
        for (ln = a; ln <= z; ln++) {
            Line *l = &b->l[ln];
            i = 0;
            while (i < l->len) {
                long s0, wl = 0, k;
                while (i < l->len && (l->s[i] == ' ' || l->s[i] == '\t')) i++;
                if (i >= l->len) break;
                s0 = i;
                while (i < l->len && l->s[i] != ' ' && l->s[i] != '\t') i++;
                for (k = s0; k < i; k++)        /* characters, not bytes */
                    if (b->enc != ENC_UTF8 || (l->s[k] & 0xC0) != 0x80) wl++;
                if (!firstword) {
                    if (col + 1 + wl > wrapcol) {
                        *o++ = '\n';
                        memcpy(o, b->l[a].s, ind);
                        o += ind;
                        col = buf_xof(b, a, ind);
                    } else {
                        *o++ = ' ';
                        col++;
                    }
                }
                memcpy(o, l->s + s0, i - s0);
                o += i - s0;
                col += wl;
                firstword = 0;
            }
        }
    }
    buf_group(b);
    p.line = a;
    p.col = 0;
    q.line = z;
    q.col = b->l[z].len;
    buf_delete(b, p, q);
    b->cur = buf_insert(b, p, out, o - out);
    FreeVec(out);
    b->sel = 0;
    setwant(b);
    buf_group(b);
    say("paragraph reflowed (Ctrl+Z undoes)", 0);
}

static void doinsert(Buf *b)
{
    char *path = insertname;
    BPTR fh;
    long n, cn, i, o;
    char *t, *c;
    if (!prompt("Insert file:", path, PATHMAX, P_FILE) || !path[0]) return;
    if (!(fh = Open((STRPTR)path, MODE_OLDFILE))) {
        say("cannot open it", 1);
        return;
    }
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    if (n < 0 || !(t = AllocVec(n + 1, MEMF_ANY))) {
        Close(fh);
        say("not enough memory for it", 1);
        return;
    }
    n = Read(fh, t, n);
    Close(fh);
    for (i = 0, o = 0; i < n; i++)              /* CR LF as LF */
        if (!(t[i] == '\r' && i + 1 < n && t[i + 1] == '\n')) t[o++] = t[i];
    n = o;
    {
        int u = 0;
        for (i = 0; i + 1 < n; i++)
            if ((unsigned char)t[i] >= 0xC2 && (unsigned char)t[i] <= 0xF4 && (t[i + 1] & 0xC0) == 0x80) {
                u = 1;
                break;
            }
        c = convert_text(t, n, u ? ENC_UTF8 : ENC_LATIN1, b->enc, &cn);
    }
    FreeVec(t);
    if (!c) return;
    buf_group(b);
    delsel(b);
    instext(b, c, cn);
    buf_group(b);
    FreeVec(c);
    say("inserted", 0);
}

/* the keys recorded and played back (micro's macros) */
#define MAXMACRO 1024
static int macro[MAXMACRO], nmacro, recording, playing;

static void key(int k);

static void domacro(int play)
{
    char m[60];
    if (!play) {
        if (recording) {
            recording = 0;
            strcpy(m, "recorded: ");
            {
                char d[12];
                int k = 11, v = nmacro;
                d[11] = 0;
                do d[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
                strcat(m, d + k);
            }
            strcat(m, " keys");
            say(m, 0);
        } else {
            recording = 1;
            nmacro = 0;
            say("recording keys (the record key again stops)", 0);
        }
        return;
    }
    if (recording) recording = 0;
    if (!nmacro) {
        say("no keys recorded", 1);
        return;
    }
    {
        int i;
        playing = 1;
        buf_group(B);
        for (i = 0; i < nmacro; i++) key(macro[i]);
        playing = 0;
    }
}

/* ---- one key ------------------------------------------------------------------ */

static void action(int act)
{
    Buf *b = B;
    int sel = (act >= A_SELUP && act <= A_SELFILEEND);
    follow = 1;
    if ((act >= A_UP && act <= A_FILEEND) || sel) {
        if (sel) {
            if (!b->sel) {
                b->anchor = b->cur;
                b->sel = 1;
            }
        } else if (b->sel && (act == A_LEFT || act == A_RIGHT)) {
            /* a move without Shift ends the selection at its edge */
            Pos a, z;
            selrange(b, &a, &z);
            b->cur = act == A_LEFT ? a : z;
            b->sel = 0;
            setwant(b);
            b->typing = 0;
            return;
        } else
            b->sel = 0;
        move(act);
        if (sel && pos_cmp(b->anchor, b->cur) == 0) b->sel = 0;
        b->typing = 0;
        return;
    }
    switch (act) {
    case A_SELECTALL:
        b->anchor.line = b->anchor.col = 0;
        b->cur.line = b->n - 1;
        b->cur.col = b->l[b->n - 1].len;
        b->sel = 1;
        break;
    case A_ENTER: if (b->lang != L_MD || !md_enter(b)) donewline(b); break;
    case A_BACKSPACE: dobackspace(b, 0); break;
    case A_DELETE: dodelete(b, 0); break;
    case A_DELWORDLEFT: dobackspace(b, 1); break;
    case A_DELWORDRIGHT: dodelete(b, 1); break;
    case A_TAB: dotab(b, 0); break;
    case A_UNTAB: dotab(b, 1); break;
    case A_CUT: docopycut(b, 1, 0); break;
    case A_COPY: docopycut(b, 0, 0); break;
    case A_CUTLINE: docopycut(b, 1, 1); break;
    case A_PASTE: dopaste(b, NULL, 0, -1); break;
    case A_DUPLICATE: doduplicate(b); break;
    case A_UNDO: if (!buf_undo(b)) say("nothing to undo", 1); break;
    case A_REDO: if (!buf_redo(b)) say("nothing to redo", 1); break;
    case A_SAVE: dosave(b, 0); break;
    case A_SAVEAS: dosave(b, 1); break;
    case A_OPEN: doopen(); break;
    case A_NEWTAB: addtab(NULL); break;
    case A_CLOSE:
        if (keepornot(ct)) {
            closetab(ct);
            if (!ntabs) quitting = 1;
        }
        break;
    case A_NEXTTAB: if (ntabs > 1) ct = (ct + 1) % ntabs; break;
    case A_PREVTAB: if (ntabs > 1) ct = (ct + ntabs - 1) % ntabs; break;
    case A_QUIT: {
        int i;
        for (i = 0; i < ntabs; i++)
            if (!keepornot(i)) return;
        quitting = 1;
        break;
    }
    case A_FIND:
        if (b->sel && b->anchor.line == b->cur.line) {
            /* the selected word is what is sought */
            Pos a, z;
            long n;
            char *t;
            selrange(b, &a, &z);
            if ((t = buf_text(b, a, z, &n)) != NULL) {
                long cn;
                char *c = convert_text(t, n, b->enc, ENC_LATIN1, &cn);
                if (c) {
                    if (cn < (long)sizeof(findtext)) memcpy(findtext, c, cn + 1);
                    FreeVec(c);
                }
                FreeVec(t);
            }
        }
        if (prompt("Find:", findtext, sizeof(findtext), P_FIND) && findtext[0]) find(b, 1, 0);
        break;
    case A_FINDNEXT: if (findtext[0]) find(b, 1, 0); else action(A_FIND); break;
    case A_FINDPREV: if (findtext[0]) find(b, -1, 0); else action(A_FIND); break;
    case A_REPLACE: doreplace(b); break;
    case A_GOTO: dogoto(b); break;
    case A_HELP: help(); break;
    case A_COMMAND: command(); break;
    case A_NUMBERS: numbers = !numbers; prefs_save(); break;
    case A_ESCAPE: b->sel = 0; msg[0] = 0; showfound = 0; break;
    case A_MATCH:
        bracketmatch(b);
        if (bm_line2 >= 0) {
            b->cur.line = bm_line2;
            b->cur.col = bm_col2;
            b->sel = 0;
            setwant(b);
        } else
            say(bm_line >= 0 ? "no matching bracket" : "no bracket here", 1);
        break;
    case A_COMMENT: docomment(b); break;
    case A_JUSTIFY: dojustify(b); break;
    case A_SOFTWRAP:
        softwrap = !softwrap;
        b->topseg = 0;
        prefs_save();
        say(softwrap ? "soft wrap on" : "soft wrap off", 0);
        break;
    case A_WHITESPACE: showws = !showws; prefs_save(); break;
    case A_MOVEUP: domove(b, -1); break;
    case A_MOVEDOWN: domove(b, 1); break;
    case A_INSERT: doinsert(b); break;
    case A_MACROREC: domacro(0); break;
    case A_MACROPLAY: domacro(1); break;
    case A_VIEW:
        b->sel = 0;
        view_fromfile(b);
        b->view = 1;
        view_forget();
        break;
    case A_SPLIT:
        split = !split;
        prefs_save();
        view_forget();
        say(split ? "the view beside" : "the view put away", 0);
        break;
    case A_BOLD: md_bold(b); break;
    case A_FORMAT: md_format(b); break;
    case A_OUTLINE: outline(); break;
    }
}

static void key(int k)
{
    int act = keys_find(k, B->view);
    if (B->view) {
        if (act) msg[0] = 0;
        if (act && view_action(act)) return;
        switch (act) {    /* what the view shares with the editor */
        case A_SAVE: case A_OPEN: case A_NEWTAB: case A_CLOSE: case A_NEXTTAB: case A_PREVTAB:
        case A_QUIT: case A_HELP: case A_COMMAND: case A_SPLIT:
            action(act);
            return;
        case A_ESCAPE:
            B->frow = -1;
            return;
        }
        if (act) return;
        {
            char m[80], n[24];
            keyname(k, n, 0);
            strcpy(m, n);
            strcat(m, " does nothing here (ENVARC:Markdown/Markdown.keys [view]) - h: the keys");
            say(m, 1);
        }
        return;
    }
    if (recording && !playing && act != A_MACROREC && act != A_MACROPLAY && nmacro < MAXMACRO)
        macro[nmacro++] = k;
    if (act) {
        if (act != A_CUTLINE || lastact != A_CUTLINE) msg[0] = 0;
        action(act);
        lastact = act;
        return;
    }
    if (k >= 32 && k < 0x110000 && !(k & TM_MASK) && k != 0x7F) {
        msg[0] = 0;
        follow = 1;
        typechar(B, (ULONG)k);
        lastact = 0;
        return;
    }
    {   /* a key that does nothing: say which, so it can be bound */
        char m[80], n[24];
        keyname(k, n, 0);
        strcpy(m, n);
        strcat(m, " does nothing (ENVARC:Markdown/Markdown.keys)");
        say(m, 1);
    }
}

/* ---- the start ------------------------------------------------------------------ */

static int smain(int argc, char **argv)
{
    int i;
    long jump = 0;
    TuiEv e;
    if (!tui_possible()) {
        PutStr((STRPTR)"Markdown is a Markdown viewer and editor for the terminal: start it from a\n"
                       "Shell (in CTerm, or a console window). Usage: markdown [-e] [+LINE] [FILE ...]\n");
        return 10;
    }
    prefs_load();
    keys_load();
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-e")) startedit = 1;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-e")) continue;
        if (argv[i][0] == '+' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            jump = atol(argv[i] + 1);
            continue;
        }
        if (addtab(argv[i]) && jump > 0) {
            B->cur.line = jump > B->n ? B->n - 1 : jump - 1;
            if (B->view) view_fromfile(B);
            jump = 0;
        }
    }
    if (!ntabs && !addtab(NULL)) {
        PutStr((STRPTR)"Markdown: out of memory\n");
        return 20;
    }
    if (ntabs > 1) ct = 0;
    if (!tui_open()) {
        PutStr((STRPTR)"Markdown: cannot use this console\n");
        for (i = 0; i < ntabs; i++) buf_free(tabs[i]);
        return 20;
    }
    if (keysbad[0]) say(keysbad, 1);
    while (!quitting) {
        drawall();
        if (!tui_wait(&e, -1, 0, NULL)) continue;
        switch (e.type) {
        case TE_KEY: key(e.key); break;
        case TE_MOUSE: mouse(&e); break;
        case TE_PASTE: {
            /* a paste through the terminal: its characters as they came */
            int enc = ENC_LATIN1;
            long k;
            for (k = 0; k < e.len; k++)
                if (e.text[k] >= 0xC2 && e.text[k] <= 0xF4 && k + 1 < e.len &&
                    (e.text[k + 1] & 0xC0) == 0x80) {
                    enc = ENC_UTF8;
                    break;
                }
            if (!B->view) dopaste(B, (const char *)e.text, e.len, enc);
            break;
        }
        case TE_RESIZE: tui_resized(); view_reset(); break;
        }
    }
    tui_close();
    for (i = 0; i < ntabs; i++) places_note(tabs[i]);
    places_save();
    for (i = 0; i < ntabs; i++) {
        view_close(tabs[i]);
        buf_free(tabs[i]);
    }
    if (cutacc) FreeVec(cutacc);
    if (hlbuf) FreeVec(hlbuf);
    clip_exit();
    (void)version;
    return 0;
}

/* a stack of its own (libnix ignores __stack): 32 KB whatever the Shell gave */
#define STACKSZ 32768
static struct StackSwapStruct sss;
static char *bigstk;
static int sargc, sret;
static char **sargv;

static void __attribute__((noinline)) runswapped(void)
{
    sret = smain(sargc, sargv);
}

int main(int argc, char **argv)
{
    struct Task *me = FindTask(NULL);
    long have = (char *)me->tc_SPUpper - (char *)me->tc_SPLower;
    if (argc == 0) {                    /* from Workbench: no terminal */
        return 20;
    }
    if (have >= STACKSZ || !(bigstk = AllocVec(STACKSZ, MEMF_ANY)))
        return smain(argc, argv);
    sargc = argc;
    sargv = argv;
    sss.stk_Lower = bigstk;
    sss.stk_Upper = (ULONG)(bigstk + STACKSZ);
    sss.stk_Pointer = (APTR)(bigstk + STACKSZ);
    StackSwap(&sss);
    runswapped();
    StackSwap(&sss);
    FreeVec(bigstk);
    return sret;
}
