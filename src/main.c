/* Sumi - a text editor that lives inside the terminal. Simple but
 * complete, in the way of micro and nano: Ctrl+S saves, Ctrl+Q closes,
 * Ctrl+C/X/V copy and paste (the Amiga clipboard), Ctrl+Z undoes,
 * Shift+arrows select, the mouse places and selects; colours for code,
 * several files in tabs, Latin-1 and UTF-8 kept as they are. Every key
 * is the person's (keys.c).
 *
 * Usage: sumi [+LINE] [FILE ...]
 *
 * The screen, top to bottom: the tabs, the text (line numbers on the
 * left), the status line, and a line for messages, questions and the
 * keys' reminder. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <string.h>
#include <stdlib.h>
#include "sumi.h"

static const char version[] = "$VER: Sumi 0.1b1 (12.10.2026) - text editor";

#define MAXTABS 24
#define PREFS   "ENVARC:Sumi/Sumi.prefs"
#define PREFENV "ENV:Sumi/Sumi.prefs"

int tabsize = 8, tabspaces = 0, numbers = 1, autoindent = 1;

static Buf *tabs[MAXTABS];
static int ntabs, ct;            /* the tabs, the shown one */
#define B (tabs[ct])
static char msg[200];
static int msgerr;
static int follow = 1;           /* the view follows the cursor (not after the wheel) */
static int lastact;              /* the action before (cutline appends) */
static char findtext[128], repltext[128];
static int quitting;
static UBYTE *hlbuf;             /* a line's colours */
static long hlcap;

/* the colours: the ANSI sixteen, so CTerm's theme decides */
static const UBYTE hlcol[] = { TC_DEFAULT, 8, 2, 12, 6, 3, 5, 11 };
#define C_DIM    8
#define C_BAR    7

char *convert_text(const char *t, long n, int from, int to, long *out);

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
    }
    Close(fh);
}

static void prefs_write(const char *dir, const char *path)
{
    BPTR l = Lock((STRPTR)dir, SHARED_LOCK), fh;
    char t[120];
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
    strcat(t, autoindent ? "\nautoindent=1\n" : "\nautoindent=0\n");
    Write(fh, t, (LONG)strlen(t));
    Close(fh);
}

void prefs_save(void)
{
    prefs_write("ENVARC:Sumi", PREFS);
    prefs_write("ENV:Sumi", PREFENV);
}

void say(const char *m, int err)
{
    strncpy(msg, m, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = 0;
    msgerr = err;
}

/* ---- the layout --------------------------------------------------------- */

static int textrows(void)
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
    int w = tui_cols - gutter(b);
    return w > 1 ? w : 1;
}

/* the view so the cursor shows */
static void scrollto(Buf *b)
{
    long x = buf_xof(b, b->cur.line, b->cur.col);
    int h = textrows(), w = textcols(b);
    if (b->cur.line < b->top) b->top = b->cur.line;
    if (b->cur.line >= b->top + h) b->top = b->cur.line - h + 1;
    if (x < b->left) b->left = x > 4 ? x - 4 : 0;
    if (x >= b->left + w) b->left = x - w + 8;
    if (b->top < 0) b->top = 0;
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

static void drawline(Buf *b, long ln, int row)
{
    Line *l = &b->l[ln];
    int g = gutter(b), w = textcols(b), sx;
    long x = 0, i = 0;
    if (g) {
        char num[12];
        long v = ln + 1;
        int k = 11;
        num[11] = 0;
        do num[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
        tui_fill(0, row, g, ' ', C_DIM, TC_DEFAULT, 0);
        tui_put(g - 1 - (11 - k), row, num + k, 11 - k,
                ln == b->cur.line ? TC_DEFAULT : C_DIM, TC_DEFAULT, 0);
    }
    tui_fill(g, row, w, ' ', TC_DEFAULT, TC_DEFAULT, 0);
    if (hlcap < l->len + 1) {
        if (hlbuf) FreeVec(hlbuf);
        hlcap = l->len + 256;
        if (!(hlbuf = AllocVec(hlcap, MEMF_ANY))) hlcap = 0;
    }
    if (hlbuf) hs_upto(b, ln);
    if (hlcap < l->len + 1) {               /* (hs_upto may have made it smaller) */
        if (hlbuf) FreeVec(hlbuf);
        hlcap = l->len + 256;
        if (!(hlbuf = AllocVec(hlcap, MEMF_ANY))) hlcap = 0;
    }
    if (hlbuf) syn_line(b->lang, l->s, l->len, l->hs, hlbuf);
    while (i < l->len && x < b->left + w) {
        int n, attr, fg;
        ULONG c = buf_char(b, ln, i, &n);
        long nx = c == '\t' ? (x / tabsize + 1) * tabsize : x + 1;
        attr = insel(b, ln, i) ? TA_REV : 0;
        fg = hlbuf ? hlcol[hlbuf[i]] : TC_DEFAULT;
        for (; x < nx; x++) {
            sx = (int)(x - b->left);
            if (sx >= 0 && sx < w) tui_putc(g + sx, row, c == '\t' ? ' ' : c, fg, TC_DEFAULT, attr);
        }
        i += n;
    }
    /* a selected line end shows as one cell */
    if (b->sel && ln < b->n - 1 && insel(b, ln, l->len)) {
        sx = (int)(x - b->left);
        if (sx >= 0 && sx < w) tui_putc(g + sx, row, ' ', TC_DEFAULT, TC_DEFAULT, TA_REV);
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
        if (w <= tui_cols - 6 || from >= ct) break;
        from++;
    }
    tui_put(x, 0, "Sumi", 4, 4, TC_DEFAULT, TA_BOLD);
    x += 5;
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
    static const int acts[] = { A_SAVE, A_CLOSE, A_FIND, A_REPLACE, A_GOTO, A_UNDO,
                                A_CUTLINE, A_COMMAND, A_HELP };
    static const char *const words[] = { "Save", "Close", "Find", "Replace", "Line", "Undo",
                                         "Cut line", "Command", "Help" };
    int x = 0, i;
    char k[24];
    tui_fill(0, tui_rows - 1, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
    for (i = 0; i < (int)(sizeof(acts) / sizeof(acts[0])); i++) {
        int kl, wl;
        if (!keys_first(acts[i], k)) continue;
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
    s[0] = ' ';
    s[1] = 0;
    strncat(s, name, 100);
    if (buf_dirty(b)) strcat(s, " [changed]");
    l = (int)strlen(s);
    tui_put(0, row, s, l < tui_cols ? l : tui_cols, TC_DEFAULT, TC_DEFAULT, TA_REV | TA_BOLD);
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
    if (l < tui_cols) tui_put(tui_cols - l, row, s, l, TC_DEFAULT, TC_DEFAULT, TA_REV);
}

static void drawall(void)
{
    Buf *b = B;
    int r, h = textrows();
    if (follow) scrollto(b);
    drawtabs();
    for (r = 0; r < h; r++) {
        long ln = b->top + r;
        if (ln < b->n) drawline(b, ln, r + 1);
        else {
            tui_fill(0, r + 1, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
            if (gutter(b)) tui_putc(gutter(b) - 2, r + 1, '~', C_DIM, TC_DEFAULT, 0);
        }
    }
    drawstatus();
    if (msg[0]) {
        int l = (int)strlen(msg);
        tui_fill(0, tui_rows - 1, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
        tui_put(0, tui_rows - 1, msg, l < tui_cols ? l : tui_cols, msgerr ? 9 : 10, TC_DEFAULT, 0);
    } else
        hints();
    {
        long x = buf_xof(b, b->cur.line, b->cur.col) - b->left;
        long y = b->cur.line - b->top;
        int on = x >= 0 && x < textcols(b) && y >= 0 && y < h;
        tui_cursor(gutter(b) + (int)x, (int)y + 1, on);
    }
    tui_flush();
}

/* ---- a question on the bottom line -------------------------------------- */

static int filecomplete(char *buf, int max, int *len);

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
static int prompt(const char *q, char *buf, int max, int file)
{
    int len = (int)strlen(buf), pos = len, ql = (int)strlen(q);
    int fresh = len > 0;            /* a suggestion: Return takes it, typing replaces it */
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
        case K_ENTER: msg[0] = 0; return 1;
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
                filecomplete(buf, max, &len);
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
static int ask(const char *q, const char *choices)
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
static int filecomplete(char *buf, int max, int *len)
{
    char dir[PATHMAX], part[108], best[108];
    const char *base = basename_(buf);
    BPTR lock;
    struct FileInfoBlock *fib;
    int pl, n = 0, bl = 0, isdir = 0;
    strncpy(dir, buf, base - buf);
    dir[base - buf] = 0;
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

static void mv_vert(Buf *b, long d)
{
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
        b->top -= textrows() - 1;
        if (b->top < 0) b->top = 0;
        mv_vert(b, -(textrows() - 1));
        break;
    case A_PAGEDOWN: case A_SELPAGEDOWN:
        b->top += textrows() - 1;
        if (b->top > b->n - 1) b->top = b->n - 1;
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

static void instext(Buf *b, const char *t, long n)
{
    b->cur = buf_insert(b, b->cur, t, n);
    setwant(b);
}

/* a character typed: in the file's own encoding */
static void typechar(Buf *b, ULONG c)
{
    char u[4];
    int n;
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
        if (!(c = convert_text(t, n, from, b->enc, &cn))) return;
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

/* smart case: a capital in what is sought makes it exact */
static int matchat(const char *s, long len, long i, const char *f, long fl, int exact)
{
    long k;
    if (i + fl > len) return 0;
    for (k = 0; k < fl; k++) {
        int a = (unsigned char)s[i + k], c = (unsigned char)f[k];
        if (exact ? a != c : ci(a) != ci(c)) return 0;
    }
    return 1;
}

static int find(Buf *b, int dir, int quiet)
{
    long fl = (long)strlen(findtext), ln, col, k;
    int exact = 0, wrapped = 0;
    char ftext[128];
    long ftl;
    if (!fl) return 0;
    {   /* what is sought, in the file's encoding */
        char *c = convert_text(findtext, fl, ENC_LATIN1, b->enc, &ftl);
        if (!c) return 0;
        if (ftl >= (long)sizeof(ftext)) ftl = sizeof(ftext) - 1;
        memcpy(ftext, c, ftl);
        ftext[ftl] = 0;
        FreeVec(c);
    }
    for (k = 0; k < fl; k++)
        if (findtext[k] >= 'A' && findtext[k] <= 'Z') exact = 1;
    ln = b->cur.line;
    col = dir > 0 ? (b->sel && pos_cmp(b->anchor, b->cur) < 0 ? b->cur.col : b->cur.col + (b->sel ? 1 : 0))
                  : (b->sel ? (pos_cmp(b->anchor, b->cur) < 0 ? b->anchor.col : b->cur.col) - 1 : b->cur.col - 1);
    for (k = 0; k <= b->n; k++) {
        Line *l = &b->l[ln];
        long i;
        if (dir > 0) {
            for (i = col < 0 ? 0 : col; i + ftl <= l->len; i++)
                if (matchat(l->s, l->len, i, ftext, ftl, exact)) goto found;
            ln++;
            col = 0;
            if (ln >= b->n) {
                ln = 0;
                wrapped = 1;
            }
        } else {
            for (i = col > l->len - ftl ? l->len - ftl : col; i >= 0; i--)
                if (matchat(l->s, l->len, i, ftext, ftl, exact)) goto found;
            ln--;
            if (ln < 0) {
                ln = b->n - 1;
                wrapped = 1;
            }
            col = b->l[ln].len;
        }
        continue;
    found:
        b->anchor.line = ln;
        b->anchor.col = i;
        b->cur.line = ln;
        b->cur.col = i + ftl;
        b->sel = 1;
        setwant(b);
        follow = 1;
        if (!quiet) say(wrapped ? "found (from the other end)" : "", 0);
        return 1;
    }
    if (!quiet) {
        char m[180];
        strcpy(m, "not found: ");
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
    if (!prompt("Find:", findtext, sizeof(findtext), 0) || !findtext[0]) return;
    if (!prompt("Replace with:", repltext, sizeof(repltext), 0)) return;
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
    if (!prompt("Go to line:", t, sizeof(t), 0) || !t[0]) return;
    ln = atol(t);
    if (ln < 1) ln = 1;
    if (ln > b->n) ln = b->n;
    b->cur.line = ln - 1;
    b->cur.col = 0;
    b->sel = 0;
    setwant(b);
    follow = 1;
    b->top = b->cur.line - textrows() / 2;
    if (b->top < 0) b->top = 0;
}

/* ---- files and tabs ----------------------------------------------------- */

static int addtab(const char *path)
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
        if (!r) say("a new file", 0);
        else if (b->prot & FIBF_WRITE) say("this file is protected against writing", 1);
    }
    tabs[ntabs] = b;
    ct = ntabs++;
    return 1;
}

static void closetab(int i)
{
    buf_free(tabs[i]);
    memmove(&tabs[i], &tabs[i + 1], (ntabs - i - 1) * sizeof(Buf *));
    ntabs--;
    if (ct >= ntabs) ct = ntabs - 1;
    if (ct < 0) ct = 0;
}

static int samefile(const char *a, const char *b)
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
        if (!prompt("Save as:", path, sizeof(path), 1) || !path[0]) return 0;
        if (strcmp(path, b->path)) {
            BPTR l = Lock((STRPTR)path, SHARED_LOCK);
            if (l) {
                UnLock(l);
                if (ask("That file is there already. Write over it? (y/n)", "yn") != 'y') return 0;
            }
        }
    } else
        strcpy(path, b->path);
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
    if (!prompt("Open:", path, sizeof(path), 1) || !path[0]) return;
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

static void help(void)
{
    static const int order[] = {
        A_SAVE, A_SAVEAS, A_OPEN, A_NEWTAB, A_CLOSE, A_NEXTTAB, A_PREVTAB, A_QUIT, 0,
        A_UNDO, A_REDO, A_CUT, A_COPY, A_PASTE, A_CUTLINE, A_DUPLICATE, A_SELECTALL,
        A_TAB, A_UNTAB, A_DELWORDLEFT, A_DELWORDRIGHT, 0,
        A_FIND, A_FINDNEXT, A_FINDPREV, A_REPLACE, A_GOTO, 0,
        A_WORDLEFT, A_WORDRIGHT, A_LINESTART, A_LINEEND, A_PAGEUP, A_PAGEDOWN,
        A_FILESTART, A_FILEEND, 0, A_COMMAND, A_NUMBERS, A_ESCAPE, A_HELP, -1
    };
    static const char *const intro[] = {
        "Sumi - a text editor inside the terminal",
        "",
        "Type to write. Shift with the moving keys selects; the mouse places the",
        "cursor, drags a selection, double-clicks a word, the wheel scrolls.",
        "Copy and paste use the Amiga clipboard. Keys: ENVARC:Sumi/Sumi.keys",
        "Commands (the command key): save NAME, open NAME, goto N, set tabsize N,",
        "set tabs spaces|tabs, set numbers on|off, set autoindent on|off,",
        "set encoding utf8|latin1, set lineends lf|crlf, lang NAME, quit.",
        "",
        NULL
    };
    int top = 0, total, r, i;
    TuiEv e;
    for (;;) {
        int row = 0, line = 0;
        tui_clear();
        for (i = 0; intro[i]; i++, line++)
            if (line >= top && row < tui_rows - 1) {
                tui_put(1, row, intro[i], (int)strlen(intro[i]), i == 0 ? 12 : TC_DEFAULT,
                        TC_DEFAULT, i == 0 ? TA_BOLD : 0);
                row++;
            }
        for (i = 0; order[i] >= 0; i++, line++) {
            char names[64], k[24];
            int any = 0;
            if (line < top || row >= tui_rows - 1) continue;
            if (!order[i]) {
                row++;
                continue;
            }
            names[0] = 0;
            {
                /* the keys for it: walk the key space through keys_find */
                static const int mods[] = { 0, TM_CTRL, TM_ALT, TM_SHIFT, TM_SHIFT | TM_CTRL,
                                            TM_SHIFT | TM_ALT };
                int m, c;
                for (m = 0; m < 6 && strlen(names) < 30; m++) {
                    for (c = K_UP; c <= K_F(12) && strlen(names) < 30; c++)
                        if (keys_find(c | mods[m]) == order[i]) {
                            keyname(c | mods[m], k, 1);
                            if (any) strcat(names, " ");
                            strcat(names, k);
                            any = 1;
                        }
                    for (c = 'a'; c <= 'z' && strlen(names) < 30; c++)
                        if (m && keys_find(c | mods[m]) == order[i]) {
                            keyname(c | mods[m], k, 1);
                            if (any) strcat(names, " ");
                            strcat(names, k);
                            any = 1;
                        }
                }
            }
            tui_put(2, row, names, (int)strlen(names), 11, TC_DEFAULT, 0);
            tui_put(24, row, actdesc[order[i]], (int)strlen(actdesc[order[i]]), TC_DEFAULT, TC_DEFAULT, 0);
            row++;
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
    if (!prompt("Command:", cmd, sizeof(cmd), 1) || !cmd[0]) return;
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
        }
    } else if (!strcmp(w, "lang")) {
        for (i = 0; i < L_COUNT; i++)
            if (!nocase(a, langnames[i], -1) ||
                (i == L_ASM && !nocase(a, "asm", -1)) ||
                (i == L_E && !nocase(a, "e", -1)) ||
                (i == L_SCRIPT && !nocase(a, "script", -1)) ||
                (i == L_GUIDE && !nocase(a, "guide", -1)) ||
                (i == L_REXX && !nocase(a, "rexx", -1)) ||
                (i == L_PY && !nocase(a, "py", -1)) ||
                (i == L_MD && !nocase(a, "md", -1))) {
                B->lang = i;
                B->hsok = 0;
                return;
            }
        say("languages: text C asm rexx e guide script py md", 1);
    } else if (!strcmp(w, "quit") || !strcmp(w, "q")) {
        quitting = 1;
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
            say("set tabsize N | tabs spaces|tabs | numbers on|off | autoindent on|off | encoding utf8|latin1 | lineends lf|crlf", 1);
            return;
        }
        prefs_save();
        say("set (and kept)", 0);
    } else
        say("commands: save [NAME], open [NAME], goto N, set ..., lang NAME, quit", 1);
}

/* ---- the mouse ------------------------------------------------------------- */

static int mousepos(Buf *b, int col, int row, Pos *p)
{
    long ln = b->top + row - 1, x;
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
    static int lc = -1, lr = -1, dragging;
    Buf *b = B;
    Pos p;
    if (e->mact == TB_WHEELUP || e->mact == TB_WHEELDOWN) {
        b->top += e->mact == TB_WHEELUP ? -3 : 3;
        if (b->top > b->n - 1) b->top = b->n - 1;
        if (b->top < 0) b->top = 0;
        follow = 0;
        return;
    }
    if (e->mact == TB_PRESS && e->row == 0) {   /* the tab bar: which tab */
        int x = 5, i, from = 0;
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
        } else if (e->col == lc && e->row == lr && DoubleClick(ls, lm, e->secs, e->mics)) {
            /* a double click: the word */
            int n;
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
            lc = -1;
            setwant(b);
            return;
        } else {
            b->cur = p;
            b->anchor = p;
            b->sel = 0;
        }
        lc = e->col;
        lr = e->row;
        ls = e->secs;
        lm = e->mics;
        dragging = 1;
        setwant(b);
    } else if (e->mact == TB_DRAG && dragging) {
        b->cur = p;
        b->sel = pos_cmp(b->anchor, b->cur) != 0;
        setwant(b);
    } else if (e->mact == TB_RELEASE) {
        dragging = 0;
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
    case A_ENTER: donewline(b); break;
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
        if (prompt("Find:", findtext, sizeof(findtext), 0) && findtext[0]) find(b, 1, 0);
        break;
    case A_FINDNEXT: if (findtext[0]) find(b, 1, 0); else action(A_FIND); break;
    case A_FINDPREV: if (findtext[0]) find(b, -1, 0); else action(A_FIND); break;
    case A_REPLACE: doreplace(b); break;
    case A_GOTO: dogoto(b); break;
    case A_HELP: help(); break;
    case A_COMMAND: command(); break;
    case A_NUMBERS: numbers = !numbers; prefs_save(); break;
    case A_ESCAPE: b->sel = 0; msg[0] = 0; break;
    }
}

static void key(int k)
{
    int act = keys_find(k);
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
        strcat(m, " does nothing (ENVARC:Sumi/Sumi.keys)");
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
        PutStr((STRPTR)"Sumi is a text editor for the terminal: start it from a Shell\n"
                       "(in CTerm, or a console window). Usage: sumi [+LINE] [FILE ...]\n");
        return 10;
    }
    prefs_load();
    keys_load();
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '+' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            jump = atol(argv[i] + 1);
            continue;
        }
        if (addtab(argv[i]) && jump > 0) {
            B->cur.line = jump > B->n ? B->n - 1 : jump - 1;
            jump = 0;
        }
    }
    if (!ntabs && !addtab(NULL)) {
        PutStr((STRPTR)"Sumi: out of memory\n");
        return 20;
    }
    if (ntabs > 1) ct = 0;
    if (!tui_open()) {
        PutStr((STRPTR)"Sumi: cannot use this console\n");
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
            dopaste(B, (const char *)e.text, e.len, enc);
            break;
        }
        case TE_RESIZE: tui_resized(); break;
        }
    }
    tui_close();
    for (i = 0; i < ntabs; i++) buf_free(tabs[i]);
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
