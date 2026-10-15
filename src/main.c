/* Maki - a pager that lives inside the terminal: less for the Amiga, in
 * a CTerm tab or pane or any console window. Pages forwards and back,
 * searches (regular expressions, every match marked), shows ANSI colours
 * as colours, reads a pipe as it comes (List | Maki), follows a growing
 * file, opens big files without reading them whole, Latin-1, UTF-8 and
 * the PC's CP437. Every key is the person's (keys.c).
 *
 * Usage: maki [-N] [-S] [+LINE | +/TEXT] [FILE ...]   (no file: the pipe)
 *
 * The screen: the text (line numbers on the left if wanted), and the
 * status line at the bottom, where questions are asked too.
 *
 * Its name: maki (巻) is a scroll - Sumi writes, Maki unrolls. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include <stdlib.h>
#include "maki.h"

static const char version[] = "$VER: Maki 0.1b2 (15.10.2026) - pager";

typedef struct {
    Src *s;
    long tl, ts, left;          /* the view: top line, its first shown row, the side scroll */
    long mline;                 /* the line of the last match (n goes on from it) */
    long marks[26];             /* line + 1, 0 unset */
} View;

#define MAXF 32
static View files[MAXF], helpv;
static int nfiles, cf, inhelp;
#define V (inhelp ? &helpv : &files[cf])

static int numbers, chop, following, quitting;
static long count = -1;         /* a number typed before a key */
static char msg[200];
static int msgerr;
static char findtext[128];
static int findback, showfound;

/* a line as cells: what render() made of its bytes */
typedef struct {
    ULONG c;
    UBYTE fg, bg, at, hl;
} LCell;
#define MAXLB 32768
#define MAXC  40000
static UBYTE *lb, *lp;          /* the line's bytes; the cells as Latin-1 (for searching) */
static LCell *lc;
static long lcn;
static Src *rsrc;               /* what lc holds now */
static long rline = -1, rend, rsize;
static int renc;

#define C_DIM 8

static void say(const char *m, int err)
{
    strncpy(msg, m, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = 0;
    msgerr = err;
}

static int textrows(void)
{
    return tui_rows > 1 ? tui_rows - 1 : 1;
}

static int digits(long n)
{
    int d = 1;
    while (n >= 10) n /= 10, d++;
    return d;
}

static int gutter(View *v)
{
    long n = src_lines(v->s);
    int d;
    if (!numbers || inhelp) return 0;
    if (n < v->tl + tui_rows) n = v->tl + tui_rows;
    d = digits(n);
    return (d < 4 ? 4 : d) + 1;
}

static int textcols(View *v)
{
    int w = tui_cols - gutter(v);
    return w > 8 ? w : 8;
}

/* ---- a line made into cells -------------------------------------------- */

static const UWORD cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

/* a terminal without UTF-8 (a plain console): lines and blocks in ASCII */
static ULONG plain(ULONG u)
{
    if (u < 0x100 || tui_utf8) return u;
    if (u == 0x2500 || u == 0x2550) return '-';
    if (u == 0x2502 || u == 0x2551) return '|';
    if (u > 0x2500 && u < 0x2580) return '+';
    if (u == 0x2591) return ':';
    if (u >= 0x2580 && u <= 0x25A0) return '#';
    if (u == 0x2219) return 0xB7;
    return '?';
}

static void put(ULONG u, UWORD st, int at)
{
    LCell *p;
    if (lcn >= MAXC) return;
    p = &lc[lcn];
    p->c = plain(u);
    p->fg = (UBYTE)ST_FG(st);
    p->bg = (UBYTE)ST_BG(st);
    p->at = (UBYTE)(ST_AT(st) | at);
    p->hl = 0;
    lp[lcn++] = (UBYTE)(u < 0x100 ? u : 0x1A);
}

static long render(Src *s, long i)
{
    long a, z, n, k;
    int esc = 0, np = 0, pv[16], back = 0;
    int enc = s->enc == ENC_AUTO ? ENC_LATIN1 : s->enc;
    UWORD st;
    if (!src_line(s, i)) {
        lcn = 0;
        rsrc = NULL;
        return 0;
    }
    a = s->lo[i];
    z = src_lineend(s, i);
    if (rsrc == s && rline == i && rend == z && renc == enc && rsize == s->size) return lcn;
    rsrc = s;
    rline = i;
    rend = z;
    renc = enc;
    rsize = s->size;
    n = z - a;
    if (n > MAXLB) n = MAXLB;
    n = src_get(s, a, lb, n);
    if (n > 0 && lb[n - 1] == '\n') n--;
    if (n > 0 && lb[n - 1] == '\r') n--;
    st = s->ls[i];
    lcn = 0;
    pv[0] = 0;
    for (k = 0; k < n && lcn < MAXC - 8;) {
        int c = lb[k++];
        ULONG u = c;
        if (esc) {
            switch (esc) {
            case 1:
                if (c == '[') esc = 2, np = 0, pv[0] = 0;
                else if (c == ']') esc = 3;
                else esc = 0;
                break;
            case 2:
                if (c >= '0' && c <= '9') pv[np] = pv[np] * 10 + c - '0';
                else if (c == ';') {
                    if (np < 15) np++;
                    pv[np] = 0;
                } else if (c >= 0x40 && c <= 0x7E) {
                    if (c == 'm') st = sgr_apply(st, pv, np + 1);
                    else if (c == 'C') {            /* forward: ANSI art's spaces */
                        int m = pv[0] ? pv[0] : 1;
                        while (m-- > 0 && lcn < MAXC - 8) put(' ', ST_DEFAULT, 0);
                    }
                    esc = 0;
                }
                break;
            case 3:
                if (c == 7) esc = 0;
                else if (c == 27) esc = 1;
                break;
            }
            continue;
        }
        if (c == 27) {
            esc = 1;
            continue;
        }
        if (c == 0x9B && enc != ENC_UTF8) {         /* the Amiga's CSI */
            esc = 2, np = 0, pv[0] = 0;
            continue;
        }
        if (c == '\t') {
            do put(' ', st, 0); while (lcn % 8);
            continue;
        }
        if (c == '\b') {                            /* overstrike: man's bold and underline */
            back = 1;
            continue;
        }
        if (c < 32 || c == 127) {
            put('^', ST_DEFAULT, TA_REV);
            put(c == 127 ? '?' : c + 64, ST_DEFAULT, TA_REV);
            back = 0;
            continue;
        }
        if (enc == ENC_UTF8 && c >= 0xC2 && c <= 0xF4) {
            int need = c <= 0xDF ? 1 : c <= 0xEF ? 2 : 3, j;
            ULONG v = c & (need == 1 ? 0x1F : need == 2 ? 0x0F : 0x07);
            for (j = 0; j < need && k + j < n && (lb[k + j] & 0xC0) == 0x80; j++)
                v = v << 6 | (lb[k + j] & 0x3F);
            if (j == need) {
                u = v;
                k += need;
            }
        } else if (enc == ENC_CP437 && c >= 0x80)
            u = cp437[c - 128];
        else if (c >= 0x80 && c < 0xA0) {
            put('?', ST_DEFAULT, TA_REV);
            back = 0;
            continue;
        }
        if (back && lcn > 0) {
            LCell *p = &lc[lcn - 1];
            back = 0;
            if (p->c == plain(u)) p->at |= TA_BOLD;
            else if (p->c == '_') {
                p->c = plain(u);
                lp[lcn - 1] = (UBYTE)(u < 0x100 ? u : 0x1A);
                p->at |= TA_UNDER;
            } else if (u == '_')
                p->at |= TA_UNDER;
            else {
                p->c = plain(u);
                lp[lcn - 1] = (UBYTE)(u < 0x100 ? u : 0x1A);
            }
            continue;
        }
        back = 0;
        put(u, st, 0);
    }
    return lcn;
}

static long nrows(View *v, long i)
{
    long w;
    if (chop) return 1;
    render(v->s, i);
    w = textcols(v);
    return lcn ? (lcn + w - 1) / w : 1;
}

/* ---- finding: a regular expression, as Sumi's -------------------------------
 * . any character, * + ? repeats (greedy), [abc] [^a-z] classes, ^ $ the
 * line's ends, \d \w \s digits/word/space (\D \W \S the others), \b a
 * word's edge, \x the character x. A capital in it makes it exact. */

static int fexact;

static int ci(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c >= 0xC0 && c <= 0xDE && c != 0xD7 ? c + 32 : c;
}

static int fci(int c)
{
    return fexact ? c : ci(c);
}

static int isword(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || (c >= 0xC0 && c != 0xD7 && c != 0xF7);
}

static const char *re_classend(const char *p)
{
    if (*p == '^') p++;
    if (*p == ']') p++;
    while (*p && *p != ']') p++;
    return p;
}

static int re_one(const char *p, const char *pe, int c)
{
    if (*p == '.') return 1;
    if (*p == '\\' && p + 1 < pe) {
        int k = (unsigned char)p[1];
        switch (k) {
        case 'd': return c >= '0' && c <= '9';
        case 'D': return !(c >= '0' && c <= '9');
        case 'w': return isword(c);
        case 'W': return !isword(c);
        case 's': return c == ' ' || c == '\t';
        case 'S': return !(c == ' ' || c == '\t');
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

static const char *re_next(const char *p)
{
    if (*p == '\\' && p[1]) return p + 2;
    if (*p == '[') {
        const char *e = re_classend(p + 1);
        return *e ? e + 1 : e;
    }
    return p + 1;
}

static int re_here(const char *p, const char *pe, const UBYTE *s, long len, long i, long *end)
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
            int a = i > 0 && isword(s[i - 1]), c = i < len && isword(s[i]);
            if (a == c) return 0;
            p += 2;
            continue;
        }
        n = re_next(p);
        rep = n < pe ? *n : 0;
        if (rep == '*' || rep == '+' || rep == '?') {
            long k = i, most;
            while (k < len && (rep != '?' || k == i) && re_one(p, n, s[k])) k++;
            most = k;
            if (rep == '+' && most == i) return 0;
            for (k = most; k >= i + (rep == '+'); k--)
                if (re_here(n + 1, pe, s, len, k, end)) return 1;
            return 0;
        }
        if (i >= len || !re_one(p, n, s[i])) return 0;
        i++;
        p = n;
    }
    *end = i;
    return 1;
}

/* a match in s at or after from: its start, its length */
static int matchin(const UBYTE *s, long len, long from, long *at, long *ml)
{
    const char *p = findtext, *pe = findtext + strlen(findtext);
    int anchored = *p == '^';
    long i, end;
    if (anchored) {
        p++;
        if (from > 0) return 0;
    }
    for (i = from; i <= len; i++) {
        if (re_here(p, pe, s, len, i, &end) && end > i) {
            *at = i;
            *ml = end - i;
            return 1;
        }
        if (anchored) break;
    }
    return 0;
}

/* Lines are first looked at as bytes: only one holding the pattern's
 * longest plain (ASCII) stretch - or anything that changes what is shown,
 * an escape sequence, a backspace, a tab when the stretch has a space - is
 * made into cells and matched. Ten times faster on a 14 MHz A1200. */
static UBYTE lit[128], hit[256], fold[256];
static int litn;

static void findsetup(void)
{
    const char *k, *p = findtext;
    UBYTE run[128];
    int rn = 0, c, i;
    fexact = 0;
    for (k = findtext; *k; k++)
        if ((*k >= 'A' && *k <= 'Z') || ((unsigned char)*k >= 0xC0 && (unsigned char)*k <= 0xDE))
            fexact = 1;
    litn = 0;
    if (*p == '^') p++;
    while (*p) {
        const char *n = re_next(p);
        int rep = *n;
        c = -1;
        if (*p == '\\') {
            if (p[1] && !strchr("dDwWsSbt", p[1])) c = (unsigned char)p[1];
        } else if (*p != '.' && *p != '[' && !(*p == '$' && !p[1]))
            c = (unsigned char)*p;
        if (c < 32 || c >= 0x7F || rep == '*' || rep == '?') c = -1;
        if (c >= 0) {
            run[rn++] = (UBYTE)c;
            if (rn > litn) memcpy(lit, run, litn = rn);
        } else
            rn = 0;
        if (rep == '*' || rep == '?' || rep == '+') {
            n++;
            rn = 0;
        }
        p = n;
    }
    for (i = 0; i < 256; i++) fold[i] = (UBYTE)(fexact ? i : i >= 'A' && i <= 'Z' ? i + 32 : i);
    for (i = 0; i < litn; i++) lit[i] = fold[lit[i]];
    memset(hit, 0, sizeof(hit));
    if (!litn) return;
    for (i = 0; i < 256; i++)
        if (fold[i] == lit[0]) hit[i] = 1;
    hit[27] = hit[0x9B] = hit[8] = 2;
    if (memchr(lit, ' ', litn)) hit['\t'] = 2;
}

/* line i might match (it is worth making into cells) */
static int maymatch(Src *s, long i)
{
    long a, n, k, j;
    if (!litn) return 1;
    a = s->lo[i];
    n = src_lineend(s, i) - a;
    if (n > MAXLB) n = MAXLB;
    n = src_get(s, a, lb, n);
    rsrc = NULL;                        /* lb is render()'s too */
    for (k = 0; k < n; k++) {
        int h = hit[lb[k]];
        if (!h) continue;
        if (h == 2) return 1;
        if (k + litn > n) continue;
        for (j = 1; j < litn && fold[lb[k + j]] == lit[j]; j++) ;
        if (j == litn) return 1;
    }
    return 0;
}

/* the matches in the rendered line marked */
static void markline(void)
{
    long i, at, ml;
    for (i = 0; i < lcn; i++) lc[i].hl = 0;
    if (!showfound || !findtext[0]) return;
    for (i = 0; i <= lcn && matchin(lp, lcn, i, &at, &ml); i = at + ml) {
        long k;
        for (k = 0; k < ml; k++) lc[at + k].hl = 1;
    }
}

/* ---- moving ------------------------------------------------------------------ */

static int stepdown(View *v)
{
    if (v->ts + 1 < nrows(v, v->tl)) {
        v->ts++;
        return 1;
    }
    if (!src_line(v->s, v->tl + 1)) return 0;
    v->tl++;
    v->ts = 0;
    return 1;
}

static int stepup(View *v)
{
    if (v->ts > 0) {
        v->ts--;
        return 1;
    }
    if (v->tl == 0) return 0;
    v->tl--;
    v->ts = nrows(v, v->tl) - 1;
    return 1;
}

/* the rows there are from the top of the view (up to limit) */
static long rowsfrom(View *v, long limit)
{
    long i = v->tl, r = nrows(v, i) - v->ts;
    while (r < limit && src_line(v->s, ++i)) r += nrows(v, i);
    return r;
}

/* the view with its last row on the file's last row */
static void toend(View *v)
{
    long n, k;
    src_scan(v->s, 0x7FFFFFFF);
    n = src_lines(v->s);
    if (n == 0) {
        v->tl = v->ts = 0;
        return;
    }
    v->tl = n - 1;
    v->ts = nrows(v, v->tl) - 1;
    for (k = 1; k < textrows() && stepup(v); k++) ;
}

/* never past the end: the last page is full (or the file is short) */
static void clamp(View *v)
{
    long n;
    if (v->tl > 0 && !src_line(v->s, v->tl)) v->tl = src_lines(v->s) ? src_lines(v->s) - 1 : 0;
    n = nrows(v, v->tl);
    if (v->ts >= n) v->ts = n - 1;
    if (rowsfrom(v, textrows()) < textrows()) {
        long tl = v->tl, ts = v->ts;
        toend(v);
        if (v->tl > tl || (v->tl == tl && v->ts > ts)) v->tl = tl, v->ts = ts;  /* never down */
    }
}

static void down(View *v, long n)
{
    while (n-- > 0 && stepdown(v)) ;
    clamp(v);
}

static void up(View *v, long n)
{
    while (n-- > 0 && stepup(v)) ;
}

static void golinen(View *v, long n)
{
    if (n < 1) n = 1;
    src_line(v->s, n - 1);
    if (n > src_lines(v->s)) n = src_lines(v->s);
    v->tl = n > 0 ? n - 1 : 0;
    v->ts = 0;
    clamp(v);
}

/* ---- drawing ----------------------------------------------------------------- */

static const char *const encnames[ENC_COUNT] = { "?", "Latin-1", "UTF-8", "CP437" };

static void putnum(char *d, long n)
{
    char b[12];
    int i = sizeof(b) - 1;
    b[i] = 0;
    if (n < 0) n = 0;
    do b[--i] = (char)('0' + n % 10); while ((n /= 10) && i);
    strcat(d, b + i);
}

/* a of b in percent, without overflow (files past 20 MB) */
static long pct(long a, long b)
{
    return b > 20000000 ? a / (b / 100) : a * 100 / b;
}

static void status(View *v, long bottom, int atend)
{
    Src *s = v->s;
    char l[300], r[120];
    int row = tui_rows - 1, ll, rl;
    tui_fill(0, row, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, TA_REV);
    if (msg[0]) {
        int ml = (int)strlen(msg);
        tui_fill(0, row, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
        tui_put(0, row, msg, ml < tui_cols ? ml : tui_cols, msgerr ? 9 : 14, TC_DEFAULT, TA_BOLD);
        return;
    }
    strcpy(l, " ");
    strncat(l, inhelp ? "Help (q goes back)" : s->title, 160);
    if (!inhelp && nfiles > 1) {
        strcat(l, " (");
        putnum(l, cf + 1);
        strcat(l, "/");
        putnum(l, nfiles);
        strcat(l, ")");
    }
    if (src_lines(s)) {
        strcat(l, "  line ");
        putnum(l, v->tl + 1);
        strcat(l, "-");
        putnum(l, bottom + 1);
        strcat(l, "/");
        if (src_full(s)) putnum(l, src_lines(s));
        else {
            putnum(l, src_lines(s));
            strcat(l, s->kind == SK_MEM && !s->done ? "+" : "?");
        }
    } else
        strcat(l, s->kind == SK_MEM && !s->done ? "  (waiting for the pipe)" : "  (empty)");
    if (atend) strcat(l, "  (END)");
    else if (s->size > 0 && (src_full(s) || s->kind == SK_FILE)) {
        long off = src_lineend(s, bottom);
        strcat(l, "  ");
        putnum(l, pct(off, s->size));
        strcat(l, "%");
    }
    r[0] = 0;
    if (count >= 0) {
        strcat(r, ":");
        putnum(r, count);
        strcat(r, "  ");
    }
    if (following) strcat(r, "following  ");
    if (chop) strcat(r, "cut  ");
    strcat(r, encnames[s->enc]);
    strcat(r, "  h help ");
    ll = (int)strlen(l);
    rl = (int)strlen(r);
    if (ll + rl >= tui_cols) rl = 0;
    tui_put(0, row, l, ll < tui_cols ? ll : tui_cols, TC_DEFAULT, TC_DEFAULT, TA_REV);
    if (rl) tui_put(tui_cols - rl, row, r, rl, TC_DEFAULT, TC_DEFAULT, TA_REV);
}

static void draw(void)
{
    View *v = V;
    Src *s = v->s;
    int g = gutter(v), w = textcols(v), r = 0, x;
    long i = v->tl, seg = v->ts, bottom = v->tl;
    int atend;
    while (r < textrows()) {
        long nr, start;
        if (!src_line(s, i)) {
            tui_fill(0, r, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
            if (i > 0 || src_lines(s)) tui_putc(0, r, '~', C_DIM, TC_DEFAULT, 0);
            r++;
            continue;
        }
        render(s, i);
        markline();
        nr = chop ? 1 : (lcn ? (lcn + w - 1) / w : 1);
        for (; seg < nr && r < textrows(); seg++, r++) {
            tui_fill(0, r, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
            if (g && seg == 0) {
                char n[16];
                int l;
                n[0] = 0;
                putnum(n, i + 1);
                l = (int)strlen(n);
                tui_put(g - 1 - l, r, n, l, C_DIM, TC_DEFAULT, 0);
            }
            start = chop ? v->left : seg * w;
            for (x = 0; x < w && start + x < lcn; x++) {
                LCell *p = &lc[start + x];
                tui_putc(g + x, r, p->c, p->fg, p->bg, p->hl ? p->at ^ TA_REV : p->at);
            }
            if (chop && lcn > start + w) tui_putc(tui_cols - 1, r, '>', C_DIM, TC_DEFAULT, TA_REV);
        }
        bottom = i;
        if (seg >= nr) {
            i++;
            seg = 0;
        }
    }
    atend = src_full(s) && !src_line(s, i) && seg == 0;
    status(v, bottom, atend);
    tui_cursor(0, tui_rows - 1, 0);
    tui_flush();
}

/* ---- a question on the bottom line ------------------------------------------- */

enum { P_FIND, P_GOTO, P_KINDS };
#define PHIST 12
static char phist[P_KINDS][PHIST][128];
static int phn[P_KINDS];

static void phist_add(int kind, const char *t)
{
    if (!t[0] || (phn[kind] && !strcmp(phist[kind][phn[kind] - 1], t))) return;
    if (phn[kind] == PHIST) {
        memmove(phist[kind][0], phist[kind][1], (PHIST - 1) * 128);
        phn[kind]--;
    }
    strncpy(phist[kind][phn[kind]], t, 127);
    phist[kind][phn[kind]++][127] = 0;
}

/* buf edited in place; 1 Return, 0 Esc */
static int prompt(const char *q, char *buf, int max, int kind)
{
    int len = (int)strlen(buf), pos = len, ql = (int)strlen(q), hp = phn[kind];
    TuiEv e;
    for (;;) {
        int row = tui_rows - 1, field = tui_cols - ql - 1, start, i;
        msg[0] = 0;
        draw();
        tui_fill(0, row, tui_cols, ' ', TC_DEFAULT, TC_DEFAULT, 0);
        tui_put(0, row, q, ql, 14, TC_DEFAULT, TA_BOLD);
        start = pos >= field ? pos - field + 1 : 0;
        for (i = start; i < len && ql + i - start < tui_cols; i++)
            tui_putc(ql + i - start, row, (unsigned char)buf[i], TC_DEFAULT, TC_DEFAULT, 0);
        tui_cursor(ql + pos - start, row, 1);
        tui_flush();
        if (!tui_wait(&e, -1, 0, NULL)) continue;
        if (e.type == TE_RESIZE) {
            tui_resized();
            continue;
        }
        if (e.type == TE_PASTE) {
            long k;
            for (k = 0; k < e.len && len < max - 1; k++)
                if (e.text[k] >= 32) {
                    memmove(buf + pos + 1, buf + pos, len - pos + 1);
                    buf[pos++] = (char)e.text[k];
                    len++;
                }
            continue;
        }
        if (e.type != TE_KEY) continue;
        switch (e.key) {
        case K_ENTER: phist_add(kind, buf); return 1;
        case K_UP: case K_DOWN:
            if (e.key == K_UP && hp > 0) hp--;
            else if (e.key == K_DOWN && hp < phn[kind]) hp++;
            else break;
            if (hp < phn[kind]) strncpy(buf, phist[kind][hp], max - 1);
            else buf[0] = 0;
            buf[max - 1] = 0;
            len = pos = (int)strlen(buf);
            break;
        case K_ESC: case TM_CTRL | 'c': return 0;
        case K_LEFT: if (pos > 0) pos--; break;
        case K_RIGHT: if (pos < len) pos++; break;
        case K_HOME: case K_LEFT | TM_CTRL: pos = 0; break;
        case K_END: case K_RIGHT | TM_CTRL: pos = len; break;
        case K_BS:
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, len - pos + 1);
                pos--;
                len--;
            } else if (!len)
                return 0;               /* Backspace on nothing: as less, away */
            break;
        case K_DEL:
            if (pos < len) {
                memmove(buf + pos, buf + pos + 1, len - pos);
                len--;
            }
            break;
        case TM_CTRL | 'u': buf[0] = 0; len = pos = 0; break;
        default:
            if (e.key >= 32 && e.key <= 255 && len < max - 1) {
                memmove(buf + pos + 1, buf + pos, len - pos + 1);
                buf[pos++] = (char)e.key;
                len++;
            }
        }
    }
}

/* the next key (a mark's letter); 0 for Esc */
static int onekey(const char *q)
{
    TuiEv e;
    say(q, 0);
    for (;;) {
        draw();
        if (!tui_wait(&e, -1, 0, NULL)) continue;
        if (e.type == TE_RESIZE) {
            tui_resized();
            continue;
        }
        if (e.type != TE_KEY) continue;
        msg[0] = 0;
        return e.key == K_ESC ? 0 : e.key;
    }
}

/* ---- searching -------------------------------------------------------------- */

static int stopped(void)
{
    if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) {
        SetSignal(0, SIGBREAKF_CTRL_C);
        say("Stopped", 1);
        return 1;
    }
    return 0;
}

static void search(View *v, int back)
{
    Src *s = v->s;
    long i, at, ml, n = 0;
    if (!findtext[0]) {
        say("Nothing to search for: / first", 1);
        return;
    }
    findsetup();
    showfound = 1;
    if (v->mline >= 0) i = back ? v->mline - 1 : v->mline + 1;
    else i = back ? v->tl - 1 : v->tl;
    say("Searching... (Ctrl+C stops)", 0);
    draw();
    for (;; back ? i-- : i++) {
        if (i < 0 || !src_line(s, i)) {
            say(back ? "Not found before here" : "Not found after here", 1);
            return;
        }
        if (maymatch(s, i)) {
            render(s, i);
            if (matchin(lp, lcn, 0, &at, &ml)) break;
        }
        if (!(++n & 1023)) {
            char m[60];
            if (stopped()) return;
            strcpy(m, "Searching... line ");
            putnum(m, i + 1);
            strcat(m, " (Ctrl+C stops)");
            say(m, 0);
            status(v, v->tl, 0);
            tui_flush();
        }
    }
    msg[0] = 0;
    v->mline = i;
    v->tl = i;
    v->ts = 0;
    if (chop) {
        int w = textcols(v);
        if (at < v->left || at + ml > v->left + w) v->left = at > w / 3 ? at - w / 3 : 0;
    } else {
        long r = at / textcols(v);
        if (r >= textrows()) v->ts = r;
    }
    clamp(v);
}

static void asksearch(View *v, int back)
{
    char t[128];
    t[0] = 0;
    if (!prompt(back ? "?" : "/", t, sizeof(t), P_FIND)) return;
    if (t[0]) {
        strcpy(findtext, t);
        v->mline = -1;
    }
    findback = back;
    search(v, back);
}

/* ---- the help: a text of our own, shown like a file --------------------------- */

static void showhelp(void)
{
    static const char head[] =
        "Maki - a pager that lives inside the terminal.\n"
        "Every key can be changed in ENVARC:Maki/Maki.keys (KEY ACTION, one a line).\n"
        "q goes back to the file.\n\n";
    static const char tail[] =
        "\nA number typed before a key counts: 5 Space goes five pages on,\n"
        "120 g to line 120, 50 % to the middle.\n\n"
        "Searching: / forward, ? backward, n and N the next and the one before.\n"
        "The text is a regular expression: . any character, * + ? repeats,\n"
        "[abc] [^a-z] classes, ^ $ the line's ends, \\d \\w \\s digits, word,\n"
        "space, \\b a word's edge, \\. a dot. A capital letter makes it exact.\n\n"
        "Starting: maki [-N] [-S] [+LINE | +/TEXT] [FILE ...]\n"
        "  -N line numbers, -S long lines cut (not folded). No file: what is\n"
        "  piped in (List | Maki). ENV:MAKI holds options to start with.\n"
        "  ENV:EDITOR names the editor for the edit key (Sumi if not set).\n\n"
        "ANSI colours show as colours; Latin-1, UTF-8 and the PC's CP437 (.ans .nfo\n"
        ".diz) are guessed, and the encoding key changes it.\n";
    char *t, names[60];
    long n = 0;
    int a;
    if (inhelp) return;
    if (!(t = AllocVec(8192, MEMF_ANY))) return;
    strcpy(t, head);
    for (a = 1; a < A_COUNT; a++) {
        int l;
        keys_all(a, names, sizeof(names));
        if (!names[0]) strcpy(names, "-");
        l = (int)strlen(names);
        strcat(t, "  ");
        strcat(t, names);
        while (l++ < 26) strcat(t, " ");
        strcat(t, actdesc[a]);
        strcat(t, "\n");
    }
    strcat(t, tail);
    n = (long)strlen(t);
    memset(&helpv, 0, sizeof(helpv));
    helpv.mline = -1;
    helpv.s = src_text(t, n, "Help");
    FreeVec(t);
    if (helpv.s) inhelp = 1;
}

static void closehelp(void)
{
    if (!inhelp) return;
    src_free(helpv.s);
    helpv.s = NULL;
    inhelp = 0;
    rsrc = NULL;
}

/* ---- editing: Sumi (or ENV:EDITOR) at this line ---------------------------------- */

static void doedit(View *v)
{
    char ed[100], cmd[PATHMAX + 140], name[PATHMAX], err[100];
    long tl = v->tl;
    BPTR in, out;
    Src *s;
    if (inhelp || v->s->kind != SK_FILE) {
        say("Only a file can be edited", 1);
        return;
    }
    if (GetVar((STRPTR)"EDITOR", (STRPTR)ed, sizeof(ed), 0) <= 0) strcpy(ed, "Sumi");
    strcpy(name, v->s->name);
    strcpy(cmd, ed);
    strcat(cmd, " +");
    putnum(cmd, tl + 1);
    strcat(cmd, " \"");
    strcat(cmd, name);
    strcat(cmd, "\"");
    src_free(v->s);                     /* closed: the editor may write it anew */
    v->s = NULL;
    rsrc = NULL;
    tui_suspend();
    in = Open((STRPTR)"*", MODE_OLDFILE);
    out = Open((STRPTR)"*", MODE_NEWFILE);
    if (in && out) SystemTags((STRPTR)cmd, SYS_Input, in, SYS_Output, out, TAG_END);
    if (in) Close(in);
    if (out) Close(out);
    tui_resume();
    s = src_file(name, err);
    if (!s) {
        static char m[] = "(the file could not be opened again)\n";
        s = src_text(m, sizeof(m) - 1, name);
    }
    v->s = s;
    v->tl = tl;
    v->ts = 0;
    v->mline = -1;
    clamp(v);
}

/* ---- the keys ------------------------------------------------------------------- */

static void info(View *v)
{
    Src *s = v->s;
    char m[200];
    src_scan(s, 0x7FFFFFFF);
    strcpy(m, inhelp ? "Help" : s->title);
    strcat(m, ": ");
    putnum(m, src_lines(s));
    strcat(m, " lines, ");
    putnum(m, s->size);
    strcat(m, " bytes, ");
    strcat(m, encnames[s->enc]);
    if (s->kind == SK_MEM && !s->done) strcat(m, ", still coming");
    say(m, 0);
}

static void tofile(int f)
{
    if (f < 0 || f >= nfiles) {
        say(f < 0 ? "This is the first file" : "This is the last file", 1);
        return;
    }
    cf = f;
    rsrc = NULL;
}

static void action(int a)
{
    View *v = V;
    long n = count;
    int w = textcols(v);
    count = -1;
    msg[0] = 0;
    switch (a) {
    case A_DOWN: down(v, n > 0 ? n : 1); break;
    case A_UP: up(v, n > 0 ? n : 1); break;
    case A_PAGEDOWN: down(v, (n > 0 ? n : 1) * textrows()); break;
    case A_PAGEUP: up(v, (n > 0 ? n : 1) * textrows()); break;
    case A_HALFDOWN: down(v, n > 0 ? n : textrows() / 2); break;
    case A_HALFUP: up(v, n > 0 ? n : textrows() / 2); break;
    case A_START:
        if (n > 0) golinen(v, n);
        else v->tl = v->ts = 0;
        break;
    case A_END:
        if (n > 0) golinen(v, n);
        else toend(v);
        break;
    case A_LEFT:
        if (!chop) {
            say("Long lines are folded: the wrap key cuts them, then this looks aside", 0);
            break;
        }
        v->left -= n > 0 ? n : w / 2;
        if (v->left < 0) v->left = 0;
        break;
    case A_RIGHT:
        if (!chop) {
            say("Long lines are folded: the wrap key cuts them, then this looks aside", 0);
            break;
        }
        v->left += n > 0 ? n : w / 2;
        break;
    case A_SEARCH: asksearch(v, 0); break;
    case A_RSEARCH: asksearch(v, 1); break;
    case A_NEXT: search(v, findback); break;
    case A_PREV: search(v, !findback); break;
    case A_QUIT:
        if (inhelp) closehelp();
        else quitting = 1;
        break;
    case A_HELP:
        if (inhelp) closehelp();
        else showhelp();
        break;
    case A_NUMBERS: numbers = !numbers; clamp(v); break;
    case A_WRAP:
        chop = !chop;
        v->ts = 0;
        v->left = 0;
        clamp(v);
        say(chop ? "Long lines cut (Left/Right look aside)" : "Long lines folded", 0);
        break;
    case A_FOLLOW:
        if (inhelp) break;
        following = 1;
        say("Following the end as it grows - a key stops", 0);
        break;
    case A_EDIT: doedit(v); break;
    case A_NEXTFILE: if (!inhelp) tofile(n > 0 ? cf + (int)n : cf + 1); break;
    case A_PREVFILE: if (!inhelp) tofile(n > 0 ? cf - (int)n : cf - 1); break;
    case A_INFO: info(v); break;
    case A_ENCODING: {
        Src *s = v->s;
        s->enc = s->enc % (ENC_COUNT - 1) + 1;     /* Latin-1, UTF-8, CP437, ... */
        s->encset = 1;
        src_reset(s);
        rsrc = NULL;
        clamp(v);
        say(encnames[s->enc], 0);
        break;
    }
    case A_MARK: {
        int k = onekey("Mark: a letter");
        if (k >= 'a' && k <= 'z') {
            v->marks[k - 'a'] = v->tl + 1;
            say("Marked", 0);
        }
        break;
    }
    case A_GOMARK: {
        int k = onekey("To the mark: a letter");
        if (k >= 'a' && k <= 'z' && v->marks[k - 'a']) golinen(v, v->marks[k - 'a']);
        else if (k) say("No such mark", 1);
        break;
    }
    case A_PERCENT: {
        Src *s = v->s;
        if (n < 0) n = 0;
        if (n > 100) n = 100;
        if (n == 100) toend(v);
        else {
            v->tl = src_lineat(s, s->size > 20000000 ? s->size / 100 * n : s->size * n / 100);
            v->ts = 0;
            clamp(v);
        }
        break;
    }
    case A_GOTO: {
        char t[128];
        t[0] = 0;
        if (n > 0) golinen(v, n);
        else if (prompt("Line: ", t, 16, P_GOTO) && t[0]) golinen(v, atol(t));
        break;
    }
    case A_REDRAW: tui_redraw(); break;
    case A_STOP:
        if (!following && n < 0) showfound = 0;
        following = 0;
        break;
    }
}

static void key(int k)
{
    int a;
    if (following) {                    /* any key stops following */
        following = 0;
        msg[0] = 0;
        return;
    }
    a = keys_find(k);
    if (a == A_NONE && k >= '0' && k <= '9') {
        if (count < 0) count = 0;
        if (count < 100000000) count = count * 10 + k - '0';
        msg[0] = 0;
        return;
    }
    if (a != A_NONE) {
        action(a);
        return;
    }
    {
        char m[80], nm[24];
        keyname(k, nm, 0);
        strcpy(m, nm);
        strcat(m, " does nothing (h shows the keys; ENVARC:Maki/Maki.keys)");
        say(m, 1);
        count = -1;
    }
}

static void mouse(TuiEv *e)
{
    View *v = V;
    if (following) return;
    if (e->mact == TB_WHEELDOWN) down(v, 3);
    else if (e->mact == TB_WHEELUP) up(v, 3);
}

/* ---- not a terminal: what it would show, written out (as cat) ------------------- */

static void copyout(BPTR from, BPTR to)
{
    UBYTE *b = AllocVec(8192, MEMF_ANY);
    long n;
    if (!b) return;
    while ((n = Read(from, b, 8192)) > 0)
        if (Write(to, b, n) != n) break;
    FreeVec(b);
}

/* ---- the start ---------------------------------------------------------------------- */

static void usage(void)
{
    PutStr((STRPTR)"Maki shows text a page at a time, inside the terminal.\n"
                   "Usage: maki [-N] [-S] [+LINE | +/TEXT] [FILE ...]\n"
                   "       command | maki\n"
                   "  -N line numbers   -S long lines cut   +LINE start there\n"
                   "  +/TEXT start at the first match   h in Maki shows the keys\n");
}

static char *opts[24];
static int nopts;
static char envopts[120];

static int smain(int argc, char **argv)
{
    int i, wantpipe = 0, rc = 0;
    long jump = 0;
    char start[128], err[100];
    BPTR in = Input(), conin = 0;
    TuiEv e;
    start[0] = 0;
    /* ENV:MAKI first (as less's LESS), then the command line */
    if (GetVar((STRPTR)"MAKI", (STRPTR)envopts, sizeof(envopts), 0) > 0) {
        char *p = envopts;
        while (*p && nopts < 8) {
            while (*p == ' ') *p++ = 0;
            if (!*p) break;
            opts[nopts++] = p;
            while (*p && *p != ' ') p++;
        }
    }
    for (i = 1; i < argc && nopts < 24; i++) opts[nopts++] = argv[i];
    if (!lb) return 20;
    for (i = 0; i < nopts; i++) {
        char *a = opts[i];
        if (!strcmp(a, "?") || !strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage();
            return 0;
        }
        if (a[0] == '-' && a[1] && a[1] != '-') {
            char *c;
            for (c = a + 1; *c; c++)
                if (*c == 'N' || *c == 'n') numbers = 1;
                else if (*c == 'S' || *c == 's') chop = 1;
            continue;
        }
        if (a[0] == '+' && a[1] == '/') {
            strncpy(start, a + 2, sizeof(start) - 1);
            continue;
        }
        if (a[0] == '+' && a[1] >= '0' && a[1] <= '9') {
            jump = atol(a + 1);
            continue;
        }
        if (!strcmp(a, "-")) {
            wantpipe = 1;
            continue;
        }
        if (nfiles < MAXF) {
            Src *s = src_file(a, err);
            if (!s) {
                PutStr((STRPTR)"Maki: ");
                PutStr((STRPTR)a);
                PutStr((STRPTR)err);
                PutStr((STRPTR)"\n");
                rc = 10;
                continue;
            }
            memset(&files[nfiles], 0, sizeof(View));
            files[nfiles].s = s;
            files[nfiles].mline = -1;
            nfiles++;
        }
    }
    if (!nfiles && rc) return rc;
    if (!nfiles && IsInteractive(in)) {
        usage();
        return 0;
    }
    /* not a terminal to show in: everything written out, as cat would */
    if (!IsInteractive(Output())) {
        for (i = 0; i < nfiles; i++) {
            Src *s = files[i].s;
            if (s->kind == SK_MEM) {    /* a drawer's listing (drawer.c) */
                UBYTE b[512];
                long off = 0, n;
                while ((n = src_get(s, off, b, sizeof b)) > 0 && Write(Output(), b, n) == n) off += n;
            } else {
                Seek(s->fh, 0, OFFSET_BEGINNING);
                copyout(s->fh, Output());
            }
            src_free(s);
        }
        if (!nfiles) copyout(in, Output());
        return rc;
    }
    if ((!nfiles || wantpipe) && !IsInteractive(in)) {
        Src *s = src_pipe(in);
        if (s) {
            memmove(&files[1], &files[0], sizeof(View) * (nfiles < MAXF ? nfiles : MAXF - 1));
            memset(&files[0], 0, sizeof(View));
            files[0].s = s;
            files[0].mline = -1;
            if (nfiles < MAXF) nfiles++;
        }
    }
    if (!IsInteractive(in)) {
        /* the keys come from the console the pipe's output goes to */
        conin = Open((STRPTR)"*", MODE_OLDFILE);
        if (!conin || !IsInteractive(conin)) {
            if (conin) Close(conin);
            for (i = 0; i < nfiles; i++) src_free(files[i].s);
            PutStr((STRPTR)"Maki: no console for the keys\n");
            return 20;
        }
        tui_console(conin);
    }
    keys_load();
    if (!tui_open()) {
        PutStr((STRPTR)"Maki: cannot use this console\n");
        for (i = 0; i < nfiles; i++) src_free(files[i].s);
        if (conin) Close(conin);
        return 20;
    }
    if (keysbad[0]) say(keysbad, 1);
    if (jump > 0) golinen(&files[0], jump);
    if (start[0]) {
        strcpy(findtext, start);
        phist_add(P_FIND, start);
        search(&files[0], 0);
    }
    while (!quitting) {
        View *v = V;
        Src *s = v->s;
        ULONG sigs = src_sigs(s), got = 0;
        long wait = -1;
        if (src_poll(s) && following) toend(v);
        if (following && s->kind == SK_FILE && src_grow(s)) {
            rsrc = NULL;
            toend(v);
        }
        if (following) toend(v);
        draw();
        if (following) wait = 500000;
        else if (s->scanned < s->size) wait = 30000;   /* idle: index on */
        if (!tui_wait(&e, wait, sigs, &got)) {
            if (!got && !following && s->scanned < s->size) src_scan(s, 262144);
            continue;
        }
        switch (e.type) {
        case TE_KEY: key(e.key); break;
        case TE_MOUSE: mouse(&e); break;
        case TE_RESIZE:
            tui_resized();
            clamp(V);
            break;
        }
    }
    tui_close();
    closehelp();
    for (i = 0; i < nfiles; i++) src_free(files[i].s);
    if (conin) Close(conin);
    (void)version;
    return rc;
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
    int r;
    if (argc == 0) return 20;           /* from Workbench: no terminal */
    if (!src_init()) return 20;
    lb = AllocVec(MAXLB + MAXC, MEMF_ANY);
    lc = AllocVec(MAXC * sizeof(LCell), MEMF_ANY);
    if (!lb || !lc) {
        if (lb) FreeVec(lb);
        if (lc) FreeVec(lc);
        src_exit();
        return 20;
    }
    lp = lb + MAXLB;
    if (have >= STACKSZ || !(bigstk = AllocVec(STACKSZ, MEMF_ANY)))
        r = smain(argc, argv);
    else {
        sargc = argc;
        sargv = argv;
        sss.stk_Lower = bigstk;
        sss.stk_Upper = (ULONG)(bigstk + STACKSZ);
        sss.stk_Pointer = (APTR)(bigstk + STACKSZ);
        StackSwap(&sss);
        runswapped();
        StackSwap(&sss);
        FreeVec(bigstk);
        r = sret;
    }
    FreeVec(lb);
    FreeVec(lc);
    src_exit();
    return r;
}
