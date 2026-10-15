/* drawer - Maki given a drawer: its contents as ls -l shows them (bits,
 * size, date, name), the names coloured by kind from ENVARC:FileColours as
 * ls, Ahiru and the rest of the family colour them; natural order, case
 * blind. The text is Maki's own (src_text): searching, marks and the rest
 * work on it as on any file. No colours when the output is not a console. */
#include <exec/memory.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include "maki.h"
#include "filecol.h"

typedef struct {
    char *name;
    LONG type, prot, size;
    struct DateStamp date;
} Ent;

static int lc(int c)
{
    c &= 0xff;
    if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7)) c += 32;
    return c;
}

/* natural, case blind: "file9" before "file10" (as ls sorts) */
static int natcmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
            const char *x = a, *y = b;
            int lx, ly;
            while (*x == '0') x++;
            while (*y == '0') y++;
            for (lx = 0; x[lx] >= '0' && x[lx] <= '9'; lx++) ;
            for (ly = 0; y[ly] >= '0' && y[ly] <= '9'; ly++) ;
            if (lx != ly) return lx - ly;
            for (; lx; lx--, x++, y++) if (*x != *y) return *x - *y;
            a = x; b = y;
            continue;
        }
        if (lc(*a) != lc(*b)) return lc(*a) - lc(*b);
        a++; b++;
    }
    return lc(*a) - lc(*b);
}

/* a merge sort, as ls's: stable, and fast enough for a drawer of thousands */
static void sortents(Ent *e, long n)
{
    Ent *t;
    long w, i;
    if (n < 2 || !(t = AllocVec(n * sizeof *t, MEMF_ANY))) return;
    for (w = 1; w < n; w *= 2) {
        for (i = 0; i < n; i += 2 * w) {
            long a = i, am = i + w < n ? i + w : n, b = am, bm = i + 2 * w < n ? i + 2 * w : n, k = i;
            while (a < am && b < bm) t[k++] = natcmp(e[a].name, e[b].name) <= 0 ? e[a++] : e[b++];
            while (a < am) t[k++] = e[a++];
            while (b < bm) t[k++] = e[b++];
        }
        memcpy(e, t, n * sizeof *e);
    }
    FreeVec(t);
}

/* the text grows in a block of its own, doubled when full */
static char *tb;
static long tn, tcap;

static int add(const char *s, long n)
{
    if (tn + n + 1 > tcap) {
        long nc = tcap ? tcap * 2 : 8192;
        char *nb;
        while (nc < tn + n + 1) nc *= 2;
        if (!(nb = AllocVec(nc, MEMF_ANY))) return 0;
        if (tb) {
            memcpy(nb, tb, tn);
            FreeVec(tb);
        }
        tb = nb;
        tcap = nc;
    }
    memcpy(tb + tn, s, n);
    tn += n;
    return 1;
}

static int adds(const char *s) { return add(s, strlen(s)); }

static void line(Ent *e, int w, int colour)
{
    static const char bits[] = "hsparwed";
    char b[48], date[16], tm[16];
    struct DateTime dt;
    int i, k;
    for (i = 0; i < 8; i++) {
        int bit = 7 - i, set = (e->prot >> bit) & 1;
        /* rwed are set when NOT allowed; hspa when set */
        b[i] = (bit < 4 ? !set : set) ? bits[i] : '-';
    }
    b[8] = ' ';
    k = 9;
    if (e->type > 0) {
        for (i = 0; i < w - 3; i++) b[k++] = ' ';
        strcpy(b + k, "Dir");
        k += 3;
    } else {
        char d[12];
        LONG v = e->size;
        int n = 0;
        do d[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 11);
        for (i = n; i < w; i++) b[k++] = ' ';
        while (n) b[k++] = d[--n];
    }
    b[k] = 0;
    adds(b);
    memset(&dt, 0, sizeof dt);
    dt.dat_Stamp = e->date;
    dt.dat_Format = FORMAT_DOS;
    dt.dat_StrDate = date;
    dt.dat_StrTime = tm;
    date[0] = tm[0] = 0;
    DateToStr(&dt);
    tm[5] = 0;                          /* hh:mm */
    adds(" "); adds(date); adds(" "); adds(tm); adds(" ");
    {
        /* the CTERM column always: Maki takes SGR as ANSI colours and maps
         * them to a plain window's pens itself (tui.c, wbpen) */
        const char *c = colour ? cu_sgr(cu_kind(e->name, e->type, e->prot), 1) : "";
        if (*c) adds(c);
        adds(e->name);
        if (*c) adds("\x1b[0m");
    }
    adds("\n");
}

Src *src_drawer(BPTR lock, const char *path, char *err)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    Ent *e = NULL, *ne;
    char *names = NULL;
    long n = 0, cap = 0, nl = 0, ncap = 0, i;
    int w = 3, colour;
    Src *s = NULL;
    tb = NULL;
    tn = tcap = 0;
    if (!fib || !Examine(lock, fib)) goto out;
    while (ExNext(lock, fib)) {
        int l = strlen(fib->fib_FileName) + 1;
        if (n == cap) {
            long nc = cap ? cap * 2 : 64;
            if (!(ne = AllocVec(nc * sizeof *e, MEMF_ANY))) goto out;
            if (e) { memcpy(ne, e, n * sizeof *e); FreeVec(e); }
            e = ne;
            cap = nc;
        }
        if (nl + l > ncap) {
            long nc = ncap ? ncap * 2 : 4096;
            char *nn;
            while (nc < nl + l) nc *= 2;
            if (!(nn = AllocVec(nc, MEMF_ANY))) goto out;
            if (names) { memcpy(nn, names, nl); FreeVec(names); }
            names = nn;
            ncap = nc;
        }
        memcpy(names + nl, fib->fib_FileName, l);
        e[n].name = (char *)nl;         /* an offset until the names stop moving */
        e[n].type = fib->fib_DirEntryType;
        e[n].prot = fib->fib_Protection;
        e[n].size = fib->fib_Size;
        e[n].date = fib->fib_Date;
        nl += l;
        n++;
    }
    for (i = 0; i < n; i++) {
        LONG v = e[i].size;
        int d = 1;
        e[i].name = names + (long)e[i].name;
        while (v >= 10) { v /= 10; d++; }
        if (e[i].type <= 0 && d > w) w = d;
    }
    sortents(e, n);
    colour = IsInteractive(Output());
    if (colour) cu_colours_load();
    for (i = 0; i < n; i++) line(&e[i], w, colour);
    if (!n) adds("(empty)\n");
    if (tb) s = src_text(tb, tn, path);
out:
    if (!s) strcpy(err, ": could not be listed");
    if (fib) FreeDosObject(DOS_FIB, fib);
    if (e) FreeVec(e);
    if (names) FreeVec(names);
    if (tb) FreeVec(tb);
    tb = NULL;
    return s;
}
