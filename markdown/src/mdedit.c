/* mdedit - help writing Markdown: Return goes on with a list or a quote,
 * bold and the rest around the selection (or the word), headings,
 * lists, quotes and task boxes put on lines and taken off again, a
 * table made neat, a list of the headings put in. Every change is one
 * undo step. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>
#include "markdown.h"
#include "md.h"

static void selr(Buf *b, Pos *a, Pos *z)
{
    if (pos_cmp(b->anchor, b->cur) <= 0) {
        *a = b->anchor;
        *z = b->cur;
    } else {
        *a = b->cur;
        *z = b->anchor;
    }
}

static void at(Buf *b, long line, long col, const char *t)
{
    Pos p;
    p.line = line;
    p.col = col;
    buf_insert(b, p, t, (long)strlen(t));
}

static void cut(Buf *b, long line, long a, long z)
{
    Pos p, q;
    p.line = q.line = line;
    p.col = a;
    q.col = z;
    if (z > a) buf_delete(b, p, q);
}

static void wantx(Buf *b)
{
    b->wantx = buf_xof(b, b->cur.line, b->cur.col);
}

/* ---- a line's own marks: indent, list marker or quote, task box ---------- */

typedef struct {
    long ind;                   /* bytes of indent */
    long mk, mlen;              /* the marker (- 1. >) and its length with the space after */
    char bullet;                /* - * + or . ) for numbers, '>' a quote, 0 none */
    long num;
    long box;                   /* the "[ ] " after it: its byte, -1 none */
    long text;                  /* where the words start */
} Marks;

static void marks(const Line *l, Marks *m)
{
    long i = 0;
    memset(m, 0, sizeof(*m));
    m->box = -1;
    while (i < l->len && (l->s[i] == ' ' || l->s[i] == '\t')) i++;
    m->ind = m->mk = i;
    if (i < l->len && (l->s[i] == '-' || l->s[i] == '*' || l->s[i] == '+') &&
        (i + 1 == l->len || l->s[i + 1] == ' ')) {
        m->bullet = l->s[i];
        i++;
    } else if (i < l->len && l->s[i] == '>') {
        m->bullet = '>';
        i++;
    } else {
        long j = i, n = 0;
        while (j < l->len && j - i < 9 && l->s[j] >= '0' && l->s[j] <= '9') n = n * 10 + l->s[j++] - '0';
        if (j > i && j < l->len && (l->s[j] == '.' || l->s[j] == ')') &&
            (j + 1 == l->len || l->s[j + 1] == ' ')) {
            m->bullet = l->s[j];
            m->num = n;
            i = j + 1;
        }
    }
    if (m->bullet) {
        if (i < l->len && l->s[i] == ' ') i++;
        m->mlen = i - m->mk;
        if (m->bullet != '>' && i + 2 < l->len && l->s[i] == '[' && l->s[i + 2] == ']' &&
            (l->s[i + 1] == ' ' || l->s[i + 1] == 'x' || l->s[i + 1] == 'X') &&
            (i + 3 == l->len || l->s[i + 3] == ' ')) {
            m->box = i;
            i += i + 3 < l->len ? 4 : 3;
        }
    }
    m->text = i;
}

int md_enter(Buf *b)
{
    Line *l = &b->l[b->cur.line];
    Marks m;
    char t[64];
    long k;
    if (b->sel) return 0;
    marks(l, &m);
    if (!m.bullet || b->cur.col < m.text) return 0;
    buf_group(b);
    if (m.text >= l->len) {
        /* an empty item: the list (or quote) ends here */
        cut(b, b->cur.line, m.mk, l->len);
        b->cur.col = m.mk;
        buf_group(b);
        wantx(b);
        return 1;
    }
    t[0] = '\n';
    k = 1;
    if (m.ind < 40) {
        memcpy(t + k, l->s, m.ind);
        k += m.ind;
    }
    t[k] = 0;
    if (m.bullet == '.' || m.bullet == ')') {
        char d[12];
        int q = 11;
        long v = m.num + 1;
        d[11] = 0;
        do d[--q] = (char)('0' + v % 10); while ((v /= 10) && q);
        strcat(t, d + q);
        k = (long)strlen(t);
        t[k++] = m.bullet;
        t[k++] = ' ';
        t[k] = 0;
    } else {
        t[k++] = m.bullet;
        t[k++] = ' ';
        t[k] = 0;
    }
    if (m.box >= 0) strcat(t, "[ ] ");
    instext(b, t, (long)strlen(t));
    buf_group(b);
    wantx(b);
    return 1;
}

/* ---- around the selection --------------------------------------------------- */

static int isw(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           (unsigned char)c >= 0x80 || c == '_';
}

/* pre and post around the selection, the word at the cursor, or nothing
 * (the cursor between them); a second time takes them away again */
static void around(Buf *b, const char *pre, const char *post)
{
    Pos a, z;
    long pl = (long)strlen(pre), ql = (long)strlen(post);
    buf_group(b);
    if (b->sel) selr(b, &a, &z);
    else {
        Line *l = &b->l[b->cur.line];
        a = z = b->cur;
        while (a.col > 0 && isw(l->s[a.col - 1])) a.col--;
        while (z.col < l->len && isw(l->s[z.col])) z.col++;
    }
    {   /* already marked so: unmark */
        Line *la = &b->l[a.line], *lz = &b->l[z.line];
        if (a.col >= pl && z.col + ql <= lz->len && !strncmp(la->s + a.col - pl, pre, pl) &&
            !strncmp(lz->s + z.col, post, ql)) {
            cut(b, z.line, z.col, z.col + ql);
            cut(b, a.line, a.col - pl, a.col);
            a.col -= pl;
            if (z.line == a.line) z.col -= pl;
            b->anchor = a;
            b->cur = z;
            b->sel = pos_cmp(a, z) != 0;
            buf_group(b);
            wantx(b);
            return;
        }
    }
    at(b, z.line, z.col, post);
    at(b, a.line, a.col, pre);
    if (pos_cmp(a, z) == 0) {           /* nothing to mark: write between them */
        b->cur.line = a.line;
        b->cur.col = a.col + pl;
        b->sel = 0;
    } else {
        b->anchor.line = a.line;
        b->anchor.col = a.col + pl;
        b->cur.line = z.line;
        b->cur.col = z.col + (z.line == a.line ? pl : 0);
        b->sel = 1;
    }
    buf_group(b);
    wantx(b);
}

void md_bold(Buf *b)
{
    around(b, "**", "**");
}

/* ---- on whole lines --------------------------------------------------------------- */

static void lines(Buf *b, long *a, long *z)
{
    if (b->sel) {
        Pos p, q;
        selr(b, &p, &q);
        *a = p.line;
        *z = q.line;
        if (q.col == 0 && q.line > p.line) (*z)--;
    } else
        *a = *z = b->cur.line;
}

/* '-' a list, '1' numbers, '>' a quote, 'x' task boxes: on, or off when all have it */
static void prefix(Buf *b, char kind)
{
    long a, z, i, n = 1;
    int all = 1;
    lines(b, &a, &z);
    for (i = a; i <= z; i++) {
        Marks m;
        marks(&b->l[i], &m);
        if (!b->l[i].len && a != z) continue;
        if (kind == '>' ? m.bullet != '>' : kind == 'x' ? m.box < 0 :
            kind == '1' ? m.bullet != '.' && m.bullet != ')' : !(m.bullet == '-' || m.bullet == '*' || m.bullet == '+'))
            all = 0;
    }
    buf_group(b);
    for (i = a; i <= z; i++) {
        Line *l = &b->l[i];
        Marks m;
        if (!l->len && a != z) continue;
        marks(l, &m);
        if (all) {      /* off */
            if (kind == 'x') cut(b, i, m.box, m.text);
            else cut(b, i, m.mk, m.mk + m.mlen + (kind != '>' && m.box >= 0 ? m.text - m.box : 0));
            continue;
        }
        if (kind == 'x') {
            if (m.box >= 0) continue;
            if (m.bullet && m.bullet != '>') at(b, i, m.mk + m.mlen, "[ ] ");
            else at(b, i, m.ind, "- [ ] ");
        } else if (kind == '>') {
            if (m.bullet != '>') at(b, i, 0, "> ");
        } else {
            char t[16];
            if (m.bullet && m.bullet != '>') cut(b, i, m.mk, m.mk + m.mlen);
            if (kind == '1') {
                char d[12];
                int q = 11;
                long v = n++;
                d[11] = 0;
                do d[--q] = (char)('0' + v % 10); while ((v /= 10) && q);
                strcpy(t, d + q);
                strcat(t, ". ");
            } else
                strcpy(t, "- ");
            at(b, i, m.ind, t);
        }
    }
    b->sel = 0;
    if (b->cur.col > b->l[b->cur.line].len) b->cur.col = b->l[b->cur.line].len;
    buf_group(b);
    wantx(b);
}

/* # more, up to ###, then none */
static void heading(Buf *b)
{
    long ln = b->cur.line;
    Line *l = &b->l[ln];
    long n = 0;
    while (n < l->len && l->s[n] == '#') n++;
    buf_group(b);
    if (n >= 3) cut(b, ln, 0, n < l->len && l->s[n] == ' ' ? n + 1 : n);
    else if (n) at(b, ln, 0, "#");
    else at(b, ln, 0, "# ");
    buf_group(b);
    if (b->cur.col > b->l[ln].len) b->cur.col = b->l[ln].len;
    wantx(b);
}

static void codeblock(Buf *b)
{
    long a, z;
    char lang[40], t[60];
    lang[0] = 0;
    lines(b, &a, &z);
    if (!prompt("Code block's language (empty: none):", lang, sizeof(lang), P_FIND)) return;
    buf_group(b);
    at(b, z, b->l[z].len, "\n```");
    strcpy(t, "```");
    strcat(t, lang);
    strcat(t, "\n");
    at(b, a, 0, t);
    b->sel = 0;
    b->cur.line = a + 1;
    b->cur.col = 0;
    buf_group(b);
    wantx(b);
}

static void link(Buf *b, int pic)
{
    char url[PATHMAX], t[PATHMAX + 8];
    url[0] = 0;
    if (!prompt(pic ? "Picture file:" : "Link to (an address, a file, #heading):", url,
                sizeof(url), pic ? P_FILE : P_FIND) || !url[0])
        return;
    if (pic && b->path[0]) {   /* beside the file: its drawer left off */
        const char *base = basename_(b->path);
        long dl = base - b->path;
        if (dl && !strncmp(url, b->path, dl)) memmove(url, url + dl, strlen(url + dl) + 1);
    }
    strcpy(t, "](");
    strcat(t, url);
    strcat(t, ")");
    if (b->sel) {
        around(b, pic ? "![" : "[", t);
        return;
    }
    {   /* no words chosen: write some, chosen to be typed over */
        const char *w = pic ? basename_(url) : "link";
        long wl = (long)strlen(w), k;
        char s[PATHMAX * 2 + 8];
        if (pic)
            for (k = 0; k < wl; k++)
                if (w[k] == '.') wl = k;
        strcpy(s, pic ? "![" : "[");
        strncat(s, w, wl);
        strcat(s, t);
        buf_group(b);
        at(b, b->cur.line, b->cur.col, s);
        b->anchor.line = b->cur.line;
        b->anchor.col = b->cur.col + (pic ? 2 : 1);
        b->cur.col = b->anchor.col + wl;
        b->sel = 1;
        buf_group(b);
        wantx(b);
    }
}

static int intable(Buf *b, long ln)
{
    return ln >= 0 && ln < b->n && memchr(b->l[ln].s, '|', b->l[ln].len) != NULL;
}

void md_format(Buf *b)
{
    int k = ask("Format: i)talic c)ode s)trike l)ink p)icture h)eading q)uote -)list 1)numbers "
                "x)task k)code block r)ule t)able", "icslphq-1xkrt");
    switch (k) {
    case 'i': around(b, "*", "*"); break;
    case 'c':
        if (b->sel && b->anchor.line != b->cur.line) codeblock(b);
        else around(b, "`", "`");
        break;
    case 's': around(b, "~~", "~~"); break;
    case 'l': link(b, 0); break;
    case 'p': link(b, 1); break;
    case 'h': heading(b); break;
    case 'q': prefix(b, '>'); break;
    case '-': prefix(b, '-'); break;
    case '1': prefix(b, '1'); break;
    case 'x': prefix(b, 'x'); break;
    case 'k': codeblock(b); break;
    case 'r':
        buf_group(b);
        at(b, b->cur.line, b->l[b->cur.line].len, "\n\n---\n");
        b->cur.line += 3;
        b->cur.col = 0;
        b->sel = 0;
        buf_group(b);
        wantx(b);
        break;
    case 't':
        if (intable(b, b->cur.line)) md_table(b);
        else {
            static const char tmpl[] = "| Column | Column |\n| ------ | ------ |\n|        |        |\n";
            long ln = b->cur.line;
            buf_group(b);
            if (b->l[ln].len) {
                at(b, ln, b->l[ln].len, "\n\n");
                ln += 2;
            }
            at(b, ln, 0, tmpl);
            b->cur.line = ln;
            b->cur.col = 2;
            b->anchor = b->cur;
            b->anchor.col = 2;
            b->cur.col = 8;
            b->sel = 1;
            buf_group(b);
            wantx(b);
        }
        break;
    }
}

/* ---- tables made neat ------------------------------------------------------------- */

#define TCOLS 32

typedef struct { const char *s; long len; } Cell;

static long cellw(const char *s, long n, int enc)
{
    long i, w = 0;
    for (i = 0; i < n; i++)
        if (enc != ENC_UTF8 || ((unsigned char)s[i] & 0xC0) != 0x80) w++;
    return w;
}

static int split_(const char *s, long n, Cell *c)
{
    long i, a;
    int k = 0;
    while (n && (s[0] == ' ' || s[0] == '\t')) s++, n--;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;
    if (n && s[0] == '|') s++, n--;
    if (n && s[n - 1] == '|' && (n < 2 || s[n - 2] != '\\')) n--;
    a = 0;
    for (i = 0; i <= n; i++) {
        if (i < n && s[i] == '\\') {
            i++;
            continue;
        }
        if (i < n && s[i] == '`') {
            long j = i + 1;
            while (j < n && s[j] != '`') j++;
            if (j < n) i = j;
            continue;
        }
        if (i == n || s[i] == '|') {
            if (k < TCOLS) {
                const char *p = s + a;
                long l = i - a;
                while (l && (*p == ' ' || *p == '\t')) p++, l--;
                while (l && (p[l - 1] == ' ' || p[l - 1] == '\t')) l--;
                c[k].s = p;
                c[k].len = l;
                k++;
            }
            a = i + 1;
        }
    }
    return k;
}

static int isdelim(const Cell *c, int n, char *al)
{
    int i;
    for (i = 0; i < n; i++) {
        long k = 0;
        int l = 0, r = 0, d = 0;
        if (k < c[i].len && c[i].s[k] == ':') l = 1, k++;
        while (k < c[i].len && c[i].s[k] == '-') d++, k++;
        if (k < c[i].len && c[i].s[k] == ':') r = 1, k++;
        if (!d || k != c[i].len) return 0;
        al[i] = l && r ? 'c' : r ? 'r' : l ? 'l' : 0;
    }
    return n > 0;
}

void md_table(Buf *b)
{
    long a = b->cur.line, z = b->cur.line, ln, total = 0;
    int nc = 0, i, w[TCOLS];
    char al[TCOLS];
    char *out, *o;
    Cell c[TCOLS];
    if (!intable(b, a)) {
        say("no table here (a line with | in it)", 1);
        return;
    }
    while (intable(b, a - 1)) a--;
    while (intable(b, z + 1)) z++;
    if (z - a < 1 || !isdelim(c, split_(b->l[a + 1].s, b->l[a + 1].len, c), al)) {
        say("not a table: its second line should be | --- | --- |", 1);
        return;
    }
    nc = split_(b->l[a].s, b->l[a].len, c);
    if (nc > TCOLS) nc = TCOLS;
    isdelim(c, split_(b->l[a + 1].s, b->l[a + 1].len, c), al);
    for (i = 0; i < nc; i++) w[i] = 3;
    for (ln = a; ln <= z; ln++) {
        int n;
        if (ln == a + 1) continue;
        n = split_(b->l[ln].s, b->l[ln].len, c);
        for (i = 0; i < n && i < nc; i++) {
            long cw = cellw(c[i].s, c[i].len, b->enc);
            if (cw > w[i]) w[i] = (int)cw;
        }
    }
    for (ln = a; ln <= z; ln++) total += b->l[ln].len * 2 + nc * (4 + 80) + 4;
    for (i = 0; i < nc; i++) total += (long)w[i] * (z - a + 1);
    if (!(out = AllocVec(total + 16, MEMF_ANY))) {
        say("out of memory", 1);
        return;
    }
    o = out;
    for (ln = a; ln <= z; ln++) {
        int n = split_(b->l[ln].s, b->l[ln].len, c);
        *o++ = '|';
        for (i = 0; i < nc; i++) {
            long cw, pad, lp, k;
            if (ln == a + 1) {
                *o++ = ' ';
                *o++ = al[i] == 'l' || al[i] == 'c' ? ':' : '-';
                for (k = 0; k < w[i] - 2; k++) *o++ = '-';
                *o++ = al[i] == 'r' || al[i] == 'c' ? ':' : '-';
                *o++ = ' ';
                *o++ = '|';
                continue;
            }
            cw = i < n ? cellw(c[i].s, c[i].len, b->enc) : 0;
            pad = w[i] - cw;
            lp = al[i] == 'r' ? pad : al[i] == 'c' ? pad / 2 : 0;
            *o++ = ' ';
            for (k = 0; k < lp; k++) *o++ = ' ';
            if (i < n) {
                memcpy(o, c[i].s, c[i].len);
                o += c[i].len;
            }
            for (k = 0; k < pad - lp; k++) *o++ = ' ';
            *o++ = ' ';
            *o++ = '|';
        }
        if (ln < z) *o++ = '\n';
    }
    {
        Pos p, q;
        long cl = b->cur.line;
        p.line = a;
        p.col = 0;
        q.line = z;
        q.col = b->l[z].len;
        buf_group(b);
        buf_delete(b, p, q);
        buf_insert(b, p, out, o - out);
        buf_group(b);
        b->cur.line = cl;
        b->cur.col = 0;
        b->sel = 0;
        wantx(b);
    }
    FreeVec(out);
    say("the table made neat", 0);
}

/* ---- a table of contents ------------------------------------------------------------- */

void md_toc(Buf *b)
{
    Doc *d;
    long i, total = 2;
    int min = 9, h1 = 0;
    char *t, *o;
    if (!view_doc(b, 100) || !(d = b->doc)->nheads) {
        say("no headings to list", 1);
        return;
    }
    for (i = 0; i < d->nheads; i++) {
        if (d->heads[i].level == 1) h1++;
        total += (long)strlen(md_str(d, d->heads[i].text)) * 2 + 40;
    }
    for (i = 0; i < d->nheads; i++)
        if (d->heads[i].level < min && !(h1 == 1 && d->heads[i].level == 1)) min = d->heads[i].level;
    if (!(t = AllocVec(total, MEMF_ANY))) return;
    o = t;
    for (i = 0; i < d->nheads; i++) {
        int lv = d->heads[i].level, k;
        const char *s = md_str(d, d->heads[i].text), *an = md_str(d, d->heads[i].anchor);
        if (lv < min || lv > min + 2) continue;
        for (k = 0; k < (lv - min) * 2; k++) *o++ = ' ';
        *o++ = '-';
        *o++ = ' ';
        *o++ = '[';
        strcpy(o, s);
        o += strlen(s);
        *o++ = ']';
        *o++ = '(';
        *o++ = '#';
        strcpy(o, an);
        o += strlen(an);
        *o++ = ')';
        *o++ = '\n';
    }
    {
        long cn;
        char *c = convert_text(t, o - t, ENC_UTF8, b->enc, &cn);
        if (c) {
            buf_group(b);
            b->sel = 0;
            instext(b, c, cn);
            buf_group(b);
            FreeVec(c);
            wantx(b);
            say("the headings listed", 0);
        }
    }
    FreeVec(t);
}
