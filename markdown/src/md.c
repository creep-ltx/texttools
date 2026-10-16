/* md - Markdown made into rows of coloured cells, ready for the screen.
 *
 * The blocks and inlines of CommonMark, with what GitHub adds (tables,
 * task lists, ~~strikethrough~~, bare links, > [!NOTE] alerts) and the
 * HTML that READMEs use (<img>, <br>, <b>, <a href>, comments). The text
 * is wrapped to a width; each row remembers the source line it came
 * from, so the editor and the view can follow each other. Links,
 * headings, pictures and task boxes are listed beside the rows.
 *
 * Blocks are found container by container: a quote's or a list item's
 * lines are taken out (their markers stripped) and read again as blocks
 * of their own, with the marker drawn in front of their rows (Lev).
 * Inlines: delimiters (* _ ~) and brackets go on stacks and are matched
 * as CommonMark's "process emphasis" does; their characters stay in the
 * cell list, marked deleted when they turn out to be markup. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include "markdown.h"
#include "md.h"

int md_unicode = 1, md_cellw, md_cellh, md_pictures = 1, md_maxpicrows = 20;

/* the colours, from CTerm's sixteen (its theme decides how they look) */
#define C_H1     12
#define C_H2     12
#define C_H3     14
#define C_H4     13
#define C_H5     11
#define C_H6     8
#define C_CODE   11
#define C_LINK   12
#define C_QUOTE  8
#define C_RULE   8
#define C_BOX    8
#define C_LABEL  11
#define C_BULLET 6
#define C_DONE   2
#define C_PIC    13
#define C_DIMT   8
static const UBYTE hlcol[] = { TC_DEFAULT, 8, 2, 12, 6, 3, 5, 11 };

#define MAXLEV  32
#define MAXDEPTH 24

/* ---- the document being made -------------------------------------------- */

static Doc *D;
static Buf *SB;
static int ENC;
static int W;                   /* the rows' width */
static const char *BASE;        /* the file (links and pictures are beside it) */
static UBYTE *isdef;            /* source lines that are link definitions */
static int depth;               /* containers deep (guards the recursion) */
static int listdepth;

static int grow_(void **p, long *cap, long need, long size)
{
    long nc;
    void *n;
    if (need <= *cap) return 1;
    nc = *cap ? *cap * 2 : 64;
    while (nc < need) nc *= 2;
    if (!(n = AllocVec(nc * size, MEMF_ANY))) {
        D->failed = 1;
        return 0;
    }
    if (*p) {
        memcpy(n, *p, *cap * size);
        FreeVec(*p);
    }
    *p = n;
    *cap = nc;
    return 1;
}
#define GROW(arr, n, cap, need) grow_((void **)&(arr), &(cap), (need), (long)sizeof(*(arr)))

static long pool_add(const char *s, long n)
{
    long at = D->npool;
    if (!GROW(D->pool, D->npool, D->cpool, D->npool + n + 1)) return 0;
    if (n) memcpy(D->pool + at, s, n);
    D->pool[at + n] = 0;
    D->npool += n + 1;
    return at;
}

const char *md_str(const Doc *d, long off)
{
    return d->pool && off >= 0 && off < d->npool ? d->pool + off : "";
}

static ULONG G(ULONG uni, ULONG asc)
{
    return md_unicode ? uni : asc;
}

/* ---- characters ---------------------------------------------------------- */

/* the code point at s[*i] in the file's encoding; *i moves past it */
static ULONG dec(const char *s, long n, long *i)
{
    UBYTE c = (UBYTE)s[*i];
    if (ENC == ENC_UTF8 && c >= 0xC2 && c <= 0xF4) {
        int k = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1, j;
        ULONG v = c & (0x3F >> k);
        if (*i + k < n) {
            for (j = 1; j <= k; j++)
                if (((UBYTE)s[*i + j] & 0xC0) != 0x80) break;
            if (j > k) {
                for (j = 1; j <= k; j++) v = v << 6 | ((UBYTE)s[*i + j] & 0x3F);
                *i += k + 1;
                return v;
            }
        }
    }
    (*i)++;
    return c;
}

static int enc8(ULONG c, char *o)
{
    if (c < 0x80) {
        o[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        o[0] = (char)(0xC0 | c >> 6);
        o[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000) {
        o[0] = (char)(0xE0 | c >> 12);
        o[1] = (char)(0x80 | (c >> 6 & 0x3F));
        o[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | c >> 18);
    o[1] = (char)(0x80 | (c >> 12 & 0x3F));
    o[2] = (char)(0x80 | (c >> 6 & 0x3F));
    o[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

static int isws(ULONG c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xA0;
}

static int ispunct_(ULONG c)
{
    if (c < 0x80) return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
                         (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
    return (c >= 0xA1 && c <= 0xBF) || c == 0xD7 || c == 0xF7 ||
           (c >= 0x2010 && c <= 0x2027) || (c >= 0x2030 && c <= 0x205E);
}

static int lowc(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int digit(int c)
{
    return c >= '0' && c <= '9';
}

static int alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* ---- source lines --------------------------------------------------------- */

typedef struct {
    const char *s;
    long len;
    long ln;                    /* the line in the Buf */
} SL;

/* leading white space in columns (a tab to the next multiple of 4) */
static int indent(const SL *l)
{
    long i;
    int col = 0;
    for (i = 0; i < l->len; i++) {
        if (l->s[i] == ' ') col++;
        else if (l->s[i] == '\t') col = (col / 4 + 1) * 4;
        else break;
    }
    return col;
}

/* the line with up to cols columns of white space taken off the front.
 * (No SL passed or returned by value anywhere in here: bebbo's gcc 6.5
 * got trimall(SL) -> SL wrong at -O1 and -Os - an empty line came back.) */
static void strip(SL *o, const SL *in, int cols)
{
    const char *s = in->s;
    long len = in->len;
    int col = 0;
    while (len && col < cols && (s[0] == ' ' || s[0] == '\t')) {
        col = s[0] == '\t' ? (col / 4 + 1) * 4 : col + 1;
        s++;
        len--;
    }
    o->s = s;
    o->len = len;
    o->ln = in->ln;
}

static void trimall(SL *l)
{
    const char *s = l->s;
    long len = l->len;
    while (len && (s[0] == ' ' || s[0] == '\t')) s++, len--;
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r')) len--;
    l->s = s;
    l->len = len;
}

static int blank(const SL *l)
{
    long i;
    for (i = 0; i < l->len; i++)
        if (l->s[i] != ' ' && l->s[i] != '\t' && l->s[i] != '\r') return 0;
    return 1;
}

/* ``` or ~~~ (3 or more): its character and count, the info string */
static int fence(const SL *l0, char *ch, int *cnt, SL *info)
{
    SL l;
    int n = 0;
    if (indent(l0) >= 4) return 0;
    strip(&l, l0, 3);
    if (!l.len || (l.s[0] != '`' && l.s[0] != '~')) return 0;
    while (n < l.len && l.s[n] == l.s[0]) n++;
    if (n < 3) return 0;
    if (info) {
        info->s = l.s + n;
        info->len = l.len - n;
        info->ln = l.ln;
        trimall(info);
        if (l.s[0] == '`' && memchr(info->s, '`', info->len)) return 0;
    }
    *ch = l.s[0];
    *cnt = n;
    return 1;
}

/* # Heading: its level and text */
static int atx(const SL *l0, SL *text)
{
    SL l;
    int n = 0;
    if (indent(l0) >= 4) return 0;
    strip(&l, l0, 3);
    while (n < l.len && l.s[n] == '#') n++;
    if (n < 1 || n > 6 || (n < l.len && l.s[n] != ' ' && l.s[n] != '\t')) return 0;
    if (text) {
        SL t;
        t.s = l.s + n;
        t.len = l.len - n;
        t.ln = l.ln;
        trimall(&t);
        /* a closing run of #s (after a space) goes */
        {
            long e = t.len;
            while (e > 0 && t.s[e - 1] == '#') e--;
            if (e == 0) t.len = 0;
            else if (e < t.len && (t.s[e - 1] == ' ' || t.s[e - 1] == '\t')) {
                t.len = e;
                trimall(&t);
            }
        }
        *text = t;
    }
    return n;
}

/* ---, ***, ___ (3 or more, spaces between allowed) */
static int hrule(const SL *l0)
{
    SL l;
    long i;
    int n = 0;
    char c;
    if (indent(l0) >= 4) return 0;
    strip(&l, l0, 3);
    if (!l.len) return 0;
    c = l.s[0];
    if (c != '-' && c != '*' && c != '_') return 0;
    for (i = 0; i < l.len; i++) {
        if (l.s[i] == c) n++;
        else if (l.s[i] != ' ' && l.s[i] != '\t' && l.s[i] != '\r') return 0;
    }
    return n >= 3;
}

/* === or --- under a paragraph: 1 or 2 */
static int setext(const SL *l0)
{
    SL l;
    long i = 0;
    char c;
    if (indent(l0) >= 4) return 0;
    strip(&l, l0, 3);
    trimall(&l);
    if (!l.len || (l.s[0] != '=' && l.s[0] != '-')) return 0;
    c = l.s[0];
    while (i < l.len && l.s[i] == c) i++;
    if (i < l.len) return 0;
    return c == '=' ? 1 : 2;
}

static int quotestart(const SL *l0)
{
    SL l;
    if (indent(l0) >= 4) return 0;
    strip(&l, l0, 3);
    return l.len && l.s[0] == '>';
}

typedef struct {
    char type;                  /* the bullet (- + *) or the ordered one's . or ) */
    long start;                 /* the ordered one's number */
    int width;                  /* columns to its content */
    int empty;                  /* nothing after the marker */
} Mark;

static int listmark(const SL *l0, Mark *m)
{
    int ind = indent(l0), w, sp = 0;
    SL l;
    long i = 0, num = 0;
    if (ind >= 4) return 0;
    strip(&l, l0, 3);
    if (!l.len) return 0;
    if (l.s[0] == '-' || l.s[0] == '+' || l.s[0] == '*') {
        m->type = l.s[0];
        i = 1;
    } else {
        while (i < l.len && i < 9 && digit(l.s[i])) num = num * 10 + (l.s[i++] - '0');
        if (!i || i >= l.len || (l.s[i] != '.' && l.s[i] != ')')) return 0;
        m->type = l.s[i++];
    }
    m->start = num;
    if (i < l.len && l.s[i] != ' ' && l.s[i] != '\t') return 0;
    w = ind + (int)i;
    {   /* the spaces after the marker: 1 to 4 belong to it */
        long j = i;
        int col = w;
        while (j < l.len && (l.s[j] == ' ' || l.s[j] == '\t')) {
            col = l.s[j] == '\t' ? (col / 4 + 1) * 4 : col + 1;
            j++;
        }
        sp = col - w;
        m->empty = j >= l.len || l.s[j] == '\r';
        if (m->empty || sp > 4) sp = 1;
        else if (sp < 1) sp = 1;
    }
    m->width = w + sp;
    return 1;
}

/* an HTML block starts: a tag, a closing tag, a comment */
static int htmlstart(const SL *l0)
{
    SL l;
    if (indent(l0) >= 4) return 0;
    strip(&l, l0, 3);
    if (l.len < 2 || l.s[0] != '<') return 0;
    if (l.s[1] == '!' || l.s[1] == '?') return 1;
    if (l.s[1] == '/') return l.len > 2 && alpha((UBYTE)l.s[2]);
    return alpha((UBYTE)l.s[1]);
}

/* the block HTML tags that may break into a paragraph */
static int htmlinterrupts(const SL *l0)
{
    static const char *const tags[] = { "div", "p", "table", "details", "summary", "h1", "h2",
                                        "h3", "h4", "h5", "h6", "center", "ul", "ol", "pre",
                                        "hr", "blockquote", NULL };
    SL l;
    int i, k;
    if (!htmlstart(l0)) return 0;
    strip(&l, l0, 3);
    if (l.s[1] == '!') return 1;
    k = l.s[1] == '/' ? 2 : 1;
    for (i = 0; tags[i]; i++) {
        int n = (int)strlen(tags[i]), j;
        if (l.len < k + n) continue;
        for (j = 0; j < n && lowc((UBYTE)l.s[k + j]) == tags[i][j]; j++) ;
        if (j == n && (l.len == k + n || !alpha((UBYTE)l.s[k + n]))) return 1;
    }
    return 0;
}

/* a line that starts another block (ends a paragraph or a lazy line) */
static int interrupts(const SL *l)
{
    char ch;
    int cnt;
    Mark m;
    if (blank(l)) return 1;
    if (indent(l) >= 4) return 0;
    if (fence(l, &ch, &cnt, NULL) || atx(l, NULL) || hrule(l) || quotestart(l)) return 1;
    if (listmark(l, &m) && !m.empty && (m.type == '-' || m.type == '+' || m.type == '*' || m.start == 1))
        return 1;
    return htmlinterrupts(l);
}

/* ---- link reference definitions: [label]: url "title" -------------------- */

typedef struct { long label, url, title; } Ref;
static Ref *refs;
static long nrefs, crefs;

/* the label as compared: lower case, white space as one space */
static long normlabel(const char *s, long n, char *o, long max)
{
    long i, k = 0;
    int sp = 0;
    for (i = 0; i < n && k < max - 1; i++) {
        UBYTE c = (UBYTE)s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            sp = 1;
            continue;
        }
        if (sp && k) o[k++] = ' ';
        sp = 0;
        if (k < max - 1) o[k++] = (char)lowc(c);
    }
    o[k] = 0;
    return k;
}

static int finddef(const char *label, long n, long *url, long *title)
{
    char a[200];
    long i;
    normlabel(label, n, a, sizeof(a));
    if (!a[0]) return 0;
    for (i = 0; i < nrefs; i++)
        if (!strcmp(md_str(D, refs[i].label), a)) {
            *url = refs[i].url;
            *title = refs[i].title;
            return 1;
        }
    return 0;
}

/* a line that is a definition: noted, and marked so no paragraph shows it */
static void trydef(const SL *lp)
{
    long i, ls, le, us, ue, ts = 0, te = 0;
    char a[200];
    SL l = *lp;
    while (l.len && (l.s[0] == '>' || l.s[0] == ' ' || l.s[0] == '\t')) l.s++, l.len--;
    if (l.len < 4 || l.s[0] != '[' || l.s[1] == '^') return;
    for (i = 1; i < l.len && l.s[i] != ']'; i++)
        if (l.s[i] == '[' || (l.s[i] == '\\' && ++i >= l.len)) return;
    if (i + 1 >= l.len || l.s[i + 1] != ':') return;
    ls = 1;
    le = i;
    i += 2;
    while (i < l.len && (l.s[i] == ' ' || l.s[i] == '\t')) i++;
    if (i >= l.len) return;
    if (l.s[i] == '<') {
        us = ++i;
        while (i < l.len && l.s[i] != '>') i++;
        if (i >= l.len) return;
        ue = i++;
    } else {
        us = i;
        while (i < l.len && l.s[i] != ' ' && l.s[i] != '\t' && l.s[i] != '\r') i++;
        ue = i;
    }
    while (i < l.len && (l.s[i] == ' ' || l.s[i] == '\t')) i++;
    if (i < l.len && (l.s[i] == '"' || l.s[i] == '\'' || l.s[i] == '(')) {
        char q = l.s[i] == '(' ? ')' : l.s[i];
        ts = ++i;
        while (i < l.len && l.s[i] != q) i++;
        if (i >= l.len) return;
        te = i++;
        while (i < l.len && (l.s[i] == ' ' || l.s[i] == '\t' || l.s[i] == '\r')) i++;
    }
    if (i < l.len && l.s[i] != '\r') return;
    if (!GROW(refs, nrefs, crefs, nrefs + 1)) return;
    normlabel(l.s + ls, le - ls, a, sizeof(a));
    if (!a[0]) return;
    refs[nrefs].label = pool_add(a, (long)strlen(a));
    refs[nrefs].url = pool_add(l.s + us, ue - us);
    refs[nrefs].title = te > ts ? pool_add(l.s + ts, te - ts) : 0;
    nrefs++;
    if (isdef) isdef[l.ln] = 1;
}

/* ---- rows ------------------------------------------------------------------- */

typedef struct {
    RCell f[12], r[12];         /* in front of its first row, of the rest */
    int w;
    int used;                   /* the first row is out */
} Lev;
static Lev lv[MAXLEV];
static int nlv;
static int gap;                 /* a blank row is owed before the next row */
static long lastsrc;

static int prew(void)
{
    int i, w = 0;
    for (i = 0; i < nlv; i++) w += lv[i].w;
    return w;
}

static int avail(void)
{
    int a = W - prew();
    return a > 4 ? a : 4;
}

static void setcell(RCell *c, ULONG ch, int fg, int bg, int at)
{
    c->c = ch;
    c->fg = (UBYTE)fg;
    c->bg = (UBYTE)bg;
    c->at = (UBYTE)at;
    c->fl = 0;
}

static void pushlev(const RCell *f, const RCell *r, int w)
{
    if (nlv >= MAXLEV || w > 12) return;
    memcpy(lv[nlv].f, f, w * sizeof(RCell));
    memcpy(lv[nlv].r, r, w * sizeof(RCell));
    lv[nlv].w = w;
    lv[nlv].used = 0;
    nlv++;
}

static void poplev(void)
{
    if (nlv) nlv--;
}

static long newrow(long src, int isgap);

/* a row begins: the prefixes in front; returns its index (-1: no memory) */
static long rowstart(long src)
{
    if (gap) {
        gap = 0;
        newrow(lastsrc, 1);
    }
    return newrow(src, 0);
}

static long newrow(long src, int isgap)
{
    RRow *r;
    int i, w = prew();
    if (!GROW(D->rows, D->nrows, D->crows, D->nrows + 1)) return -1;
    if (!GROW(D->cells, D->ncells, D->ccells, D->ncells + w + 1)) return -1;
    r = &D->rows[D->nrows];
    r->cell = D->ncells;
    r->src = src;
    r->len = 0;
    r->pic = -1;
    for (i = 0; i < nlv; i++) {
        const RCell *p;
        if (isgap) {    /* a blank row: a level not begun yet shows nothing */
            if (!lv[i].used) {
                int k;
                for (k = 0; k < lv[i].w; k++) setcell(&D->cells[D->ncells + r->len + k], ' ',
                                                       TC_DEFAULT, TC_DEFAULT, 0);
                r->len += (short)lv[i].w;
                continue;
            }
            p = lv[i].r;
        } else if (!lv[i].used) {
            p = lv[i].f;
            lv[i].used = 1;
        } else
            p = lv[i].r;
        memcpy(&D->cells[D->ncells + r->len], p, lv[i].w * sizeof(RCell));
        r->len += (short)lv[i].w;
    }
    D->ncells += r->len;
    lastsrc = src;
    return D->nrows++;
}

/* a cell onto the last row */
static void rowput(ULONG c, int fg, int bg, int at)
{
    if (!D->nrows || D->rows[D->nrows - 1].len >= 2000) return;
    if (!GROW(D->cells, D->ncells, D->ccells, D->ncells + 1)) return;
    setcell(&D->cells[D->ncells++], c, fg, bg, at);
    D->rows[D->nrows - 1].len++;
}

static void rowspan(long row, int c0, int c1, int link)
{
    if (!link || c1 <= c0) return;
    if (D->nspans && D->spans[D->nspans - 1].row == row && D->spans[D->nspans - 1].link == link &&
        D->spans[D->nspans - 1].c1 == c0) {
        D->spans[D->nspans - 1].c1 = (short)c1;
        return;
    }
    if (!GROW(D->spans, D->nspans, D->cspans, D->nspans + 1)) return;
    D->spans[D->nspans].row = row;
    D->spans[D->nspans].c0 = (short)c0;
    D->spans[D->nspans].c1 = (short)c1;
    D->spans[D->nspans].link = (short)link;
    D->nspans++;
}

static int rowlen(void)
{
    return D->nrows ? D->rows[D->nrows - 1].len : 0;
}

/* ---- inlines ------------------------------------------------------------------ */

typedef struct {
    ULONG c;
    UBYTE fg, bg, at, fl;
    UWORD link, sl;
} IC;
#define IF_DEL  1               /* markup after all: not shown */
#define IF_BRK  2               /* a hard line break */
#define IF_KEEP 4               /* a space that stays (code) */
#define IF_PIC  8               /* a picture's mark */
#define IF_ALT  16              /* a picture's words */

static IC *ic;
static long nic, cic;
static int ifg = TC_DEFAULT, iat;   /* the style of the text being read */
static int isl;                     /* its source line, from the block's first */
static int hb, hi, hs, hc, hk, hu;  /* inside HTML <b> <i> <s> <code> <kbd> <u> */

static void icput(ULONG c, int fg, int at, int fl)
{
    IC *p;
    if (!GROW(ic, nic, cic, nic + 1)) return;
    p = &ic[nic++];
    p->c = c;
    p->fg = (UBYTE)fg;
    p->bg = TC_DEFAULT;
    p->at = (UBYTE)at;
    p->fl = (UBYTE)fl;
    p->link = 0;
    p->sl = (UWORD)isl;
}

static int curat(void)
{
    return iat | (hb ? TA_BOLD : 0) | (hi ? TA_ITAL : 0) | (hs ? TA_STRIKE : 0) |
           (hk ? TA_REV : 0) | (hu ? TA_UNDER : 0);
}

static int curfg(void)
{
    return hc ? C_CODE : ifg;
}

static void text(ULONG c)
{
    icput(c, curfg(), curat(), 0);
}

static void texts(const char *s)
{
    while (*s) text((UBYTE)*s++);
}

/* the paragraph's text and where its lines start */
static char *pt;
static long npt, cpt;
static long *pls;
static long npls, cpls;
static long plcur;

static void slat(long i)
{
    if (plcur > npls - 1 || (plcur && pls[plcur] > i)) plcur = 0;
    while (plcur + 1 < npls && pls[plcur + 1] <= i) plcur++;
    isl = (int)plcur;
}

typedef struct { long pos; int cnt, orig; char ch; UBYTE open, close; } Dl;
typedef struct { long pos, src; int img, dbot, active; } Br;
typedef struct { long pos; long url, alt; int wpx; } Im;
#define MAXDL 256
#define MAXBR 64
#define MAXIM 64
static Dl dl[MAXDL];
static int ndl;
static Br br[MAXBR];
static int nbr;
static Im im[MAXIM];
static int nim;
static long hlink_at = -1, hlink_url;   /* an HTML <a href> open */

static int addlink(long url, long title)
{
    if (D->nlinks >= 32000 || !GROW(D->links, D->nlinks, D->clinks, D->nlinks + 1)) return 0;
    D->links[D->nlinks].url = url;
    D->links[D->nlinks].title = title;
    return (int)++D->nlinks;
}

static void linkcells(long a, long z, int id)
{
    long j;
    for (j = a; j < z; j++) {
        if (ic[j].link) continue;           /* a link inside keeps its own */
        ic[j].link = (UWORD)id;
        if (!(ic[j].fl & (IF_PIC | IF_ALT))) {
            ic[j].fg = C_LINK;
            ic[j].at |= TA_UNDER;
        }
    }
}

/* CommonMark's "process emphasis", for the delimiters from bot up */
static void emph(int bot)
{
    int c, o;
    for (c = bot; c < ndl; c++) {
        Dl *cl = &dl[c];
        while (cl->close && cl->cnt > 0) {
            Dl *op = NULL;
            int k;
            for (o = c - 1; o >= bot; o--) {
                Dl *p = &dl[o];
                if (p->ch != cl->ch || !p->open || p->cnt <= 0) continue;
                if (cl->ch == '~') {
                    if (p->cnt != cl->cnt) continue;
                } else if ((p->close || cl->open) && (p->orig + cl->orig) % 3 == 0 &&
                           !(p->orig % 3 == 0 && cl->orig % 3 == 0))
                    continue;
                op = p;
                break;
            }
            if (!op) break;
            k = cl->ch == '~' ? cl->cnt : (op->cnt >= 2 && cl->cnt >= 2 ? 2 : 1);
            {
                long a = op->pos + op->cnt, z = cl->pos, j;
                int at = cl->ch == '~' ? TA_STRIKE : k == 2 ? TA_BOLD : TA_ITAL;
                for (j = op->pos + op->cnt - k; j < op->pos + op->cnt; j++) ic[j].fl |= IF_DEL;
                for (j = cl->pos; j < cl->pos + k; j++) ic[j].fl |= IF_DEL;
                for (j = a; j < z; j++) ic[j].at |= (UBYTE)at;
                op->cnt -= k;
                cl->cnt -= k;
                cl->pos += k;
                for (j = o + 1; j < c; j++) dl[j].cnt = 0;
            }
        }
    }
    ndl = bot;
}

static ULONG prevch(const char *s, long i)
{
    long j = i - 1;
    if (i <= 0) return '\n';
    if (ENC == ENC_UTF8)
        while (j > 0 && ((UBYTE)s[j] & 0xC0) == 0x80 && i - j < 4) j--;
    {
        long k = j;
        return dec(s, i, &k);
    }
}

/* ( dest "title" ) after a ]: *end past the ), the parts */
static int dest(const char *s, long n, long j, long *end, long *d0, long *d1, long *t0, long *t1)
{
    int depth_ = 0;
    if (j >= n || s[j] != '(') return 0;
    j++;
    while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\n')) j++;
    *t0 = *t1 = 0;
    if (j < n && s[j] == '<') {
        *d0 = ++j;
        while (j < n && s[j] != '>' && s[j] != '\n' && s[j] != '<') {
            if (s[j] == '\\' && j + 1 < n) j++;
            j++;
        }
        if (j >= n || s[j] != '>') return 0;
        *d1 = j++;
    } else {
        *d0 = j;
        while (j < n && (UBYTE)s[j] > ' ') {
            if (s[j] == '\\' && j + 1 < n) {
                j += 2;
                continue;
            }
            if (s[j] == '(') depth_++;
            else if (s[j] == ')') {
                if (!depth_) break;
                depth_--;
            }
            j++;
        }
        *d1 = j;
    }
    while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\n')) j++;
    if (j < n && (s[j] == '"' || s[j] == '\'' || s[j] == '(') && j > *d1) {
        char q = s[j] == '(' ? ')' : s[j];
        *t0 = ++j;
        while (j < n && s[j] != q) {
            if (s[j] == '\\' && j + 1 < n) j++;
            j++;
        }
        if (j >= n) return 0;
        *t1 = j++;
        while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\n')) j++;
    }
    if (j >= n || s[j] != ')') return 0;
    *end = j + 1;
    return 1;
}

/* a string with backslash escapes taken out, into the pool */
static long pool_unesc(const char *s, long n)
{
    char tmp[512];
    long i, k = 0;
    for (i = 0; i < n && k < (long)sizeof(tmp) - 1; i++) {
        if (s[i] == '\\' && i + 1 < n && ispunct_((UBYTE)s[i + 1])) i++;
        tmp[k++] = s[i];
    }
    return pool_add(tmp, k);
}

/* the words of cells a..z as UTF-8 (a picture's alt, a heading) */
static long plain(long a, long z, char *o, long max)
{
    long j, k = 0;
    for (j = a; j < z && k < max - 5; j++) {
        if (ic[j].fl & (IF_DEL | IF_PIC)) continue;
        if (ic[j].fl & IF_BRK) {
            o[k++] = ' ';
            continue;
        }
        k += enc8(ic[j].c, o + k);
    }
    o[k] = 0;
    return k;
}

/* an image found: its mark, its words styled */
static void image(long markpos, long url, long alt, int wpx)
{
    long j;
    ic[markpos].c = G(0x2592, '#');
    ic[markpos].fl = IF_PIC;
    ic[markpos].fg = C_PIC;
    ic[markpos].at = 0;
    for (j = markpos + 1; j < nic; j++) {
        if (!(ic[j].fl & IF_DEL)) ic[j].fl |= IF_ALT;
        ic[j].fg = C_PIC;
        ic[j].at |= TA_ITAL;
    }
    if (nim < MAXIM) {
        im[nim].pos = markpos;
        im[nim].url = url;
        im[nim].alt = alt;
        im[nim].wpx = wpx;
        nim++;
    }
}

static const struct { const char *n; UWORD c; } ents[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
    { "nbsp", 0xA0 }, { "copy", 0xA9 }, { "reg", 0xAE }, { "trade", 0x2122 },
    { "hellip", 0x2026 }, { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "laquo", 0xAB },
    { "raquo", 0xBB }, { "middot", 0xB7 }, { "bull", 0x2022 }, { "deg", 0xB0 },
    { "plusmn", 0xB1 }, { "times", 0xD7 }, { "divide", 0xF7 }, { "larr", 0x2190 },
    { "rarr", 0x2192 }, { "uarr", 0x2191 }, { "darr", 0x2193 }, { "harr", 0x2194 },
    { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
    { "sect", 0xA7 }, { "para", 0xB6 }, { "euro", 0x20AC }, { "pound", 0xA3 },
    { "check", 0x2713 }, { "auml", 0xE4 }, { "ouml", 0xF6 }, { "aring", 0xE5 },
    { "Auml", 0xC4 }, { "Ouml", 0xD6 }, { "Aring", 0xC5 }, { "uuml", 0xFC },
    { "eacute", 0xE9 }, { "shy", 0xAD }, { "ensp", ' ' }, { "emsp", ' ' }, { "thinsp", ' ' },
    { NULL, 0 }
};

/* &name; &#123; &#x1F; at s[i]: the code point, *len its length; 0 none */
static ULONG entity(const char *s, long n, long i, long *len)
{
    long j = i + 1;
    ULONG v = 0;
    int k;
    if (j < n && s[j] == '#') {
        int hex = 0, dg = 0;
        j++;
        if (j < n && (s[j] == 'x' || s[j] == 'X')) hex = 1, j++;
        for (; j < n && dg < 7; j++, dg++) {
            int c = (UBYTE)s[j], d;
            if (digit(c)) d = c - '0';
            else if (hex && lowc(c) >= 'a' && lowc(c) <= 'f') d = lowc(c) - 'a' + 10;
            else break;
            v = v * (hex ? 16 : 10) + d;
        }
        if (!dg || j >= n || s[j] != ';') return 0;
        *len = j + 1 - i;
        return v && v < 0x110000 ? v : 0xFFFD;
    }
    for (k = 0; ents[k].n; k++) {
        int l = (int)strlen(ents[k].n);
        if (j + l < n && !strncmp(s + j, ents[k].n, l) && s[j + l] == ';') {
            *len = l + 2;
            return ents[k].c;
        }
    }
    return 0;
}

/* an attribute's value in a tag (s[a..z)): *v0, *v1 */
static int tagattr(const char *s, long a, long z, const char *name, long *v0, long *v1)
{
    int nl = (int)strlen(name);
    long j;
    for (j = a; j + nl < z; j++) {
        int k;
        if (j > a && s[j - 1] != ' ' && s[j - 1] != '\t' && s[j - 1] != '\n') continue;
        for (k = 0; k < nl && lowc((UBYTE)s[j + k]) == name[k]; k++) ;
        if (k < nl) continue;
        k = (int)(j + nl);
        while (k < z && s[k] == ' ') k++;
        if (k >= z || s[k] != '=') continue;
        k++;
        while (k < z && s[k] == ' ') k++;
        if (k < z && (s[k] == '"' || s[k] == '\'')) {
            char q = s[k];
            *v0 = ++k;
            while (k < z && s[k] != q) k++;
            *v1 = k;
        } else {
            *v0 = k;
            while (k < z && s[k] != ' ' && s[k] != '>' && s[k] != '/') k++;
            *v1 = k;
        }
        return 1;
    }
    return 0;
}

static int tagis(const char *s, long n, const char *name)
{
    int l = (int)strlen(name), k;
    if (n < l) return 0;
    for (k = 0; k < l; k++)
        if (lowc((UBYTE)s[k]) != name[k]) return 0;
    return n == l;
}

/* an inline HTML tag at s[i] ('<'): handled; returns past it, or i if it is none */
static long htmltag(const char *s, long n, long i)
{
    long j = i + 1, t0, t1, end;
    int close = 0;
    if (j + 3 <= n && !strncmp(s + j, "!--", 3)) {
        for (j += 3; j + 2 < n; j++)
            if (s[j] == '-' && s[j + 1] == '-' && s[j + 2] == '>') return j + 3;
        return n;
    }
    if (j < n && s[j] == '/') close = 1, j++;
    if (j >= n || !alpha((UBYTE)s[j])) return i;
    t0 = j;
    while (j < n && (alpha((UBYTE)s[j]) || digit((UBYTE)s[j]) || s[j] == '-')) j++;
    t1 = j;
    /* to the '>', past quoted values */
    while (j < n && s[j] != '>') {
        if (s[j] == '"' || s[j] == '\'') {
            char q = s[j++];
            while (j < n && s[j] != q) j++;
        } else if (s[j] == '<')
            return i;
        if (j < n) j++;
    }
    if (j >= n) return i;
    end = j + 1;
    {
        const char *tn = s + t0;
        long tl = t1 - t0;
        int d = close ? -1 : 1;
        if (tagis(tn, tl, "br")) icput('\n', ifg, iat, IF_BRK);
        else if (tagis(tn, tl, "b") || tagis(tn, tl, "strong")) hb = hb + d < 0 ? 0 : hb + d;
        else if (tagis(tn, tl, "i") || tagis(tn, tl, "em")) hi = hi + d < 0 ? 0 : hi + d;
        else if (tagis(tn, tl, "s") || tagis(tn, tl, "del") || tagis(tn, tl, "strike"))
            hs = hs + d < 0 ? 0 : hs + d;
        else if (tagis(tn, tl, "code") || tagis(tn, tl, "tt") || tagis(tn, tl, "samp"))
            hc = hc + d < 0 ? 0 : hc + d;
        else if (tagis(tn, tl, "kbd")) hk = hk + d < 0 ? 0 : hk + d;
        else if (tagis(tn, tl, "u") || tagis(tn, tl, "ins")) hu = hu + d < 0 ? 0 : hu + d;
        else if (tagis(tn, tl, "a")) {
            long v0, v1;
            if (!close && tagattr(s, t1, j, "href", &v0, &v1)) {
                hlink_at = nic;
                hlink_url = pool_add(s + v0, v1 - v0);
            } else if (close && hlink_at >= 0) {
                int id = addlink(hlink_url, 0);
                if (id) linkcells(hlink_at, nic, id);
                hlink_at = -1;
            }
        } else if (tagis(tn, tl, "img") && !close) {
            long v0, v1, a0 = 0, a1 = 0, url, alt;
            int wpx = 0;
            char tmp[256];
            long at = nic;
            if (tagattr(s, t1, j, "src", &v0, &v1)) {
                url = pool_add(s + v0, v1 - v0);
                if (tagattr(s, t1, j, "width", &a0, &a1))
                    while (a0 < a1 && digit((UBYTE)s[a0])) wpx = wpx * 10 + (s[a0++] - '0');
                icput('!', ifg, iat, 0);
                if (tagattr(s, t1, j, "alt", &a0, &a1) && a1 > a0) {
                    long k = a0;
                    while (k < a1) text(dec(s, a1, &k));
                } else
                    texts("picture");
                alt = pool_add(tmp, plain(at + 1, nic, tmp, sizeof(tmp)));
                image(at, url, alt, wpx);
            }
        } else if ((tagis(tn, tl, "p") || tagis(tn, tl, "div") || tagis(tn, tl, "li") ||
                    tagis(tn, tl, "tr") || tagis(tn, tl, "h1") || tagis(tn, tl, "h2") ||
                    tagis(tn, tl, "h3")) && nic && !(ic[nic - 1].fl & IF_BRK))
            icput('\n', ifg, iat, IF_BRK);
    }
    return end;
}

/* <http://...> or <someone@where>: a link; returns past it, or i */
static long autolink(const char *s, long n, long i)
{
    long j = i + 1, k;
    int scheme = 0, at = 0;
    for (k = j; k < n && s[k] != '>'; k++)
        if ((UBYTE)s[k] <= ' ' || s[k] == '<') return i;
    if (k >= n || k == j) return i;
    {
        long m = j;
        while (m < k && (alpha((UBYTE)s[m]) || (m > j && (digit((UBYTE)s[m]) || s[m] == '+' ||
                                                           s[m] == '.' || s[m] == '-'))))
            m++;
        scheme = m - j >= 2 && m < k && s[m] == ':';
        at = !scheme && memchr(s + j, '@', k - j) != NULL;
    }
    if (!scheme && !at) return i;
    {
        long a = nic, url;
        int id;
        char tmp[300];
        if (at) {
            long l = k - j < 250 ? k - j : 250;
            memcpy(tmp, "mailto:", 7);
            memcpy(tmp + 7, s + j, l);
            url = pool_add(tmp, 7 + l);
        } else
            url = pool_add(s + j, k - j);
        while (j < k) text(dec(s, k, &j));
        if ((id = addlink(url, 0)) != 0) linkcells(a, nic, id);
    }
    return k + 1;
}

/* a bare http://, https:// or www. address (GitHub's): returns past it, or i */
static long barelink(const char *s, long n, long i)
{
    long j, e;
    int www = 0, par = 0;
    if (i > 0) {
        char p = s[i - 1];
        if (p != ' ' && p != '\n' && p != '\t' && p != '(' && p != '*' && p != '_' && p != '~')
            return i;
    }
    if (n - i >= 7 && !strncmp(s + i, "http://", 7)) j = i + 7;
    else if (n - i >= 8 && !strncmp(s + i, "https://", 8)) j = i + 8;
    else if (n - i >= 4 && !strncmp(s + i, "www.", 4)) j = i + 4, www = 1;
    else return i;
    e = j;
    while (e < n && (UBYTE)s[e] > ' ' && s[e] != '<') e++;
    while (e > j && strchr("?!.,:*_~'\";", s[e - 1])) e--;
    for (j = i; j < e; j++)
        if (s[j] == '(') par++;
        else if (s[j] == ')') par--;
    while (par < 0 && e > i && s[e - 1] == ')') e--, par++;
    if (e - i < (www ? 6 : 10)) return i;
    {
        long a = nic, url, k = i;
        int id;
        char tmp[520];
        if (www) {
            long l = e - i < 500 ? e - i : 500;
            memcpy(tmp, "http://", 7);
            memcpy(tmp + 7, s + i, l);
            url = pool_add(tmp, 7 + l);
        } else
            url = pool_add(s + i, e - i);
        while (k < e) text(dec(s, e, &k));
        if ((id = addlink(url, 0)) != 0) linkcells(a, nic, id);
    }
    return e;
}

/* the text s[0..n) (a paragraph's lines, '\n' between) into cells */
static void inl(const char *s, long n)
{
    long i = 0;
    int dbot = ndl, bbot = nbr;
    plcur = 0;
    while (i < n) {
        UBYTE c = (UBYTE)s[i];
        slat(i);
        if (c == '\\' && i + 1 < n) {
            if (s[i + 1] == '\n') {
                icput('\n', ifg, iat, IF_BRK);
                i += 2;
                while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
                continue;
            }
            if (ispunct_((UBYTE)s[i + 1])) {
                text((UBYTE)s[i + 1]);
                i += 2;
                continue;
            }
        }
        if (c == '`') {
            long m = 0, j, e = -1;
            while (i + m < n && s[i + m] == '`') m++;
            for (j = i + m; j < n; j++)
                if (s[j] == '`') {
                    long r = 0;
                    while (j + r < n && s[j + r] == '`') r++;
                    if (r == m) {
                        e = j;
                        break;
                    }
                    j += r - 1;
                }
            if (e < 0) {
                for (j = 0; j < m; j++) text('`');
                i += m;
                continue;
            }
            {
                long a = i + m, z = e, k;
                int allsp = 1;
                for (k = a; k < z; k++)
                    if (s[k] != ' ' && s[k] != '\n') allsp = 0;
                if (!allsp && z - a >= 2 && (s[a] == ' ' || s[a] == '\n') &&
                    (s[z - 1] == ' ' || s[z - 1] == '\n'))
                    a++, z--;
                k = a;
                while (k < z) {
                    ULONG ch;
                    slat(k);
                    ch = dec(s, z, &k);
                    icput(ch == '\n' ? ' ' : ch, C_CODE, curat(), ch == ' ' || ch == '\n' ? IF_KEEP : 0);
                }
                i = e + m;
            }
            continue;
        }
        if (c == '*' || c == '_' || c == '~') {
            long m = 0, k;
            ULONG pc = prevch(s, i), nc;
            int lf, rf, po, pn;
            while (i + m < n && (UBYTE)s[i + m] == c) m++;
            k = i + m;
            nc = k < n ? dec(s, n, &k) : '\n';
            lf = !isws(nc) && (!ispunct_(nc) || isws(pc) || ispunct_(pc));
            rf = !isws(pc) && (!ispunct_(pc) || isws(nc) || ispunct_(nc));
            po = ispunct_(pc);
            pn = ispunct_(nc);
            for (k = 0; k < m; k++) text(c);
            if ((c == '~' && m > 2) || ndl >= MAXDL) {
                i += m;
                continue;
            }
            dl[ndl].pos = nic - m;
            dl[ndl].cnt = dl[ndl].orig = (int)m;
            dl[ndl].ch = (char)c;
            if (c == '_') {
                dl[ndl].open = (UBYTE)(lf && (!rf || po));
                dl[ndl].close = (UBYTE)(rf && (!lf || pn));
            } else {
                dl[ndl].open = (UBYTE)lf;
                dl[ndl].close = (UBYTE)rf;
            }
            if (dl[ndl].open || dl[ndl].close) ndl++;
            i += m;
            continue;
        }
        if ((c == '[' || (c == '!' && i + 1 < n && s[i + 1] == '[')) && nbr < MAXBR) {
            int img = c == '!';
            if (img) text('!');
            text('[');
            br[nbr].pos = nic - 1;
            br[nbr].src = i + (img ? 2 : 1);
            br[nbr].img = img;
            br[nbr].dbot = ndl;
            br[nbr].active = 1;
            nbr++;
            i += img ? 2 : 1;
            continue;
        }
        if (c == ']' && nbr > bbot) {
            Br *b = &br[nbr - 1];
            long end = i + 1, d0, d1, t0, t1, url = 0, title = 0;
            int found = 0;
            if (!b->active) {
                nbr--;
                text(']');
                i++;
                continue;
            }
            if (dest(s, n, i + 1, &end, &d0, &d1, &t0, &t1)) {
                url = pool_unesc(s + d0, d1 - d0);
                title = t1 > t0 ? pool_unesc(s + t0, t1 - t0) : 0;
                found = 1;
            } else {
                long l0 = b->src, l1 = i;
                end = i + 1;
                if (i + 1 < n && s[i + 1] == '[') {
                    long k = i + 2;
                    while (k < n && s[k] != ']' && s[k] != '[') k++;
                    if (k < n && s[k] == ']') {
                        if (k > i + 2) l0 = i + 2, l1 = k;
                        end = k + 1;
                    }
                }
                if (s[b->src] != '^' && finddef(s + l0, l1 - l0, &url, &title)) found = 1;
                else end = i + 1;
            }
            if (!found) {
                nbr--;
                text(']');
                i++;
                continue;
            }
            emph(b->dbot);
            ic[b->pos].fl |= IF_DEL;
            if (b->img) {
                char tmp[256];
                long alt = pool_add(tmp, plain(b->pos + 1, nic, tmp, sizeof(tmp)));
                image(b->pos - 1, url, alt, 0);
            } else {
                int id = addlink(url, title), k;
                if (id) linkcells(b->pos + 1, nic, id);
                for (k = bbot; k < nbr - 1; k++)
                    if (!br[k].img) br[k].active = 0;
            }
            nbr--;
            i = end;
            continue;
        }
        if (c == '<') {
            long e = autolink(s, n, i);
            if (e == i) e = htmltag(s, n, i);
            if (e != i) {
                i = e;
                continue;
            }
        }
        if (c == '&') {
            long l;
            ULONG v = entity(s, n, i, &l);
            if (v) {
                text(v);
                i += l;
                continue;
            }
        }
        if (c == '\n') {
            long sp = 0;
            while (nic - sp > 0 && ic[nic - sp - 1].c == ' ' && !(ic[nic - sp - 1].fl & IF_KEEP)) sp++;
            nic -= sp;
            if (sp >= 2) icput('\n', ifg, iat, IF_BRK);
            else icput(' ', curfg(), curat(), 0);
            i++;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            continue;
        }
        if (c == 'h' || c == 'w') {
            long e = barelink(s, n, i);
            if (e != i) {
                i = e;
                continue;
            }
        }
        if (c == '\t') {
            text(' ');
            i++;
            continue;
        }
        text(dec(s, n, &i));
    }
    emph(dbot);
    nbr = bbot;
    hlink_at = -1;
}

/* the paragraph text from lines (each stripped in front), '\n' between */
static void ptext(const SL *L, long n)
{
    long i;
    npt = 0;
    npls = 0;
    for (i = 0; i < n; i++) {
        SL l = L[i];
        while (l.len && (l.s[0] == ' ' || l.s[0] == '\t')) l.s++, l.len--;
        if (i == n - 1) trimall(&l);
        else
            while (l.len && l.s[l.len - 1] == '\r') l.len--;
        if (!GROW(pt, npt, cpt, npt + l.len + 2) || !GROW(pls, npls, cpls, npls + 1)) return;
        pls[npls++] = npt;
        memcpy(pt + npt, l.s, l.len);
        npt += l.len;
        if (i < n - 1) pt[npt++] = '\n';
    }
}

/* ---- wrapping ------------------------------------------------------------------ */

typedef struct { long a; int n, sl; } WL;
static IC *wc;
static long nwc, cwc;
static WL *wl;
static long nwl, cwl;

static void wl_new(int sl)
{
    if (!GROW(wl, nwl, cwl, nwl + 1)) return;
    wl[nwl].a = nwc;
    wl[nwl].n = 0;
    wl[nwl].sl = sl;
    nwl++;
}

static void wl_add(const IC *c)
{
    if (!nwl || !GROW(wc, nwc, cwc, nwc + 1)) return;
    wc[nwc++] = *c;
    wl[nwl - 1].n++;
}

/* cells a..z wrapped to width into new lines (wl/wc); returns the first */
static long wrap(long a, long z, int width)
{
    long first = nwl, i = a;
    int x = 0, started = 0;
    long sp = -1;               /* a space waiting to go between words */
    if (width < 1) width = 1;
    while (i < z) {
        IC *c = &ic[i];
        if (c->fl & IF_DEL) {
            i++;
            continue;
        }
        if (!started) {
            wl_new(c->sl);
            started = 1;
        }
        if (c->fl & IF_BRK) {
            wl_new(c->sl);
            x = 0;
            sp = -1;
            i++;
            continue;
        }
        if (c->c == ' ') {
            if (c->fl & IF_KEEP && sp >= 0 && x > 0 && x < width) {
                wl_add(&ic[sp]);        /* code keeps its runs of spaces */
                x++;
            }
            if (x > 0) sp = i;
            i++;
            continue;
        }
        {   /* a word: to the next space or break */
            long j = i;
            int wlen = 0;
            while (j < z && (ic[j].fl & IF_DEL || (ic[j].c != ' ' && !(ic[j].fl & IF_BRK)))) {
                if (!(ic[j].fl & IF_DEL)) wlen++;
                j++;
            }
            if (x > 0 && x + (sp >= 0) + wlen > width) {
                wl_new(c->sl);
                x = 0;
                sp = -1;
            }
            if (x > 0 && sp >= 0) {
                IC s = ic[sp];
                /* a space between two parts of one link stays underlined */
                if (!(s.link && ic[i].link == s.link)) s.at &= ~(TA_UNDER | TA_STRIKE);
                wl_add(&s);
                x++;
            }
            sp = -1;
            for (; i < j; i++) {
                if (ic[i].fl & IF_DEL) continue;
                if (x >= width) {
                    wl_new(ic[i].sl);
                    x = 0;
                }
                wl_add(&ic[i]);
                x++;
            }
        }
    }
    if (!started) wl_new(0);
    return first;
}

static void wl_reset(void)
{
    nwl = nwc = 0;
}

/* a wrapped line onto the current row (links become spans) */
static void putwl(long k, long row, int fg, int at)
{
    int j;
    for (j = 0; j < wl[k].n; j++) {
        IC *c = &wc[wl[k].a + j];
        int col = rowlen();
        rowput(c->c, fg >= 0 && c->fg == TC_DEFAULT ? fg : c->fg, c->bg, c->at | at);
        if (c->link) rowspan(row, col, col + 1, c->link);
    }
}

/* ---- blocks ------------------------------------------------------------------- */

static void blocks(SL *L, long n, int tight);

static void hrow(int w, ULONG ch, int fg)
{
    int i;
    for (i = 0; i < w; i++) rowput(ch, fg, TC_DEFAULT, 0);
}

static void rule(long src)
{
    if (rowstart(src) < 0) return;
    hrow(avail(), G(0x2500, '-'), C_RULE);
}

/* a heading's anchor as GitHub makes it: lower case, punctuation gone,
 * spaces as -, a number after one seen before */
static long slug(const char *t)
{
    char a[200], b[220];
    long k = 0, i, n = 0;
    for (i = 0; t[i] && k < (long)sizeof(a) - 1; i++) {
        UBYTE c = (UBYTE)t[i];
        if (c >= 0x80 || digit(c) || alpha(c) || c == '-' || c == '_') a[k++] = (char)lowc(c);
        else if (c == ' ') a[k++] = '-';
    }
    a[k] = 0;
    strcpy(b, a);
    for (;;) {
        long h;
        int same = 0;
        for (h = 0; h < D->nheads; h++)
            if (!strcmp(md_str(D, D->heads[h].anchor), b)) same = 1;
        if (!same) break;
        n++;
        strcpy(b, a);
        {
            char num[12];
            int q = 11;
            long v = n;
            num[11] = 0;
            do num[--q] = (char)('0' + v % 10); while ((v /= 10) && q);
            strcat(b, "-");
            strcat(b, num + q);
        }
    }
    return pool_add(b, (long)strlen(b));
}

static void heading(int level, const SL *L, long n, long src)
{
    static const UBYTE hfg[7] = { 0, C_H1, C_H2, C_H3, C_H4, C_H5, C_H6 };
    long a, first, k, row0 = D->nrows;
    int w = avail();
    char tmp[256];
    nic = 0;
    nim = 0;
    ifg = hfg[level];
    iat = TA_BOLD;
    if (level == 6) iat = TA_BOLD | TA_ITAL;
    ptext(L, n);
    inl(pt, npt);
    ifg = TC_DEFAULT;
    iat = 0;
    wl_reset();
    a = 0;
    first = wrap(a, nic, level == 1 ? w - 2 : w);
    for (k = first; k < nwl; k++) {
        long row = rowstart(src + wl[k].sl);
        if (row < 0) return;
        if (k == first) row0 = row;
        if (level == 1) {
            rowput(' ', C_H1, TC_DEFAULT, TA_REV | TA_BOLD);
            putwl(k, row, -1, TA_REV);
            rowput(' ', C_H1, TC_DEFAULT, TA_REV | TA_BOLD);
        } else
            putwl(k, row, -1, 0);
    }
    if (level == 2) {
        if (rowstart(src) < 0) return;
        hrow(w, G(0x2500, '-'), C_RULE);
    }
    if (GROW(D->heads, D->nheads, D->cheads, D->nheads + 1)) {
        RHead *h = &D->heads[D->nheads];
        h->row = row0;
        h->src = src;
        h->level = (short)level;
        h->text = pool_add(tmp, plain(0, nic, tmp, sizeof(tmp)));
        h->anchor = slug(md_str(D, h->text));
        D->nheads++;
    }
}

/* the language of a code block from its info string */
static int langof(const SL *info)
{
    static const struct { const char *n; int l; } names[] = {
        { "c", L_C }, { "h", L_C }, { "cpp", L_C }, { "c++", L_C }, { "java", L_C },
        { "js", L_C }, { "javascript", L_C }, { "ts", L_C }, { "typescript", L_C },
        { "go", L_C }, { "rust", L_C }, { "rs", L_C }, { "cs", L_C }, { "swift", L_C },
        { "asm", L_ASM }, { "s", L_ASM }, { "68k", L_ASM }, { "m68k", L_ASM }, { "nasm", L_ASM },
        { "rexx", L_REXX }, { "arexx", L_REXX }, { "e", L_E }, { "amigae", L_E },
        { "guide", L_GUIDE }, { "amigaguide", L_GUIDE }, { "amigados", L_SCRIPT },
        { "script", L_SCRIPT }, { "python", L_PY }, { "py", L_PY }, { "md", L_MD },
        { "markdown", L_MD }, { "sh", L_SH }, { "bash", L_SH }, { "zsh", L_SH },
        { "shell", L_SH }, { "console", L_SH }, { "csh", L_SH }, { "make", L_MAKE },
        { "makefile", L_MAKE }, { "json", L_JSON }, { "html", L_HTML }, { "xml", L_HTML },
        { "svg", L_HTML }, { "lua", L_LUA }, { "blitz", L_BLITZ }, { "amiblitz", L_BLITZ },
        { "installer", L_INSTALL }, { NULL, 0 }
    };
    char w[24];
    int k = 0, i;
    while (k < info->len && k < 23 && info->s[k] != ' ' && info->s[k] != '{' && info->s[k] != ',')
        w[k] = (char)lowc((UBYTE)info->s[k]), k++;
    w[k] = 0;
    for (i = 0; names[i].n; i++)
        if (!strcmp(w, names[i].n)) return names[i].l;
    return L_NONE;
}

static UBYTE *hl;
static long chl;

/* a code block: in a box, the language on its top, coloured as Sumi does */
static void codeblock(const SL *L, long n, const SL *info, long src, long endsrc)
{
    int w = avail(), inner = w - 4, lang = info ? langof(info) : L_NONE, st = 0, boxed = w >= 12;
    long i;
    if (!boxed) inner = w;
    if (boxed) {
        if (rowstart(src) < 0) return;
        rowput(tui_box(BX_TL), C_BOX, TC_DEFAULT, 0);
        rowput(tui_box(BX_H), C_BOX, TC_DEFAULT, 0);
        if (info && info->len) {
            long k = 0;
            int x = 2, lim = info->len < w - 6 ? (int)info->len : w - 6;
            rowput(' ', C_BOX, TC_DEFAULT, 0);
            x++;
            while (k < lim) {
                rowput(dec(info->s, lim, &k), C_LABEL, TC_DEFAULT, 0);
                x++;
            }
            rowput(' ', C_BOX, TC_DEFAULT, 0);
            x++;
            hrow(w - x - 1, tui_box(BX_H), C_BOX);
        } else
            hrow(w - 3, tui_box(BX_H), C_BOX);
        rowput(tui_box(BX_TR), C_BOX, TC_DEFAULT, 0);
    }
    for (i = 0; i < n; i++) {
        const SL *l = &L[i];
        long len = l->len, k = 0;
        int x = 0;
        while (len && l->s[len - 1] == '\r') len--;
        if (lang != L_NONE && GROW(hl, len, chl, len + 1)) st = syn_line(lang, l->s, len, st, hl);
        if (rowstart(l->ln) < 0) return;
        if (boxed) {
            rowput(tui_box(BX_V), C_BOX, TC_DEFAULT, 0);
            rowput(' ', C_BOX, TC_DEFAULT, 0);
        }
        while (k < len) {
            long at = k;
            ULONG c = dec(l->s, len, &k);
            int fg = hl && lang != L_NONE ? hlcol[hl[at]] : TC_DEFAULT, reps = 1;
            if (c == '\t') {
                reps = tabsize - x % tabsize;
                c = ' ';
            }
            while (reps--) {
                if (x >= inner) {   /* too long: on, on the next row */
                    if (boxed) {
                        rowput(' ', C_BOX, TC_DEFAULT, 0);
                        rowput(tui_box(BX_V), C_BOX, TC_DEFAULT, 0);
                    }
                    if (rowstart(l->ln) < 0) return;
                    if (boxed) {
                        rowput(tui_box(BX_V), C_BOX, TC_DEFAULT, 0);
                        rowput(0xBB, C_DIMT, TC_DEFAULT, 0);
                    }
                    x = 0;
                }
                rowput(c < 32 ? '?' : c, fg, TC_DEFAULT, 0);
                x++;
            }
        }
        if (boxed) {
            hrow(inner - x, ' ', TC_DEFAULT);
            rowput(' ', C_BOX, TC_DEFAULT, 0);
            rowput(tui_box(BX_V), C_BOX, TC_DEFAULT, 0);
        }
    }
    if (boxed) {
        if (rowstart(endsrc) < 0) return;
        rowput(tui_box(BX_BL), C_BOX, TC_DEFAULT, 0);
        hrow(w - 2, tui_box(BX_H), C_BOX);
        rowput(tui_box(BX_BR), C_BOX, TC_DEFAULT, 0);
    }
}

/* ---- pictures --------------------------------------------------------------- */

static ULONG be32(const UBYTE *p) { return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3]; }
static ULONG le32(const UBYTE *p) { return (ULONG)p[3] << 24 | (ULONG)p[2] << 16 | (ULONG)p[1] << 8 | p[0]; }

/* a picture file's size in pixels from its first bytes (PNG, GIF, JPEG,
 * IFF ILBM, BMP); FALSE if not known */
int md_picsize(const char *path, int *w, int *h)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    UBYTE *b;
    long n, i;
    int ok = 0;
    if (!fh) return 0;
    if (!(b = AllocVec(16384, MEMF_ANY))) {
        Close(fh);
        return 0;
    }
    n = Read(fh, b, 16384);
    Close(fh);
    if (n >= 24 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
        *w = (int)be32(b + 16);
        *h = (int)be32(b + 20);
        ok = 1;
    } else if (n >= 10 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F') {
        *w = b[6] | b[7] << 8;
        *h = b[8] | b[9] << 8;
        ok = 1;
    } else if (n >= 26 && b[0] == 'B' && b[1] == 'M') {
        *w = (int)le32(b + 18);
        *h = (int)le32(b + 22);
        if (*h < 0) *h = -*h;
        ok = 1;
    } else if (n >= 12 && b[0] == 'F' && b[1] == 'O' && b[2] == 'R' && b[3] == 'M') {
        for (i = 12; i + 12 <= n; ) {
            ULONG l = be32(b + i + 4);
            if (!memcmp(b + i, "BMHD", 4)) {
                *w = b[i + 8] << 8 | b[i + 9];
                *h = b[i + 10] << 8 | b[i + 11];
                ok = 1;
                break;
            }
            i += 8 + l + (l & 1);
        }
    } else if (n >= 4 && b[0] == 0xFF && b[1] == 0xD8) {
        for (i = 2; i + 9 < n; ) {
            UBYTE m;
            if (b[i] != 0xFF) break;
            m = b[i + 1];
            if (m == 0xFF) {
                i++;
                continue;
            }
            if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
                *h = b[i + 5] << 8 | b[i + 6];
                *w = b[i + 7] << 8 | b[i + 8];
                ok = 1;
                break;
            }
            i += 2 + (b[i + 2] << 8 | b[i + 3]);
        }
    }
    FreeVec(b);
    return ok && *w > 0 && *h > 0 && *w < 20000 && *h < 20000;
}

/* a link's target as an AmigaDOS path beside the file: "../x" is "/x",
 * "./x" is "x", %20 a space; "" for addresses (http:...) */
void md_resolve(const char *base, const char *url, char *out, int max)
{
    char tmp[256];
    int k = 0, i, dl_ = 0;
    const char *u = url;
    out[0] = 0;
    for (i = 0; u[i] && u[i] != '/' && u[i] != '#'; i++)
        if (u[i] == ':') return;            /* a scheme */
    if (base) {
        for (i = 0; base[i]; i++)
            if (base[i] == '/' || base[i] == ':') dl_ = i + 1;
        if (dl_ > max - 1) dl_ = max - 1;
        memcpy(tmp, base, dl_ < (int)sizeof(tmp) ? dl_ : (int)sizeof(tmp) - 1);
        k = dl_ < (int)sizeof(tmp) ? dl_ : (int)sizeof(tmp) - 1;
    }
    if (u[0] == '/') u++;                   /* from the top of the repository: beside it is best */
    while (*u && *u != '#' && *u != '?' && k < (int)sizeof(tmp) - 4) {
        if (u[0] == '.' && u[1] == '/') {
            u += 2;
            continue;
        }
        if (u[0] == '.' && u[1] == '.' && (u[2] == '/' || !u[2])) {
            tmp[k++] = '/';
            u += u[2] ? 3 : 2;
            continue;
        }
        if (u[0] == '%' && u[1] && u[2]) {
            int v = 0, j;
            for (j = 1; j <= 2; j++) {
                int c = lowc((UBYTE)u[j]);
                v = v * 16 + (digit(c) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0);
            }
            tmp[k++] = (char)v;
            u += 3;
            continue;
        }
        tmp[k++] = *u++;
    }
    tmp[k] = 0;
    strncpy(out, tmp, max - 1);
    out[max - 1] = 0;
}

static void picture(const Im *m, long src)
{
    char path[256];
    const char *url = md_str(D, m->url), *alt = md_str(D, m->alt);
    int pw = 0, ph = 0, cols, rows, w = avail();
    long row0;
    md_resolve(BASE, url, path, sizeof(path));
    if (md_pictures && md_cellw > 0 && md_cellh > 0 && path[0] && md_picsize(path, &pw, &ph)) {
        int want = m->wpx > 0 ? m->wpx : pw, r;
        cols = (want + md_cellw - 1) / md_cellw;
        if (cols > w) cols = w;
        if (cols < 1) cols = 1;
        rows = (int)(((long)ph * cols * md_cellw / pw + md_cellh - 1) / md_cellh);
        if (rows > md_maxpicrows) {
            rows = md_maxpicrows;
            cols = (int)(((long)pw * rows * md_cellh / ph + md_cellw - 1) / md_cellw);
            if (cols > w) cols = w;
        }
        if (rows < 1) rows = 1;
        row0 = -1;
        for (r = 0; r < rows; r++) {
            long row = rowstart(src);
            if (row < 0) return;
            if (row0 < 0) row0 = row;
            if (r == rows / 2) {    /* the words show if the picture cannot */
                long k = 0, l = (long)strlen(alt);
                int x = 0;
                rowput(G(0x2592, '#'), C_PIC, TC_DEFAULT, 0);
                rowput(' ', C_PIC, TC_DEFAULT, 0);
                x = 2;
                ENC = ENC_UTF8;
                while (k < l && x < cols) {
                    rowput(dec(alt, l, &k), C_PIC, TC_DEFAULT, TA_ITAL);
                    x++;
                }
                ENC = SB->enc;
            }
        }
        if (GROW(D->pics, D->npics, D->cpics, D->npics + 1)) {
            RPic *p = &D->pics[D->npics];
            p->row = row0;
            p->src = src;
            p->path = pool_add(path, (long)strlen(path));
            p->alt = m->alt;
            p->col = (short)prew();
            p->cols = (short)cols;
            p->rows = (short)rows;
            D->rows[row0].pic = (short)D->npics;
            D->npics++;
        }
        return;
    }
    {   /* no picture here: a line that says which */
        long row = rowstart(src), k = 0, l;
        int x = 2;
        int id = addlink(m->url, 0);
        if (row < 0) return;
        rowput(G(0x2592, '#'), C_PIC, TC_DEFAULT, 0);
        rowput(' ', C_PIC, TC_DEFAULT, 0);
        ENC = ENC_UTF8;
        l = (long)strlen(alt);
        if (!l) alt = url, l = (long)strlen(url);
        while (k < l && x < w) {
            int col = rowlen();
            rowput(dec(alt, l, &k), C_PIC, TC_DEFAULT, TA_ITAL | TA_UNDER);
            rowspan(row, col, col + 1, id);
            x++;
        }
        ENC = SB->enc;
    }
}

/* a paragraph's cells: only pictures (and spaces)? */
static int onlypics(long a, long z)
{
    long j;
    int any = 0;
    for (j = a; j < z; j++) {
        if (ic[j].fl & (IF_DEL | IF_BRK)) continue;
        if (ic[j].fl & IF_PIC) any = 1;
        else if (!(ic[j].fl & IF_ALT) && ic[j].c != ' ') return 0;
    }
    return any;
}

/* cells into rows: wrapped, each row's source line kept */
static void flow(long src, int fg, int at)
{
    long first, k;
    wl_reset();
    first = wrap(0, nic, avail());
    for (k = first; k < nwl; k++) {
        long row = rowstart(src + wl[k].sl);
        if (row < 0) return;
        putwl(k, row, fg, at);
    }
}

static void paragraph(const SL *L, long n)
{
    long src = L[0].ln;
    nic = 0;
    nim = 0;
    hb = hi = hs = hc = hk = hu = 0;
    ptext(L, n);
    inl(pt, npt);
    if (nim && onlypics(0, nic)) {
        int k;
        for (k = 0; k < nim; k++) {
            if (k) gap = 0;
            picture(&im[k], src);
        }
        return;
    }
    flow(src, -1, 0);
}

/* ---- tables ------------------------------------------------------------------ */

#define MAXCOL 32

/* a table row's cells (between |s, outside `code`) */
static int cellsof(const SL *lp, SL *c, int max)
{
    int n = 0;
    long i, a;
    SL l = *lp;
    trimall(&l);
    if (l.len && l.s[0] == '|') l.s++, l.len--;
    if (l.len && l.s[l.len - 1] == '|' && (l.len < 2 || l.s[l.len - 2] != '\\')) l.len--;
    a = 0;
    for (i = 0; i <= l.len; i++) {
        if (i < l.len && l.s[i] == '\\') {
            i++;
            continue;
        }
        if (i < l.len && l.s[i] == '`') {
            long m = 0, j;
            while (i + m < l.len && l.s[i + m] == '`') m++;
            for (j = i + m; j < l.len; j++)
                if (l.s[j] == '`') {
                    long r = 0;
                    while (j + r < l.len && l.s[j + r] == '`') r++;
                    if (r == m) break;
                    j += r - 1;
                }
            if (j < l.len) i = j + m - 1;
            else i += m - 1;
            continue;
        }
        if (i == l.len || l.s[i] == '|') {
            if (n < max) {
                c[n].s = l.s + a;
                c[n].len = i - a;
                c[n].ln = l.ln;
                trimall(&c[n]);
                n++;
            }
            a = i + 1;
        }
    }
    return n;
}

/* the delimiter row: its alignments (0 left, 1 centre, 2 right), the count */
static int delimrow(const SL *l, UBYTE *al)
{
    SL c[MAXCOL];
    int n, i;
    if (indent(l) >= 4 || !memchr(l->s, '-', l->len)) return 0;
    n = cellsof(l, c, MAXCOL);
    for (i = 0; i < n; i++) {
        long k = 0;
        int lc = 0, rc = 0, d = 0;
        if (k < c[i].len && c[i].s[k] == ':') lc = 1, k++;
        while (k < c[i].len && c[i].s[k] == '-') d++, k++;
        if (k < c[i].len && c[i].s[k] == ':') rc = 1, k++;
        if (!d || k != c[i].len) return 0;
        al[i] = (UBYTE)(lc && rc ? 1 : rc ? 2 : 0);
    }
    if (n == 1 && !memchr(l->s, '|', l->len)) return 0;
    return n;
}

static int tablestart(const SL *L, long n, long i, UBYTE *al)
{
    SL c[MAXCOL];
    int h, d;
    if (i + 1 >= n || indent(&L[i]) >= 4 || !memchr(L[i].s, '|', L[i].len)) return 0;
    if (!(d = delimrow(&L[i + 1], al))) return 0;
    h = cellsof(&L[i], c, MAXCOL);
    return h == d ? d : 0;
}

static void border(const int *cw, int nc, ULONG l, ULONG m, ULONG r, ULONG h, long src)
{
    int i;
    if (rowstart(src) < 0) return;
    rowput(l, C_BOX, TC_DEFAULT, 0);
    for (i = 0; i < nc; i++) {
        hrow(cw[i] + 2, h, C_BOX);
        rowput(i == nc - 1 ? r : m, C_BOX, TC_DEFAULT, 0);
    }
}

static long table(const SL *L, long n, long i, int nc, const UBYTE *al)
{
    long start = i, end, r, j;
    int cw[MAXCOL], nat[MAXCOL], k, total, w = avail();
    long (*ca)[2] = NULL;       /* each cell's cells in ic: a, z */
    int nrow;
    /* the rows: to a blank line or another block */
    for (end = i + 2; end < n; end++)
        if (blank(&L[end]) || (indent(&L[end]) < 4 && interrupts(&L[end]))) break;
    nrow = (int)(end - start - 1);
    nim = 0;
    if (!(ca = AllocVec(sizeof(*ca) * nrow * nc, MEMF_ANY | MEMF_CLEAR))) {
        D->failed = 1;
        return end;
    }
    nic = 0;
    for (k = 0; k < nc; k++) nat[k] = 3;
    for (r = 0; r < nrow; r++) {
        SL c[MAXCOL];
        long li = r == 0 ? start : start + 1 + r;
        int got = cellsof(&L[li], c, MAXCOL);
        for (k = 0; k < nc; k++) {
            ca[r * nc + k][0] = nic;
            if (k < got && c[k].len) {
                hb = hi = hs = hc = hk = hu = 0;
                npls = 1;
                if (!pls && !GROW(pls, npls, cpls, 1)) break;
                pls[0] = 0;
                isl = 0;
                inl(c[k].s, c[k].len);
            }
            ca[r * nc + k][1] = nic;
            {   /* its widest line */
                int x = 0, mx = 0;
                for (j = ca[r * nc + k][0]; j < nic; j++) {
                    if (ic[j].fl & IF_DEL) continue;
                    if (ic[j].fl & IF_BRK) x = 0;
                    else if (++x > mx) mx = x;
                }
                if (mx > nat[k]) nat[k] = mx;
            }
        }
    }
    /* the widths: as wide as the widest cell, the widest made narrower until it fits */
    for (k = 0; k < nc; k++) cw[k] = nat[k];
    for (;;) {
        int big = -1;
        total = 1;
        for (k = 0; k < nc; k++) total += cw[k] + 3;
        if (total <= w) break;
        for (k = 0; k < nc; k++)
            if (cw[k] > 6 && (big < 0 || cw[k] > cw[big])) big = k;
        if (big < 0) break;
        cw[big]--;
    }
    border(cw, nc, G(0x256D, '+'), G(0x252C, '+'), G(0x256E, '+'), G(0x2500, '-'), L[start].ln);
    for (r = 0; r < nrow; r++) {
        long li = r == 0 ? start : start + 1 + r, fl[MAXCOL];
        int nl[MAXCOL], h = 1, line;
        wl_reset();
        for (k = 0; k < nc; k++) {
            long z0 = ca[r * nc + k][0], z1 = ca[r * nc + k][1];
            if (z1 > z0) {
                fl[k] = wrap(z0, z1, cw[k]);
                nl[k] = (int)(nwl - fl[k]);
            } else {
                fl[k] = -1;
                nl[k] = 0;
            }
            if (nl[k] > h) h = nl[k];
        }
        for (line = 0; line < h; line++) {
            long row = rowstart(L[li].ln);
            if (row < 0) break;
            rowput(G(0x2502, '|'), C_BOX, TC_DEFAULT, 0);
            for (k = 0; k < nc; k++) {
                int used = 0, pad, lp;
                if (line < nl[k]) used = wl[fl[k] + line].n;
                pad = cw[k] - used;
                if (pad < 0) pad = 0;
                lp = al[k] == 2 ? pad : al[k] == 1 ? pad / 2 : 0;
                rowput(' ', TC_DEFAULT, TC_DEFAULT, 0);
                hrow(lp, ' ', TC_DEFAULT);
                if (line < nl[k]) putwl(fl[k] + line, row, -1, r == 0 ? TA_BOLD : 0);
                hrow(pad - lp, ' ', TC_DEFAULT);
                rowput(' ', TC_DEFAULT, TC_DEFAULT, 0);
                rowput(G(0x2502, '|'), C_BOX, TC_DEFAULT, 0);
            }
        }
        if (r == 0)
            border(cw, nc, G(0x255E, '+'), G(0x256A, '+'), G(0x2561, '+'), G(0x2550, '='),
                   L[start + 1].ln);
    }
    border(cw, nc, G(0x2570, '+'), G(0x2534, '+'), G(0x256F, '+'), G(0x2500, '-'), L[end - 1].ln);
    FreeVec(ca);
    return end;
}

/* ---- containers ----------------------------------------------------------- */

static SL *sublines(long n)
{
    SL *s = AllocVec(sizeof(SL) * (n ? n : 1), MEMF_ANY);
    if (!s) D->failed = 1;
    return s;
}

static long blockquote(SL *L, long n, long i, int tight)
{
    long j, m = 0;
    SL *sub = sublines(n - i);
    static const struct { const char *n; const char *label; UBYTE fg; } alerts[] = {
        { "[!note]", "Note", 12 }, { "[!tip]", "Tip", 10 }, { "[!important]", "Important", 13 },
        { "[!warning]", "Warning", 11 }, { "[!caution]", "Caution", 9 }, { NULL, NULL, 0 }
    };
    int bar = C_QUOTE, alert = -1, lazyok = 0;
    RCell f[2];
    (void)tight;
    if (!sub) return n;
    for (j = i; j < n; j++) {
        SL l = L[j];
        if (quotestart(&l)) {
            strip(&l, &l, 3);
            l.s++, l.len--;
            if (l.len && (l.s[0] == ' ' || l.s[0] == '\t')) l.s++, l.len--;
            sub[m++] = l;
            lazyok = !blank(&l) && !fence(&l, (char[1]){0}, (int[1]){0}, NULL);
        } else if (lazyok && !interrupts(&l)) {
            sub[m++] = l;               /* a lazy line: the paragraph goes on */
        } else
            break;
    }
    if (m) {
        SL t = sub[0];
        trimall(&t);
        int a;
        for (a = 0; alerts[a].n; a++) {
            int k, ln = (int)strlen(alerts[a].n);
            if (t.len != ln) continue;
            for (k = 0; k < ln && lowc((UBYTE)t.s[k]) == alerts[a].n[k]; k++) ;
            if (k == ln) {
                alert = a;
                bar = alerts[a].fg;
                break;
            }
        }
    }
    setcell(&f[0], G(0x2502, '|'), bar, TC_DEFAULT, 0);
    setcell(&f[1], ' ', TC_DEFAULT, TC_DEFAULT, 0);
    pushlev(f, f, 2);
    if (alert >= 0) {
        const char *s = alerts[alert].label;
        if (rowstart(sub[0].ln) >= 0) {
            while (*s) rowput((UBYTE)*s++, bar, TC_DEFAULT, TA_BOLD);
        }
        if (m > 1) blocks(sub + 1, m - 1, 1);
    } else
        blocks(sub, m, 0);
    poplev();
    FreeVec(sub);
    return j;
}

static long list(SL *L, long n, long i)
{
    Mark m0, m;
    long j, num, maxnum, items = 0, k;
    int loose = 0, numw = 1;
    listmark(&L[i], &m0);
    num = m0.start;
    /* how wide the numbers get: count the items first */
    maxnum = num;
    for (j = i; j < n; j++)
        if (listmark(&L[j], &m) && m.type == m0.type && indent(&L[j]) < 4) maxnum++;
    if (m0.type == '.' || m0.type == ')') {
        long v = maxnum;
        numw = 1;
        while (v >= 10) v /= 10, numw++;
    }
    /* loose: a blank line between items, or inside one between blocks */
    {
        long a = i;
        while (a < n) {
            long e = a + 1, lastnb;
            int w;
            if (!listmark(&L[a], &m) || m.type != m0.type) break;
            w = m.width;
            for (; e < n; e++) {
                if (blank(&L[e])) continue;
                if (indent(&L[e]) >= w) continue;
                if (!blank(&L[e - 1])) {
                    if (listmark(&L[e], &m) || interrupts(&L[e])) break;
                    continue;           /* lazy */
                }
                break;
            }
            lastnb = e - 1;
            while (lastnb > a && blank(&L[lastnb])) lastnb--;
            for (k = a + 1; k < lastnb; k++)
                if (blank(&L[k])) loose = 1;
            if (e < n && lastnb < e - 1 && listmark(&L[e], &m) && m.type == m0.type) loose = 1;
            a = e;
            if (a < n && blank(&L[a])) break;
        }
    }
    listdepth++;
    for (j = i; j < n;) {
        SL *sub;
        long e, cnt = 0, lastnb;
        int w, task = 0, done = 0, pw;
        RCell f[12], r[12];
        Mark m2;
        if (!listmark(&L[j], &m) || m.type != m0.type || indent(&L[j]) >= 4) break;
        w = m.width;
        for (e = j + 1; e < n; e++) {
            if (blank(&L[e])) continue;
            if (indent(&L[e]) >= w) continue;
            if (!blank(&L[e - 1]) && !interrupts(&L[e]) && !listmark(&L[e], &m2)) continue;
            break;
        }
        lastnb = e - 1;
        while (lastnb > j && blank(&L[lastnb])) lastnb--;
        if (!(sub = sublines(lastnb - j + 1))) break;
        {   /* the first line: past the indent, the marker and its spaces */
            SL t;
            long q = 0;
            int ind = indent(&L[j]);
            strip(&t, &L[j], 3);
            while (q < t.len && t.s[q] != ' ' && t.s[q] != '\t') q++;
            t.s += q;
            t.len -= q;
            if (m.empty) t.len = 0;
            else strip(&t, &t, w - ind - (int)q > 0 ? w - ind - (int)q : 1);
            sub[cnt] = t;
        }
        cnt++;
        for (k = j + 1; k <= lastnb; k++) {
            if (indent(&L[k]) >= w || blank(&L[k])) strip(&sub[cnt++], &L[k], w);
            else sub[cnt++] = L[k];     /* lazy */
        }
        /* [ ] and [x]: a task */
        if (sub[0].len >= 3 && sub[0].s[0] == '[' && sub[0].s[2] == ']' &&
            (sub[0].s[1] == ' ' || sub[0].s[1] == 'x' || sub[0].s[1] == 'X') &&
            (sub[0].len == 3 || sub[0].s[3] == ' ' || sub[0].s[3] == '\t')) {
            task = 1;
            done = sub[0].s[1] != ' ';
            if (GROW(D->tasks, D->ntasks, D->ctasks, D->ntasks + 1)) {
                RTask *t = &D->tasks[D->ntasks++];
                t->row = -1;
                t->src = sub[0].ln;
                t->scol = sub[0].s - SB->l[sub[0].ln].s;
                t->done = (short)done;
                t->col = 0;
            }
            sub[0].s += sub[0].len > 3 ? 4 : 3;
            sub[0].len -= sub[0].len > 3 ? 4 : 3;
        }
        /* the marker in front of the first row, spaces in front of the rest */
        pw = 0;
        if (m0.type == '.' || m0.type == ')') {
            char d[12];
            int q = 11, z;
            long v = num;
            d[11] = 0;
            do d[--q] = (char)('0' + v % 10); while ((v /= 10) && q);
            for (z = 0; z < numw - (11 - q); z++) setcell(&f[pw++], ' ', TC_DEFAULT, TC_DEFAULT, 0);
            while (d[q]) setcell(&f[pw++], (UBYTE)d[q++], C_BULLET, TC_DEFAULT, 0);
            setcell(&f[pw++], (UBYTE)m0.type, C_BULLET, TC_DEFAULT, 0);
        } else {
            /* glyphs any CTerm font has (U+25E6 and the like are often missing) */
            static const ULONG bu[3] = { 0x2022, '-', 0xB7 };
            static const char ba[3] = { '*', '-', '+' };
            int dd = (listdepth - 1) % 3;
            setcell(&f[pw++], G(bu[dd], (UBYTE)ba[dd]), C_BULLET, TC_DEFAULT, 0);
        }
        setcell(&f[pw++], ' ', TC_DEFAULT, TC_DEFAULT, 0);
        if (task) {
            setcell(&f[pw++], '[', C_DIMT, TC_DEFAULT, 0);
            setcell(&f[pw++], done ? 'x' : ' ', done ? C_DONE : C_DIMT, TC_DEFAULT,
                    TA_BOLD);
            setcell(&f[pw++], ']', C_DIMT, TC_DEFAULT, 0);
            setcell(&f[pw++], ' ', TC_DEFAULT, TC_DEFAULT, 0);
        }
        for (k = 0; k < pw; k++) setcell(&r[k], ' ', TC_DEFAULT, TC_DEFAULT, 0);
        if (items && loose) gap = 1;
        {
            long r0 = D->nrows;
            pushlev(f, r, pw);
            if (!(cnt == 1 && sub[0].len == 0)) blocks(sub, cnt, !loose);
            if (!lv[nlv - 1].used) rowstart(L[j].ln);   /* an empty item still shows */
            poplev();
            if (task && D->ntasks) {
                RTask *t = &D->tasks[D->ntasks - 1];
                long q;
                t->col = (short)(prew() + pw - 4);      /* where its [ is on the row */
                t->row = r0;
                for (q = r0; q < D->nrows; q++)         /* past a gap row */
                    if (D->rows[q].src == t->src) {
                        t->row = q;
                        break;
                    }
            }
        }
        FreeVec(sub);
        items++;
        num++;
        j = e;
        /* blank lines before the next item */
        while (j < n && blank(&L[j])) j++;
        if (j < n && !(listmark(&L[j], &m) && m.type == m0.type && indent(&L[j]) < 4)) {
            /* the list ended: give the blank lines back */
            while (j > e && blank(&L[j - 1])) j--;
            break;
        }
    }
    listdepth--;
    return j;
}

/* the blocks of these lines, rows out */
static void blocks(SL *L, long n, int tight)
{
    long i = 0;
    int first = 1;
    if (++depth > MAXDEPTH) {
        depth--;
        return;
    }
    while (i < n && !D->failed) {
        SL *l = &L[i];
        char ch;
        int cnt, lvl, nc;
        SL info, t;
        Mark mk;
        UBYTE al[MAXCOL];
        long r0 = D->nrows;
        if (blank(l) || (isdef && isdef[l->ln])) {
            i++;
            continue;
        }
        if (!first && !tight) gap = 1;
        else if (!first && tight) gap = 0;
        if (indent(l) >= 4) {           /* indented code */
            long e = i, k;
            SL *sub;
            while (e < n && (blank(&L[e]) || indent(&L[e]) >= 4)) e++;
            while (e > i && blank(&L[e - 1])) e--;
            if ((sub = sublines(e - i)) != NULL) {
                for (k = i; k < e; k++) strip(&sub[k - i], &L[k], 4);
                codeblock(sub, e - i, NULL, L[i].ln, L[e - 1].ln);
                FreeVec(sub);
            }
            i = e;
        } else if (fence(l, &ch, &cnt, &info)) {
            long e = i + 1, k;
            int ind = indent(l);
            SL *sub;
            char c2;
            int n2;
            SL in2;
            while (e < n) {
                if (fence(&L[e], &c2, &n2, &in2) && c2 == ch && n2 >= cnt && !in2.len) break;
                e++;
            }
            if ((sub = sublines(e - i)) != NULL) {
                for (k = i + 1; k < e; k++) strip(&sub[k - i - 1], &L[k], ind);
                codeblock(sub, e - i - 1, &info, L[i].ln, e < n ? L[e].ln : L[e - 1].ln);
                FreeVec(sub);
            }
            i = e < n ? e + 1 : e;
        } else if ((lvl = atx(l, &t)) != 0) {
            heading(lvl, &t, t.len ? 1 : 0, l->ln);
            i++;
        } else if (hrule(l)) {
            rule(l->ln);
            i++;
        } else if (quotestart(l)) {
            i = blockquote(L, n, i, tight);
        } else if (listmark(l, &mk)) {
            long e = list(L, n, i);
            i = e > i ? e : i + 1;
        } else if ((nc = tablestart(L, n, i, al)) != 0) {
            i = table(L, n, i, nc, al);
        } else if (htmlstart(l)) {
            long e = i, s2;
            int comment;
            SL q;
            strip(&q, l, 3);
            comment = q.len >= 4 && !strncmp(q.s, "<!--", 4);
            if (comment) {
                for (; e < n; e++)
                    if (L[e].len >= 3) {
                        long q;
                        for (q = 0; q + 2 < L[e].len; q++)
                            if (!strncmp(L[e].s + q, "-->", 3)) break;
                        if (q + 2 < L[e].len) break;
                    }
                i = e < n ? e + 1 : e;
                /* a comment and nothing else: no rows, no gap */
                if (D->nrows == r0) gap = 0;
                continue;
            }
            while (e < n && !blank(&L[e])) e++;
            s2 = i;
            paragraph(L + s2, e - s2);
            i = e;
        } else {                        /* a paragraph (or a setext heading) */
            long e = i + 1;
            int sx = 0;
            UBYTE al2[MAXCOL];
            while (e < n) {
                if (blank(&L[e])) break;
                if ((sx = setext(&L[e])) != 0) break;
                if (tablestart(L, n, e, al2)) break;
                if (interrupts(&L[e])) break;
                e++;
            }
            if (sx) {
                heading(sx, L + i, e - i, L[i].ln);
                i = e + 1;
            } else {
                paragraph(L + i, e - i);
                i = e;
            }
        }
        if (D->nrows > r0) first = 0;
        else if (!first) gap = 0;
    }
    gap = 0;
    depth--;
}

/* ---- the whole ------------------------------------------------------------- */

void md_free(Doc *d)
{
    if (d->rows) FreeVec(d->rows);
    if (d->cells) FreeVec(d->cells);
    if (d->spans) FreeVec(d->spans);
    if (d->links) FreeVec(d->links);
    if (d->heads) FreeVec(d->heads);
    if (d->pics) FreeVec(d->pics);
    if (d->tasks) FreeVec(d->tasks);
    if (d->pool) FreeVec(d->pool);
    memset(d, 0, sizeof(*d));
    d->rev = -1;
}

/* the work buffers md_render keeps between calls (grown, never shrunk),
 * freed: for a program before it ends, for a library (markdown.datatype)
 * before it is unloaded */
void md_cleanup(void)
{
    if (refs) FreeVec(refs);
    if (ic) FreeVec(ic);
    if (pt) FreeVec(pt);
    if (pls) FreeVec(pls);
    if (wc) FreeVec(wc);
    if (wl) FreeVec(wl);
    if (hl) FreeVec(hl);
    refs = NULL;
    ic = wc = NULL;
    pt = NULL;
    pls = NULL;
    wl = NULL;
    hl = NULL;
    crefs = cic = cpt = cpls = cwc = cwl = chl = 0;
    nrefs = nic = npt = npls = nwc = nwl = 0;
}

void md_render(Doc *d, Buf *b, int width, const char *path)
{
    SL *L;
    long i, start = 0;
    D = d;
    SB = b;
    ENC = b->enc;
    W = width > 8 ? width : 8;
    BASE = path;
    d->nrows = d->ncells = d->nspans = d->nlinks = d->nheads = d->npics = d->ntasks = 0;
    d->npool = 0;
    d->failed = 0;
    d->width = width;
    pool_add("", 0);
    nrefs = 0;
    nlv = 0;
    gap = 0;
    depth = 0;
    listdepth = 0;
    nic = 0;
    ndl = nbr = nim = 0;
    hb = hi = hs = hc = hk = hu = 0;
    ifg = TC_DEFAULT;
    iat = 0;
    lastsrc = 0;
    if (!(L = AllocVec(sizeof(SL) * (b->n ? b->n : 1), MEMF_ANY))) {
        d->failed = 1;
        return;
    }
    isdef = AllocVec(b->n + 1, MEMF_ANY | MEMF_CLEAR);
    for (i = 0; i < b->n; i++) {
        L[i].s = b->l[i].s;
        L[i].len = b->l[i].len;
        L[i].ln = i;
        if (L[i].len && L[i].s[L[i].len - 1] == '\r') L[i].len--;
    }
    /* the definitions first: a link may use one written below it */
    for (i = 0; i < b->n; i++)
        if (memchr(L[i].s, ']', L[i].len)) trydef(&L[i]);
    /* front matter (--- at the very top): shown as a quiet block */
    if (b->n > 2 && L[0].len == 3 && !strncmp(L[0].s, "---", 3)) {
        long e;
        for (e = 1; e < b->n && e < 200; e++)
            if ((L[e].len == 3 && (!strncmp(L[e].s, "---", 3) || !strncmp(L[e].s, "...", 3)))) break;
        if (e < b->n && e < 200 && e > 1) {
            SL info;
            info.s = "front matter";
            info.len = 12;
            info.ln = 0;
            codeblock(L + 1, e - 1, &info, 0, e);
            start = e + 1;
            gap = 1;
        }
    }
    blocks(L + start, b->n - start, 0);
    FreeVec(L);
    if (isdef) FreeVec(isdef);
    isdef = NULL;
    d->rev = b->rev;
}

long md_rowof(const Doc *d, long src)
{
    long lo = 0, hi_ = d->nrows - 1, best = 0;
    while (lo <= hi_) {
        long mid = (lo + hi_) / 2;
        if (d->rows[mid].src <= src) {
            best = mid;
            lo = mid + 1;
        } else
            hi_ = mid - 1;
    }
    /* the first row of that line */
    while (best > 0 && d->rows[best - 1].src == d->rows[best].src) best--;
    return best;
}

int md_anchor(const Doc *d, const char *name)
{
    long h;
    char a[200];
    int k = 0;
    if (*name == '#') name++;
    while (*name && k < (int)sizeof(a) - 1) {
        if (name[0] == '%' && name[1] && name[2]) {
            int v = 0, j;
            for (j = 1; j <= 2; j++) {
                int c = lowc((UBYTE)name[j]);
                v = v * 16 + (digit(c) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0);
            }
            a[k++] = (char)v;
            name += 3;
        } else
            a[k++] = (char)lowc((UBYTE)*name++);
    }
    a[k] = 0;
    for (h = 0; h < d->nheads; h++)
        if (!strcmp(md_str(d, d->heads[h].anchor), a)) return (int)h;
    return -1;
}

int md_link_at(const Doc *d, long row, int col)
{
    long i;
    for (i = 0; i < d->nspans; i++)
        if (d->spans[i].row == row && col >= d->spans[i].c0 && col < d->spans[i].c1)
            return d->spans[i].link;
    return 0;
}
