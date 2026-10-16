/* buf - a file's text as an array of lines (bytes, Latin-1 or UTF-8 as
 * the file has them), changed only through buf_insert and buf_delete so
 * that every change is in the undo list. Consecutive typing (or
 * backspacing) is one undo step; any other command starts a new one
 * (buf_group). A file is changed when the newest undo step is not the
 * one it was saved at. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>
#include "markdown.h"

static long serials;            /* every undo step gets its own */
static int replaying;           /* undo/redo at work: nothing recorded */

int pos_cmp(Pos a, Pos b)
{
    if (a.line != b.line) return a.line < b.line ? -1 : 1;
    if (a.col != b.col) return a.col < b.col ? -1 : 1;
    return 0;
}

/* ---- lines ---------------------------------------------------------- */

static int line_room(Line *l, long need)
{
    char *p;
    long cap;
    if (need <= l->cap) return 1;
    cap = need + need / 2 + 16;
    if (!(p = AllocVec(cap, MEMF_ANY))) return 0;
    if (l->s) {
        CopyMem(l->s, p, l->len);
        if (l->own) FreeVec(l->s);
    }
    l->s = p;
    l->cap = cap;
    l->own = 1;
    return 1;
}

static int line_insert(Line *l, long col, const char *t, long n)
{
    if (n <= 0) return 1;
    if (!line_room(l, l->len + n)) return 0;
    memmove(l->s + col + n, l->s + col, l->len - col);
    memcpy(l->s + col, t, n);
    l->len += n;
    return 1;
}

static void line_cut(Line *l, long col, long n)
{
    memmove(l->s + col, l->s + col + n, l->len - col - n);
    l->len -= n;
}

/* room for k more lines at index i (they start empty) */
static int lines_open(Buf *b, long i, long k)
{
    if (b->n + k > b->cap) {
        long cap = (b->n + k) + (b->n + k) / 2 + 64;
        Line *p = AllocVec(cap * sizeof(Line), MEMF_ANY | MEMF_CLEAR);
        if (!p) return 0;
        if (b->l) {
            CopyMem(b->l, p, b->n * sizeof(Line));
            FreeVec(b->l);
        }
        b->l = p;
        b->cap = cap;
    }
    memmove(&b->l[i + k], &b->l[i], (b->n - i) * sizeof(Line));
    memset(&b->l[i], 0, k * sizeof(Line));
    b->n += k;
    return 1;
}

static void lines_close(Buf *b, long i, long k)
{
    long j;
    for (j = i; j < i + k; j++)
        if (b->l[j].s && b->l[j].own) FreeVec(b->l[j].s);
    memmove(&b->l[i], &b->l[i + k], (b->n - i - k) * sizeof(Line));
    b->n -= k;
}

Buf *buf_new(void)
{
    Buf *b = AllocVec(sizeof(Buf), MEMF_ANY | MEMF_CLEAR);
    if (!b) return NULL;
    if (!lines_open(b, 0, 1)) {
        FreeVec(b);
        return NULL;
    }
    b->enc = ENC_LATIN1;
    b->finalnl = 1;
    b->prot = 0;
    return b;
}

static void undo_free(Undo *u)
{
    while (u) {
        Undo *n = u->next;
        if (u->text) FreeVec(u->text);
        FreeVec(u);
        u = n;
    }
}

void buf_clearundo(Buf *b)
{
    undo_free(b->undo);
    undo_free(b->redo);
    b->undo = b->redo = NULL;
    b->saved = 0;
    b->typing = 0;
}

void buf_free(Buf *b)
{
    long i;
    if (!b) return;
    for (i = 0; i < b->n; i++)
        if (b->l[i].s && b->l[i].own) FreeVec(b->l[i].s);
    if (b->l) FreeVec(b->l);
    if (b->bulk) FreeVec(b->bulk);
    buf_clearundo(b);
    FreeVec(b);
}

/* A loaded file's text as the lines, in one go: they point into the
 * block (one allocation, not one a line - a big file loads in a moment)
 * and get their own memory only when they change. No undo for it. */
int buf_load(Buf *b, char *text, long n)
{
    long lines = 1, i, k = 0, start = 0;
    Line *l;
    for (i = 0; i < n; i++)
        if (text[i] == '\n') lines++;
    if (!(l = AllocVec(lines * sizeof(Line) + 64 * sizeof(Line), MEMF_ANY | MEMF_CLEAR))) return 0;
    for (i = 0; i <= n; i++)
        if (i == n || text[i] == '\n') {
            l[k].s = text + start;
            l[k].len = l[k].cap = i - start;
            l[k].own = 0;
            k++;
            start = i + 1;
        }
    for (i = 0; i < b->n; i++)
        if (b->l[i].s && b->l[i].own) FreeVec(b->l[i].s);
    if (b->l) FreeVec(b->l);
    if (b->bulk) FreeVec(b->bulk);
    b->l = l;
    b->n = k;
    b->cap = lines + 64;
    b->bulk = text;
    b->hsok = 0;
    b->rev++;
    return 1;
}

int buf_dirty(Buf *b)
{
    return (b->undo ? b->undo->serial : 0) != b->saved;
}

void buf_group(Buf *b)
{
    b->group++;
    b->typing = 0;
}

/* ---- the text between two places ------------------------------------- */

char *buf_text(Buf *b, Pos a, Pos z, long *len)
{
    long n = 0, i, o = 0;
    char *t;
    for (i = a.line; i <= z.line; i++) {
        long s = i == a.line ? a.col : 0, e = i == z.line ? z.col : b->l[i].len;
        n += e - s + (i < z.line);
    }
    if (!(t = AllocVec(n + 1, MEMF_ANY))) return NULL;
    for (i = a.line; i <= z.line; i++) {
        long s = i == a.line ? a.col : 0, e = i == z.line ? z.col : b->l[i].len;
        if (e > s) memcpy(t + o, b->l[i].s + s, e - s);
        o += e - s;
        if (i < z.line) t[o++] = '\n';
    }
    t[o] = 0;
    *len = n;
    return t;
}

/* ---- undo records ---------------------------------------------------- */

static void record(Buf *b, int ins, Pos a, const char *t, long n, Pos before, Pos after)
{
    Undo *u = b->undo;
    if (replaying) return;
    undo_free(b->redo);             /* a new change: the old future is gone */
    b->redo = NULL;
    if (u && u->group == b->group && u->ins == ins && b->typing) {
        /* the same typing run: one step */
        if (ins && u->a.line == a.line && u->a.col + u->tlen == a.col && !memchr(t, '\n', n)) {
            char *p = AllocVec(u->tlen + n, MEMF_ANY);
            if (p) {
                memcpy(p, u->text, u->tlen);
                memcpy(p + u->tlen, t, n);
                FreeVec(u->text);
                u->text = p;
                u->tlen += n;
                u->after = after;
                u->serial = ++serials;  /* changed again: not the saved state any more */
                return;
            }
        } else if (!ins && pos_cmp(after, before) <= 0 && a.line == u->a.line &&
                   a.col + n == u->a.col && !memchr(t, '\n', n)) {
            /* backspace: what went before what already went */
            char *p = AllocVec(u->tlen + n, MEMF_ANY);
            if (p) {
                memcpy(p, t, n);
                memcpy(p + n, u->text, u->tlen);
                FreeVec(u->text);
                u->text = p;
                u->tlen += n;
                u->a = a;
                u->after = after;
                u->serial = ++serials;
                return;
            }
        } else if (!ins && pos_cmp(a, u->a) == 0 && !memchr(t, '\n', n)) {
            /* delete: what came after */
            char *p = AllocVec(u->tlen + n, MEMF_ANY);
            if (p) {
                memcpy(p, u->text, u->tlen);
                memcpy(p + u->tlen, t, n);
                FreeVec(u->text);
                u->text = p;
                u->tlen += n;
                u->after = after;
                u->serial = ++serials;
                return;
            }
        }
    }
    if (!(u = AllocVec(sizeof(Undo), MEMF_ANY | MEMF_CLEAR))) return;
    if (n && !(u->text = AllocVec(n, MEMF_ANY))) {
        FreeVec(u);
        return;
    }
    if (n) memcpy(u->text, t, n);
    u->tlen = n;
    u->ins = ins;
    u->a = a;
    u->before = before;
    u->after = after;
    u->group = b->group;
    u->serial = ++serials;
    u->next = b->undo;
    b->undo = u;
}

/* ---- the two changes ------------------------------------------------- */

static void touched(Buf *b, long line)
{
    b->rev++;                   /* the view knows it must be made again */
    if (b->hsok > line + 1) b->hsok = line + 1;
}

Pos buf_insert(Buf *b, Pos at, const char *t, long n)
{
    Pos end = at, before = b->cur;
    const char *nl;
    long k = 0, i;
    if (n <= 0) return at;
    for (i = 0; i < n; i++)
        if (t[i] == '\n') k++;
    touched(b, at.line);
    if (!k) {
        if (!line_insert(&b->l[at.line], at.col, t, n)) return at;
        end.col += n;
    } else {
        Line *l;
        long rest = b->l[at.line].len - at.col, first;
        char *tail = NULL;
        if (!lines_open(b, at.line + 1, k)) return at;
        l = &b->l[at.line];
        if (rest > 0) {             /* what was after the place goes to the end */
            if (!(tail = AllocVec(rest, MEMF_ANY))) {
                lines_close(b, at.line + 1, k);
                return at;
            }
            memcpy(tail, l->s + at.col, rest);
            l->len = at.col;
        }
        nl = memchr(t, '\n', n);
        first = nl - t;
        line_insert(l, at.col, t, first);
        t = nl + 1;
        n -= first + 1;
        for (i = 1; i <= k; i++) {
            const char *e = i < k ? memchr(t, '\n', n) : t + n;
            long m = e - t;
            line_insert(&b->l[at.line + i], 0, t, m);
            if (i < k) {
                t = e + 1;
                n -= m + 1;
            } else
                end.col = m;
        }
        end.line = at.line + k;
        if (tail) {
            line_insert(&b->l[end.line], end.col, tail, rest);
            FreeVec(tail);
        }
        t = NULL;
    }
    if (!replaying) {
        /* (t was walked: take the text again for the record) */
        long len;
        char *copy = buf_text(b, at, end, &len);
        if (copy) {
            record(b, 1, at, copy, len, before, end);
            FreeVec(copy);
        }
    }
    return end;
}

void buf_delete(Buf *b, Pos a, Pos z)
{
    Pos before = b->cur;
    if (pos_cmp(a, z) >= 0) return;
    touched(b, a.line);
    if (!replaying) {
        long len;
        char *t = buf_text(b, a, z, &len);
        if (t) {
            record(b, 0, a, t, len, before, a);
            FreeVec(t);
        }
    }
    if (a.line == z.line) {
        line_cut(&b->l[a.line], a.col, z.col - a.col);
        return;
    }
    {
        Line *la = &b->l[a.line], *lz = &b->l[z.line];
        la->len = a.col;
        line_insert(la, a.col, lz->s + z.col, lz->len - z.col);
        lines_close(b, a.line + 1, z.line - a.line);
    }
}

/* ---- undo and redo ---------------------------------------------------- */

static Pos endof(Pos a, const char *t, long n)
{
    long i;
    for (i = 0; i < n; i++)
        if (t[i] == '\n') {
            a.line++;
            a.col = 0;
        } else
            a.col++;
    return a;
}

static void apply(Buf *b, Undo *u, int forward)
{
    int put = forward ? u->ins : !u->ins;
    replaying = 1;
    if (put) buf_insert(b, u->a, u->text, u->tlen);
    else buf_delete(b, u->a, endof(u->a, u->text, u->tlen));
    replaying = 0;
    b->cur = forward ? u->after : u->before;
    if (b->cur.line >= b->n) b->cur.line = b->n - 1;
    if (b->cur.col > b->l[b->cur.line].len) b->cur.col = b->l[b->cur.line].len;
}

int buf_undo(Buf *b)
{
    long g;
    if (!b->undo) return 0;
    g = b->undo->group;
    while (b->undo && b->undo->group == g) {
        Undo *u = b->undo;
        b->undo = u->next;
        apply(b, u, 0);
        u->next = b->redo;
        b->redo = u;
    }
    b->sel = 0;
    buf_group(b);
    return 1;
}

int buf_redo(Buf *b)
{
    long g;
    if (!b->redo) return 0;
    g = b->redo->group;
    while (b->redo && b->redo->group == g) {
        Undo *u = b->redo;
        b->redo = u->next;
        apply(b, u, 1);
        u->next = b->undo;
        b->undo = u;
    }
    b->sel = 0;
    buf_group(b);
    return 1;
}

/* ---- characters and columns -------------------------------------------- */

long buf_next(Buf *b, long line, long col)
{
    Line *l = &b->l[line];
    if (col >= l->len) return l->len;
    col++;
    if (b->enc == ENC_UTF8)
        while (col < l->len && (l->s[col] & 0xC0) == 0x80) col++;
    return col;
}

long buf_prev(Buf *b, long line, long col)
{
    Line *l = &b->l[line];
    if (col <= 0) return 0;
    col--;
    if (b->enc == ENC_UTF8)
        while (col > 0 && (l->s[col] & 0xC0) == 0x80) col--;
    return col;
}

ULONG buf_char(Buf *b, long line, long col, int *len)
{
    Line *l = &b->l[line];
    unsigned char c;
    if (col >= l->len) {
        *len = 0;
        return 0;
    }
    c = (unsigned char)l->s[col];
    *len = 1;
    if (b->enc == ENC_UTF8 && c >= 0xC2 && c <= 0xF4) {
        int n = c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4, k;
        ULONG u = c & (0x7F >> n);
        for (k = 1; k < n; k++) {
            if (col + k >= l->len || (l->s[col + k] & 0xC0) != 0x80) return c;
            u = (u << 6) | (l->s[col + k] & 0x3F);
        }
        *len = n;
        return u;
    }
    return c;
}

long buf_xof(Buf *b, long line, long col)
{
    long x = 0, i = 0;
    Line *l = &b->l[line];
    while (i < col && i < l->len) {
        int n;
        ULONG c = buf_char(b, line, i, &n);
        if (c == '\t') x = (x / tabsize + 1) * tabsize;
        else x++;
        i += n;
    }
    return x;
}

long buf_colat(Buf *b, long line, long x)
{
    long cx = 0, i = 0;
    Line *l = &b->l[line];
    while (i < l->len) {
        int n;
        ULONG c = buf_char(b, line, i, &n);
        long nx = c == '\t' ? (cx / tabsize + 1) * tabsize : cx + 1;
        if (nx > x) break;
        cx = nx;
        i += n;
    }
    return i;
}
