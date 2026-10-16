/* view - the Markdown as it reads: md.c's rows drawn into the terminal,
 * moved like a pager, with links to follow (and back), task boxes to
 * tick, headings to jump between, a search, and pictures (CTerm shows
 * them: iTerm2's inline files, decoded by datatypes on the Amiga).
 *
 * The view is drawn full screen (a file being read) or beside the
 * editor (split: it follows the cursor's line). Pictures are sent after
 * the cells are flushed, only when one comes into sight whole; when the
 * cells under one are drawn over, it is sent again (view_forget). */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <inline/macros.h>
#include <string.h>
#include "markdown.h"
#include "md.h"

int split, ew = 80;

/* the area last drawn (the mouse and the page keys use it) */
static int ax0, aw, ay0 = 1, ah = 20, aoff, afull;

/* found by the search: a row and its cells */
static long fsrow = -1;
static int fscol, fslen;

/* split: the view stays where the wheel put it until the cursor moves */
static long detached = -1;

/* where links were followed from */
#define MAXHIST 32
static struct { Buf *b; long vtop, frow; int fcol; } hist[MAXHIST];
static int nhist;

/* ---- the layout ------------------------------------------------------------- */

/* the text's width in an area w wide, and where it starts in it */
static int textw(int w, int full, int *off)
{
    int m = full ? 2 : 1, tw = w - 2 * m;
    if (tw < 10) tw = w > 10 ? w : 10;
    if (mdwidth && tw > mdwidth) tw = mdwidth;
    if (off) *off = full && tw < w - 2 * m ? (w - tw) / 2 : m;
    if (off && *off + tw > w) *off = 0;
    return tw;
}

int viewrows = 20;               /* the view's height now (pictures fit in it) */

static int stamp(void)
{
    return pictures * 4096 + viewrows + (tui_cellw << 16) + (tui_cellh << 24) +
           (tui_utf8 ? 2048 : 0);
}

static int lastw = 80;

int view_doc(Buf *b, int width)
{
    Doc *d = b->doc;
    long keep = -1;
    int st = stamp();
    if (!d) {
        if (!(d = b->doc = AllocVec(sizeof(Doc), MEMF_ANY | MEMF_CLEAR))) return 0;
        d->rev = -1;
    }
    if (d->rev == b->rev && d->width == width && d->stamp == st && !d->failed) return 1;
    if (d->nrows && b->vtop < d->nrows && d->width != width) keep = d->rows[b->vtop].src;
    md_unicode = tui_cterm && tui_utf8;
    md_cellw = tui_cterm ? tui_cellw : 0;
    md_cellh = tui_cterm ? tui_cellh : 0;
    md_pictures = pictures;
    md_maxpicrows = viewrows - 2 > 3 ? viewrows - 2 : 3;
    md_render(d, b, width, b->path[0] ? b->path : NULL);
    d->stamp = st;
    if (keep >= 0) b->vtop = md_rowof(d, keep);
    if (b->frow >= d->nrows) b->frow = -1;
    fsrow = -1;
    return 1;
}

static long maxtop(Buf *b, int h)
{
    long m = b->doc ? b->doc->nrows - h : 0;
    return m > 0 ? m : 0;
}

static void clamp(Buf *b)
{
    long m = maxtop(b, ah);
    if (b->vtop > m) b->vtop = m;
    if (b->vtop < 0) b->vtop = 0;
}

/* ---- pictures ------------------------------------------------------------------- */

typedef struct { int x, y, cols, rows; char path[PATHMAX]; } Shown;
#define MAXSHOWN 12
static Shown shown[MAXSHOWN], wanted[MAXSHOWN];
static int nshown, nwanted;

static int samepic(const Shown *a, const Shown *b)
{
    return a->x == b->x && a->y == b->y && a->cols == b->cols && a->rows == b->rows &&
           !strcmp(a->path, b->path);
}

void view_forget(void)
{
    int i;
    for (i = 0; i < nshown; i++) tui_dirty(shown[i].x, shown[i].y, shown[i].cols, shown[i].rows);
    nshown = 0;
}

void view_reset(void)
{
    nshown = 0;
}

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void num(char *d, long v)
{
    char t[12];
    int k = 11;
    t[11] = 0;
    do t[--k] = (char)('0' + v % 10); while ((v /= 10) && k);
    strcat(d, t + k);
}

/* the file as iTerm2's inline picture, at its cells */
static void sendpic(const Shown *s)
{
    BPTR fh = Open((STRPTR)s->path, MODE_OLDFILE);
    long n, i, k, hl;
    UBYTE *raw;
    char *out, head[120];
    if (!fh) return;
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    if (n <= 0 || n > 4000000 || !(raw = AllocVec(n, MEMF_ANY))) {
        Close(fh);
        return;
    }
    n = Read(fh, raw, n);
    Close(fh);
    strcpy(head, "\x1b]1337;File=inline=1;size=");
    num(head, n);
    strcat(head, ";width=");
    num(head, s->cols);
    strcat(head, "c;height=");
    num(head, s->rows);
    strcat(head, "c;preserveAspectRatio=1:");
    hl = (long)strlen(head);
    if (n > 0 && (out = AllocVec(hl + (n + 2) / 3 * 4 + 4, MEMF_ANY)) != NULL) {
        memcpy(out, head, hl);
        k = hl;
        for (i = 0; i < n; i += 3) {
            ULONG v = (ULONG)raw[i] << 16 | (i + 1 < n ? (ULONG)raw[i + 1] << 8 : 0) |
                      (i + 2 < n ? raw[i + 2] : 0);
            out[k++] = b64[v >> 18 & 63];
            out[k++] = b64[v >> 12 & 63];
            out[k++] = i + 1 < n ? b64[v >> 6 & 63] : '=';
            out[k++] = i + 2 < n ? b64[v & 63] : '=';
        }
        out[k++] = 7;
        tui_raw(s->x, s->y, out, k);
        FreeVec(out);
    }
    FreeVec(raw);
}

/* after a flush: the pictures wanted that are not up yet */
void view_pictures(void)
{
    int i, j;
    for (i = 0; i < nwanted; i++) {
        for (j = 0; j < nshown && !samepic(&shown[j], &wanted[i]); j++) ;
        if (j < nshown) continue;
        sendpic(&wanted[i]);
        if (nshown < MAXSHOWN) shown[nshown++] = wanted[i];
    }
}

/* the pictures up that are not wanted any more: their cells drawn again */
static void unwanted(void)
{
    int i, j;
    for (i = 0; i < nshown;) {
        for (j = 0; j < nwanted && !samepic(&shown[i], &wanted[j]); j++) ;
        if (j < nwanted) {
            i++;
            continue;
        }
        tui_dirty(shown[i].x, shown[i].y, shown[i].cols, shown[i].rows);
        shown[i] = shown[--nshown];
    }
}

/* ---- targets: links and task boxes, in reading order ---------------------------- */

static int istask(const Doc *d, long row, int col)
{
    long i;
    for (i = 0; i < d->ntasks; i++)
        if (d->tasks[i].row == row && d->tasks[i].col == col) return (int)i + 1;
    return 0;
}

/* the span that begins a link (not its continuation on the next row) */
static int spanstart(const Doc *d, long k)
{
    const RSpan *s = &d->spans[k], *p;
    if (!k) return 1;
    p = &d->spans[k - 1];
    if (p->link != s->link) return 1;
    return !(p->row == s->row - 1 || (p->row == s->row && p->c1 == s->c0));
}

/* the first span of the target at (row, col), or -1 */
static long spanat(const Doc *d, long row, int col)
{
    long k;
    for (k = 0; k < d->nspans; k++)
        if (d->spans[k].row == row && d->spans[k].c0 == col) return k;
    return -1;
}

/* the next target after (row, col) (dir 1) or before it (-1) */
static int nexttarget(const Doc *d, long row, int col, int dir, long *nr, int *nc)
{
    long k, br = -1;
    int bc = 0;
    for (k = 0; k < d->nspans; k++) {
        const RSpan *s = &d->spans[k];
        int after;
        if (!spanstart(d, k)) continue;
        after = s->row > row || (s->row == row && s->c0 > col);
        if (dir > 0 ? !after : (s->row > row || (s->row == row && s->c0 >= col))) continue;
        if (br < 0 || (dir > 0 ? (s->row < br || (s->row == br && s->c0 < bc))
                               : (s->row > br || (s->row == br && s->c0 > bc)))) {
            br = s->row;
            bc = s->c0;
        }
    }
    for (k = 0; k < d->ntasks; k++) {
        const RTask *t = &d->tasks[k];
        int after;
        if (t->row < 0) continue;
        after = t->row > row || (t->row == row && t->col > col);
        if (dir > 0 ? !after : (t->row > row || (t->row == row && t->col >= col))) continue;
        if (br < 0 || (dir > 0 ? (t->row < br || (t->row == br && t->col < bc))
                               : (t->row > br || (t->row == br && t->col > bc)))) {
            br = t->row;
            bc = t->col;
        }
    }
    if (br < 0) return 0;
    *nr = br;
    *nc = bc;
    return 1;
}

/* is this cell part of the chosen target? */
static int chosen(const Buf *b, long row, int col)
{
    const Doc *d = b->doc;
    long k;
    if (b->frow < 0) return 0;
    if (istask(d, b->frow, b->fcol))
        return row == b->frow && col >= b->fcol && col < b->fcol + 3;
    k = spanat(d, b->frow, b->fcol);
    if (k < 0) return 0;
    {
        int link = d->spans[k].link;
        long q;
        for (q = k; q < d->nspans && d->spans[q].link == link && (q == k || !spanstart(d, q)); q++)
            if (d->spans[q].row == row && col >= d->spans[q].c0 && col < d->spans[q].c1) return 1;
    }
    return 0;
}

static const char *linkurl(const Buf *b)
{
    const Doc *d = b->doc;
    long k;
    if (!d || b->frow < 0 || istask(d, b->frow, b->fcol)) return NULL;
    k = spanat(d, b->frow, b->fcol);
    if (k < 0 || d->spans[k].link < 1 || d->spans[k].link > d->nlinks) return NULL;
    return md_str(d, d->links[d->spans[k].link - 1].url);
}

static int put8(int x, int y, const char *s, int max, int fg, int bg, int at);

/* ---- drawing ------------------------------------------------------------------- */

void view_draw(Buf *b, int x0, int w, int y0, int h, int full)
{
    Doc *d;
    int r, tw = textw(w, full, &aoff);
    ax0 = x0;
    aw = w;
    ay0 = y0;
    ah = h;
    afull = full;
    lastw = tw;
    nwanted = 0;
    if (!view_doc(b, tw)) {
        for (r = 0; r < h; r++) tui_fill(x0, y0 + r, w, ' ', TC_DEFAULT, TC_DEFAULT, 0);
        tui_put(x0 + 1, y0, "not enough memory for the view", 30, 9, TC_DEFAULT, 0);
        return;
    }
    d = b->doc;
    clamp(b);
    for (r = 0; r < h; r++) {
        long row = b->vtop + r;
        tui_fill(x0, y0 + r, w, ' ', TC_DEFAULT, TC_DEFAULT, 0);
        if (row >= d->nrows) {
            if (row == d->nrows && !d->nrows)
                tui_put(x0 + aoff, y0 + r, "(nothing to show)", 17, 8, TC_DEFAULT, 0);
            continue;
        }
        {
            const RRow *rw = &d->rows[row];
            const RCell *c = d->cells + rw->cell;
            int k, room = w - aoff;
            for (k = 0; k < rw->len && k < room; k++) {
                int fg = c[k].fg, bg = c[k].bg, at = c[k].at;
                if (chosen(b, row, k)) at ^= TA_REV;
                if (row == fsrow && k >= fscol && k < fscol + fslen) {
                    fg = 0;
                    bg = 3;
                }
                tui_putc(x0 + aoff + k, y0 + r, c[k].c, fg, bg, at);
            }
            if (rw->pic >= 0 && rw->pic < d->npics && nwanted < MAXSHOWN) {
                const RPic *p = &d->pics[rw->pic];
                if (r + p->rows <= h && aoff + p->col + p->cols <= w) {
                    Shown *s = &wanted[nwanted++];
                    s->x = x0 + aoff + p->col;
                    s->y = y0 + r;
                    s->cols = p->cols;
                    s->rows = p->rows;
                    strncpy(s->path, md_str(d, p->path), PATHMAX - 1);
                    s->path[PATHMAX - 1] = 0;
                }
            }
        }
    }
    {   /* a picture only partly in sight: its name on its first row in sight */
        long i;
        for (i = 0; i < d->npics; i++) {
            const RPic *p = &d->pics[i];
            long a0 = p->row, z0 = p->row + p->rows, first, mid = p->row + p->rows / 2;
            int j, x, shownow = 0;
            const char *alt = md_str(d, p->alt), *tail = pictures && md_cellw ?
                              "  - scroll to see it" : "";
            if (z0 <= b->vtop || a0 >= b->vtop + h) continue;
            for (j = 0; j < nwanted; j++)
                if (wanted[j].y == y0 + (a0 - b->vtop)) shownow = 1;
            if (shownow) continue;
            x = x0 + aoff + p->col;
            if (mid >= b->vtop && mid < b->vtop + h)
                tui_fill(x, y0 + (int)(mid - b->vtop), w - aoff - p->col, ' ', TC_DEFAULT,
                         TC_DEFAULT, 0);
            first = a0 > b->vtop ? a0 : b->vtop;
            tui_putc(x, y0 + (int)(first - b->vtop), md_unicode ? 0x2592 : '#', 13, TC_DEFAULT, 0);
            j = put8(x + 2, y0 + (int)(first - b->vtop), alt[0] ? alt : "a picture",
                     w - aoff - p->col - 24, 13, TC_DEFAULT, TA_ITAL);
            tui_put(x + 2 + j, y0 + (int)(first - b->vtop), tail, (int)strlen(tail), 8,
                    TC_DEFAULT, 0);
        }
    }
    if (d->failed) tui_put(x0 + aoff, y0 + h - 1, "(not enough memory: cut short)", 30, 9, TC_DEFAULT, 0);
    unwanted();
}

/* the section the view is in: the last heading at or above its top */
static const char *section(const Buf *b)
{
    const Doc *d = b->doc;
    long i;
    const char *s = NULL;
    if (!d) return NULL;
    for (i = 0; i < d->nheads && d->heads[i].row <= b->vtop + 1; i++) s = md_str(d, d->heads[i].text);
    return s;
}

/* UTF-8 (the pool's) onto the screen */
static int put8(int x, int y, const char *s, int max, int fg, int bg, int at)
{
    int n = 0;
    while (*s && n < max) {
        UBYTE c = (UBYTE)*s;
        ULONG v = c;
        if (c >= 0xC0) {
            int k = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            v = c & (0x3F >> k);
            while (k-- && (s[1] & 0xC0) == 0x80) v = v << 6 | (*++s & 0x3F);
        }
        s++;
        tui_putc(x + n++, y, v, fg, bg, at);
    }
    return n;
}

void view_status(void)
{
    Buf *b = B_;
    char s[60];
    const char *sec = section(b), *name = b->path[0] ? b->path : "(new file)";
    int row = tui_rows - 2, l, x;
    long pc;
    tui_fill(0, row, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, TA_REV);
    s[0] = 0;
    if (b->doc && b->doc->nrows > ah) {
        pc = (b->vtop + ah) * 100 / b->doc->nrows;
        if (pc > 100) pc = 100;
        num(s, pc);
        strcat(s, "%");
    } else
        strcat(s, "all");
    strcat(s, "  view ");
    l = (int)strlen(s);
    tui_put(tui_cols - l, row, s, l, TC_DEFAULT, TC_DEFAULT, TA_REV);
    x = 1;
    {
        int room = tui_cols - l - 2, nl = (int)strlen(name);
        if (nl > room / 2 && sec) nl = room / 2;
        if (nl > room) nl = room;
        if (nl > 0) {
            int from = (int)strlen(name) - nl;
            tui_put(x, row, name + from, nl, TC_DEFAULT, TC_DEFAULT, TA_REV | TA_BOLD);
            x += nl;
        }
        if (buf_dirty(b) && x + 10 < tui_cols - l) {
            tui_put(x, row, " [changed]", 10, TC_DEFAULT, TC_DEFAULT, TA_REV);
            x += 10;
        }
        if (sec && x + 6 < tui_cols - l) {
            tui_put(x, row, "  > ", 4, TC_DEFAULT, TC_DEFAULT, TA_REV);
            x += 4;
            put8(x, row, sec, tui_cols - l - x - 1, TC_DEFAULT, TC_DEFAULT, TA_REV);
        }
    }
}

void view_hints(void)
{
    static const int acts[] = { A_PAGEDOWN, A_NEXTLINK, A_FOLLOW, A_BACK, A_OUTLINE, A_VIEW,
                                A_FIND, A_CLOSE, A_HELP };
    static const char *const words[] = { "Page", "Link", "Follow", "Back", "Outline", "Edit",
                                         "Find", "Close", "Help" };
    Buf *b = B_;
    int x = 0, i, row = tui_rows - 1;
    char k[24];
    const char *u = linkurl(b);
    tui_fill(0, row, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
    if (u) {
        tui_put(0, row, "->", 2, 12, TC_DEFAULT, TA_BOLD);
        put8(3, row, u, tui_cols - 4, 12, TC_DEFAULT, 0);
        return;
    }
    if (b->frow >= 0 && b->doc && istask(b->doc, b->frow, b->fcol)) {
        if (keys_first(A_TASK, k, 1)) {
            int kl = (int)strlen(k);
            tui_put(0, row, k, kl, TC_DEFAULT, TC_DEFAULT, TA_REV);
            tui_put(kl + 1, row, "ticks the box, then save", 24, 8, TC_DEFAULT, 0);
        }
        return;
    }
    for (i = 0; i < (int)(sizeof(acts) / sizeof(acts[0])); i++) {
        int kl, wl;
        if (!keys_first(acts[i], k, 1)) continue;
        if (!strcmp(k, " ")) strcpy(k, "Space");
        kl = (int)strlen(k);
        wl = (int)strlen(words[i]);
        if (x + kl + wl + 1 > tui_cols) break;
        tui_put(x, row, k, kl, TC_DEFAULT, TC_DEFAULT, TA_REV);
        tui_put(x + kl + 1, row, words[i], wl, 8, TC_DEFAULT, 0);
        x += kl + wl + 3;
    }
}

/* ---- the place in the file and in the view ----------------------------------- */

void view_fromfile(Buf *b)
{
    int tw = textw(tui_cols, 1, NULL);
    long r, off = b->cur.line - b->top;
    if (!view_doc(b, tw)) return;
    r = md_rowof(b->doc, b->cur.line);
    if (off < 0 || off > textrows()) off = textrows() / 3;
    b->vtop = r - off;
    if (b->vtop < 0) b->vtop = 0;
    b->frow = -1;
}

void view_tofile(Buf *b)
{
    const Doc *d = b->doc;
    long first, last, line;
    if (!d || !d->nrows) return;
    clamp(b);
    first = d->rows[b->vtop].src;
    last = d->rows[b->vtop + ah - 1 < d->nrows ? b->vtop + ah - 1 : d->nrows - 1].src;
    if (b->frow >= 0 && b->frow >= b->vtop && b->frow < b->vtop + ah) line = d->rows[b->frow].src;
    else if (fsrow >= b->vtop && fsrow < b->vtop + ah) line = d->rows[fsrow].src;
    else if (b->cur.line >= first && b->cur.line <= last) line = b->cur.line;
    else line = first;
    if (line >= b->n) line = b->n - 1;
    if (line != b->cur.line) {
        b->cur.line = line;
        b->cur.col = 0;
    }
    b->sel = 0;
    b->wantx = 0;
    /* the line where it was on the screen */
    b->top = line - (md_rowof(d, line) - b->vtop);
    if (b->top > line) b->top = line;
    if (b->top < 0) b->top = 0;
    b->topseg = 0;
}

void view_follow_cursor(Buf *b, int crow, int w)
{
    int tw = textw(w, 0, NULL);
    if (!view_doc(b, tw)) return;
    if (detached >= 0 && detached == b->cur.line) return;
    detached = -1;
    b->vtop = md_rowof(b->doc, b->cur.line) - crow;
    if (b->vtop < 0) b->vtop = 0;
}

void view_close(Buf *b)
{
    int i;
    if (b->doc) {
        md_free(b->doc);
        FreeVec(b->doc);
        b->doc = NULL;
    }
    for (i = 0; i < nhist;)
        if (hist[i].b == b) {
            memmove(&hist[i], &hist[i + 1], (nhist - i - 1) * sizeof(hist[0]));
            nhist--;
        } else
            i++;
}

/* ---- links ------------------------------------------------------------------------ */

static void pushhist(Buf *b)
{
    if (nhist == MAXHIST) {
        memmove(&hist[0], &hist[1], (MAXHIST - 1) * sizeof(hist[0]));
        nhist--;
    }
    hist[nhist].b = b;
    hist[nhist].vtop = b->vtop;
    hist[nhist].frow = b->frow;
    hist[nhist].fcol = b->fcol;
    nhist++;
}

static void goback(void)
{
    while (nhist > 0) {
        int i;
        nhist--;
        for (i = 0; i < ntabs && tabs[i] != hist[nhist].b; i++) ;
        if (i == ntabs) continue;
        ct = i;
        tabs[i]->vtop = hist[nhist].vtop;
        tabs[i]->frow = hist[nhist].frow;
        tabs[i]->fcol = hist[nhist].fcol;
        if (!tabs[i]->view) {
            view_fromfile(tabs[i]);
            tabs[i]->view = 1;
            tabs[i]->vtop = hist[nhist].vtop;
        }
        return;
    }
    say("nowhere to go back to", 1);
}

static int jumpanchor(Buf *b, const char *a)
{
    int h;
    if (!b->doc) return 0;
    h = md_anchor(b->doc, a);
    if (h < 0) return 0;
    b->vtop = b->doc->heads[h].row;
    b->frow = -1;
    return 1;
}

/* openurl.library: the browser the person chose (OpenURL on Aminet) */
static int openurl(const char *url)
{
    struct Library *OpenURLBase = OpenLibrary((STRPTR)"openurl.library", 1);
    ULONG ok;
    if (!OpenURLBase) return 0;
    ok = LP2(30, ULONG, URL_OpenA, STRPTR, (STRPTR)url, a0, struct TagItem *, NULL, a1, ,
             OpenURLBase);
    CloseLibrary(OpenURLBase);
    return ok != 0;
}

static int ispicture(const char *p)
{
    static const char *const ext[] = { ".png", ".jpg", ".jpeg", ".gif", ".iff", ".ilbm", ".bmp",
                                       ".svg", ".webp", NULL };
    int l = (int)strlen(p), i;
    for (i = 0; ext[i]; i++) {
        int e = (int)strlen(ext[i]), k;
        if (l <= e) continue;
        for (k = 0; k < e && (p[l - e + k] | 32) == ext[i][k]; k++) ;
        if (k == e) return 1;
    }
    return 0;
}

static void follow(Buf *b, const char *url)
{
    char path[PATHMAX], m[PATHMAX + 40];
    const char *frag = strchr(url, '#');
    int i;
    BPTR l;
    if (url[0] == '#') {
        pushhist(b);
        if (!jumpanchor(b, url)) {
            nhist--;
            strcpy(m, "no heading for ");
            strncat(m, url, 100);
            say(m, 1);
        }
        return;
    }
    md_resolve(b->path, url, path, sizeof(path));
    if (!path[0]) {             /* an address: to the browser, else the clipboard */
        if (openurl(url)) {
            strcpy(m, "sent to the browser: ");
            strncat(m, url, 120);
            say(m, 0);
        } else {
            clip_put(url, (long)strlen(url), ENC_LATIN1);
            strcpy(m, "copied (no OpenURL here): ");
            strncat(m, url, 120);
            say(m, 0);
        }
        return;
    }
    if (!(l = Lock((STRPTR)path, SHARED_LOCK))) {
        strcpy(m, "not there: ");
        strncat(m, path, PATHMAX);
        say(m, 1);
        return;
    }
    {
        struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
        int dir = 0;
        if (fib) {
            if (Examine(l, fib)) dir = fib->fib_DirEntryType > 0;
            FreeDosObject(DOS_FIB, fib);
        }
        UnLock(l);
        if (dir) {
            strcpy(m, "a drawer: ");
            strncat(m, path, PATHMAX);
            say(m, 1);
            return;
        }
    }
    if (ispicture(path)) {
        strcpy(m, "a picture: ");
        strncat(m, path, PATHMAX);
        say(m, 0);
        return;
    }
    pushhist(b);
    for (i = 0; i < ntabs; i++)
        if (tabs[i]->path[0] && samefile(tabs[i]->path, path)) break;
    if (i < ntabs) ct = i;
    else if (!addtab(path)) {
        nhist--;
        return;
    }
    if (frag && B_->view) {
        view_doc(B_, textw(tui_cols, 1, NULL));
        jumpanchor(B_, frag);
    }
}

static void chooselink(Buf *b, int dir)
{
    long nr, from = b->frow;
    int nc, fc = b->fcol;
    if (from < b->vtop || from >= b->vtop + ah) {   /* the chosen one is out of sight */
        from = dir > 0 ? b->vtop : b->vtop + ah;
        fc = -1;
    }
    if (!nexttarget(b->doc, from, fc, dir, &nr, &nc)) {
        say(dir > 0 ? "no more links below" : "no more links above", 1);
        return;
    }
    b->frow = nr;
    b->fcol = nc;
    if (nr < b->vtop || nr >= b->vtop + ah) b->vtop = nr - ah / 3;
    clamp(b);
}

static void ticktask(Buf *b, int t)
{
    RTask *k = &b->doc->tasks[t - 1];
    Line *l;
    Pos a, z;
    if (k->src >= b->n) return;
    l = &b->l[k->src];
    if (k->scol + 2 >= l->len + 1 || l->s[k->scol] != '[' || l->s[k->scol + 2] != ']') {
        say("the box moved: look again", 1);
        return;
    }
    a.line = z.line = k->src;
    a.col = k->scol + 1;
    z.col = k->scol + 2;
    buf_group(b);
    buf_delete(b, a, z);
    buf_insert(b, a, k->done ? " " : "x", 1);
    buf_group(b);
    say(k->done ? "unticked (save to keep it)" : "ticked (save to keep it)", 0);
}

/* ---- finding --------------------------------------------------------------------- */

static void vfind(Buf *b, int dir)
{
    const Doc *d = b->doc;
    static char row[2048];
    long r, start, k;
    int first = 1;
    if (!d || !d->nrows || !findtext[0]) return;
    if (fsrow >= 0 && fsrow >= b->vtop && fsrow < b->vtop + ah) start = fsrow;
    else start = dir > 0 ? b->vtop : b->vtop + ah - 1, first = 0;
    if (start >= d->nrows) start = d->nrows - 1;
    for (k = 0, r = start; k <= d->nrows; k++) {
        const RRow *rw = &d->rows[r];
        const RCell *c = d->cells + rw->cell;
        long n = rw->len < (long)sizeof(row) ? rw->len : (long)sizeof(row) - 1, i, at, ml;
        long from;
        for (i = 0; i < n; i++) row[i] = (char)(c[i].c <= 0xFF ? c[i].c : '?');
        row[n] = 0;
        if (dir > 0) from = first && r == fsrow ? fscol + 1 : 0;
        else from = first && r == fsrow ? fscol - 1 : n;
        if (from >= 0 && from <= n && findrow(row, n, from, dir < 0, &at, &ml)) {
            fsrow = r;
            fscol = (int)at;
            fslen = ml > 0 ? (int)ml : 1;
            if (r < b->vtop || r >= b->vtop + ah) b->vtop = r - ah / 3;
            clamp(b);
            if (k && ((dir > 0 && r < start) || (dir < 0 && r > start)))
                say("found (from the other end)", 0);
            return;
        }
        first = 0;
        r += dir;
        if (r >= d->nrows) r = 0;
        if (r < 0) r = d->nrows - 1;
    }
    fsrow = -1;
    {
        char m[180];
        strcpy(m, "not found: ");
        strncat(m, findtext, 120);
        say(m, 1);
    }
}

/* ---- the keys ------------------------------------------------------------------------ */

int view_action(int act)
{
    Buf *b = B_;
    Doc *d;
    if (!view_doc(b, lastw)) return 0;
    d = b->doc;
    switch (act) {
    case A_DOWN: case A_LINEDOWN: b->vtop++; break;
    case A_UP: case A_LINEUP: b->vtop--; break;
    case A_PAGEDOWN: b->vtop += ah > 2 ? ah - 1 : 1; break;
    case A_PAGEUP: b->vtop -= ah > 2 ? ah - 1 : 1; break;
    case A_HALFDOWN: b->vtop += ah / 2; break;
    case A_HALFUP: b->vtop -= ah / 2; break;
    case A_FILESTART: b->vtop = 0; break;
    case A_FILEEND: b->vtop = maxtop(b, ah); break;
    case A_NEXTHEAD: case A_PREVHEAD: {
        long i, to = -1;
        for (i = 0; i < d->nheads; i++) {
            long r = d->heads[i].row;
            if (act == A_NEXTHEAD && r > b->vtop) {
                to = r;
                break;
            }
            if (act == A_PREVHEAD && r < b->vtop) to = r;
        }
        if (to < 0) say(act == A_NEXTHEAD ? "no more headings below" : "no headings above", 1);
        else b->vtop = to;
        break;
    }
    case A_OUTLINE: outline(); break;
    case A_NEXTLINK: chooselink(b, 1); break;
    case A_PREVLINK: chooselink(b, -1); break;
    case A_FOLLOW: case A_TASK: {
        const char *u = linkurl(b);
        int t = b->frow >= 0 ? istask(d, b->frow, b->fcol) : 0;
        if (!t && act == A_TASK) {  /* none chosen: the first box in sight */
            long i;
            for (i = 0; i < d->ntasks; i++)
                if (d->tasks[i].row >= b->vtop && d->tasks[i].row < b->vtop + ah) {
                    t = (int)i + 1;
                    break;
                }
        }
        if (t) ticktask(b, t);
        else if (u && act == A_FOLLOW) {
            char url[PATHMAX];
            strncpy(url, u, PATHMAX - 1);
            url[PATHMAX - 1] = 0;
            follow(b, url);
        } else
            say(act == A_TASK ? "no task box in sight" : "no link chosen: Tab chooses one", 1);
        break;
    }
    case A_BACK: goback(); break;
    case A_COPYLINK: {
        const char *u = linkurl(b);
        if (!u) say("no link chosen: Tab chooses one", 1);
        else {
            char m[200];
            clip_put(u, (long)strlen(u), ENC_LATIN1);
            strcpy(m, "copied: ");
            strncat(m, u, 150);
            say(m, 0);
        }
        break;
    }
    case A_FIND:
        if (prompt("Find:", findtext, sizeof(findtext), P_FIND) && findtext[0]) {
            fsrow = -1;
            vfind(b, 1);
        }
        break;
    case A_FINDNEXT: if (findtext[0]) vfind(b, 1); else view_action(A_FIND); break;
    case A_FINDPREV: if (findtext[0]) vfind(b, -1); else view_action(A_FIND); break;
    case A_VIEW:
        view_tofile(b);
        b->view = 0;
        view_forget();
        break;
    case A_PICTURES:
        pictures = !pictures;
        prefs_save();
        view_forget();
        say(pictures ? "pictures on" : "pictures off", 0);
        break;
    default:
        return 0;
    }
    clamp(b);
    return 1;
}

void view_mouse(TuiEv *e, int x0, int w, int y0, int h)
{
    Buf *b = B_;
    Doc *d = b->doc;
    long row;
    int col;
    (void)w;
    if (!d) return;
    if (e->mact == TB_WHEELUP || e->mact == TB_WHEELDOWN) {
        b->vtop += e->mact == TB_WHEELUP ? -3 : 3;
        ah = h;
        clamp(b);
        if (!b->view) detached = b->cur.line;
        return;
    }
    if (e->mact != TB_PRESS) return;
    row = b->vtop + (e->row - y0);
    col = e->col - x0 - aoff;
    if (row < 0 || row >= d->nrows) return;
    if (!b->view) {             /* split: a click in the view puts the cursor at its line */
        b->cur.line = d->rows[row].src < b->n ? d->rows[row].src : b->n - 1;
        b->cur.col = 0;
        b->sel = 0;
        b->wantx = 0;
        return;
    }
    {
        long i;
        for (i = 0; i < d->ntasks; i++)
            if (d->tasks[i].row == row && col >= d->tasks[i].col && col < d->tasks[i].col + 3) {
                b->frow = row;
                b->fcol = d->tasks[i].col;
                ticktask(b, (int)i + 1);
                return;
            }
        for (i = 0; i < d->nspans; i++)
            if (d->spans[i].row == row && col >= d->spans[i].c0 && col < d->spans[i].c1) {
                long k = i;
                char url[PATHMAX];
                while (k > 0 && !spanstart(d, k)) k--;
                b->frow = d->spans[k].row;
                b->fcol = d->spans[k].c0;
                strncpy(url, md_str(d, d->links[d->spans[i].link - 1].url), PATHMAX - 1);
                url[PATHMAX - 1] = 0;
                follow(b, url);
                return;
            }
    }
}

/* ---- the outline: the headings, to go to one ------------------------------------------ */

void outline(void)
{
    Buf *b = B_;
    Doc *d;
    long sel = 0, top = 0, i;
    int h = textrows();
    TuiEv e;
    if (!view_doc(b, b->view ? lastw : textw(tui_cols, 1, NULL))) return;
    d = b->doc;
    if (!d->nheads) {
        say("no headings", 1);
        return;
    }
    for (i = 0; i < d->nheads; i++)
        if (b->view ? d->heads[i].row <= b->vtop + 1 : d->heads[i].src <= b->cur.line) sel = i;
    for (;;) {
        int r, rows = h - 1;
        drawall();
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;
        tui_fill(0, 1, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, TA_REV);
        tui_put(1, 1, "Outline - Return goes there, Esc goes back", 42, TC_DEFAULT, TC_DEFAULT,
                TA_REV | TA_BOLD);
        for (r = 0; r < rows; r++) {
            long k = top + r;
            int y = 2 + r, x;
            tui_fill(0, y, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
            if (k >= d->nheads) continue;
            x = 2 + (d->heads[k].level - 1) * 2;
            if (x > tui_cols / 2) x = tui_cols / 2;
            put8(x, y, md_str(d, d->heads[k].text), tui_cols - x - 1,
                 k == sel ? TC_DEFAULT : d->heads[k].level <= 2 ? 12 : TC_DEFAULT, TC_DEFAULT,
                 (k == sel ? TA_REV : 0) | (d->heads[k].level <= 2 ? TA_BOLD : 0));
        }
        tui_cursor(0, 0, 0);
        tui_flush();
        if (!tui_wait(&e, -1, 0, NULL)) continue;
        if (e.type == TE_END) {               /* the terminal is gone: as Esc */
            view_forget();
            return;
        }
        if (e.type == TE_RESIZE) {
            tui_resized();
            view_reset();
            h = textrows();
            continue;
        }
        if (e.type == TE_MOUSE) {
            if (e.mact == TB_WHEELDOWN && sel < d->nheads - 1) sel += 3;
            if (e.mact == TB_WHEELUP) sel -= 3;
            if (sel >= d->nheads) sel = d->nheads - 1;
            if (sel < 0) sel = 0;
            if (e.mact == TB_PRESS && e.row >= 2 && top + e.row - 2 < d->nheads) {
                sel = top + e.row - 2;
                break;
            }
            continue;
        }
        if (e.type != TE_KEY) continue;
        if (e.key == K_ESC || e.key == 'q' || e.key == (TM_CTRL | 'c')) {
            view_forget();
            return;
        }
        if (e.key == K_ENTER || e.key == K_RIGHT) break;
        if (e.key == K_DOWN || e.key == 'j') sel++;
        else if (e.key == K_UP || e.key == 'k') sel--;
        else if (e.key == K_PGDN || e.key == (K_DOWN | TM_ALT) || e.key == ' ') sel += rows - 1;
        else if (e.key == K_PGUP || e.key == (K_UP | TM_ALT) || e.key == 'b') sel -= rows - 1;
        else if (e.key == K_HOME || e.key == (K_UP | TM_CTRL) || e.key == 'g') sel = 0;
        else if (e.key == K_END || e.key == (K_DOWN | TM_CTRL) || e.key == 'G') sel = d->nheads - 1;
        else if (e.key > ' ' && e.key < 0x100) {   /* a letter: the next heading starting so */
            long k;
            for (k = 1; k <= d->nheads; k++) {
                long q = (sel + k) % d->nheads;
                const char *t = md_str(d, d->heads[q].text);
                if ((*t | 32) == (e.key | 32)) {
                    sel = q;
                    break;
                }
            }
        }
        if (sel >= d->nheads) sel = d->nheads - 1;
        if (sel < 0) sel = 0;
    }
    if (b->view) {
        pushhist(b);
        b->vtop = d->heads[sel].row;
        b->frow = -1;
        clamp(b);
    } else {
        b->cur.line = d->heads[sel].src < b->n ? d->heads[sel].src : b->n - 1;
        b->cur.col = 0;
        b->sel = 0;
        b->wantx = 0;
        b->top = b->cur.line > 1 ? b->cur.line - 1 : 0;
        b->topseg = 0;
    }
    view_forget();
}
