/* src - what Maki shows: a file (read a piece at a time through a small
 * cache, so a big file is never read whole), a pipe (read with a packet
 * of our own as the bytes come, kept in memory blocks), or text of our
 * own (the help).
 *
 * The line index - where each line starts, and the ANSI colours in force
 * there - is built only as far as it is needed, and further in idle time,
 * so the first page shows at once whatever the size. */
#include <exec/memory.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include "maki.h"

#define BLK   32768             /* a pipe's memory block */
#define CB    16384             /* a file cache slot */
#define NC    8
#define RBUF  4096              /* one read from a pipe */
#define SCAN  65536             /* indexed in one go when a line is wanted */

static struct {
    Src *s;
    long off, len;
    ULONG use;
    UBYTE *d;
} cache[NC];
static ULONG usectr;
static UBYTE *cmem, *sbuf;

int src_init(void)
{
    int i;
    if (!(cmem = AllocVec(NC * CB + 4096, MEMF_ANY))) return 0;
    for (i = 0; i < NC; i++) cache[i].d = cmem + i * CB;
    sbuf = cmem + NC * CB;
    return 1;
}

void src_exit(void)
{
    if (cmem) FreeVec(cmem);
    cmem = NULL;
}

static void uncache(Src *s)
{
    int i;
    for (i = 0; i < NC; i++)
        if (cache[i].s == s) cache[i].s = NULL;
}

/* ---- ANSI colours ------------------------------------------------------- */

/* a 256-colour or 24-bit colour as the nearest of the sixteen */
static int near16(int r, int g, int b)
{
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int c = (r >= 128 ? 1 : 0) | (g >= 128 ? 2 : 0) | (b >= 128 ? 4 : 0);
    if (mx < 64) return 0;
    if (!c) return 8;                           /* dim: dark grey */
    return mx > 200 ? c + 8 : c;
}

static int c256(int n)
{
    static const UBYTE lv[6] = { 0, 95, 135, 175, 215, 255 };
    if (n < 16) return n;
    if (n < 232) {
        n -= 16;
        return near16(lv[n / 36], lv[n / 6 % 6], lv[n % 6]);
    }
    n = 8 + (n - 232) * 10;                     /* the greys */
    return n < 60 ? 0 : n < 140 ? 8 : n < 220 ? 7 : 15;
}

UWORD sgr_apply(UWORD st, const int *pv, int n)
{
    int fg = ST_FG(st), bg = ST_BG(st), at = ST_AT(st), i;
    for (i = 0; i < n; i++) {
        int p = pv[i];
        if (p == 0) fg = bg = TC_DEFAULT, at = 0;
        else if (p == 1) at |= TA_BOLD;
        else if (p == 4) at |= TA_UNDER;
        else if (p == 7) at |= TA_REV;
        else if (p == 22) at &= ~TA_BOLD;
        else if (p == 24) at &= ~TA_UNDER;
        else if (p == 27) at &= ~TA_REV;
        else if (p >= 30 && p <= 37) fg = p - 30;
        else if (p == 39) fg = TC_DEFAULT;
        else if (p >= 40 && p <= 47) bg = p - 40;
        else if (p == 49) bg = TC_DEFAULT;
        else if (p >= 90 && p <= 97) fg = p - 90 + 8;
        else if (p >= 100 && p <= 107) bg = p - 100 + 8;
        else if ((p == 38 || p == 48) && i + 1 < n) {
            int c = -1;
            if (pv[i + 1] == 5 && i + 2 < n) c = c256(pv[i + 2] & 255), i += 2;
            else if (pv[i + 1] == 2 && i + 4 < n) c = near16(pv[i + 2], pv[i + 3], pv[i + 4]), i += 4;
            if (c >= 0) {
                if (p == 38) fg = c;
                else bg = c;
            }
        }
    }
    return (UWORD)(fg | bg << 5 | at << 10);
}

/* ---- the sources --------------------------------------------------------- */

static Src *newsrc(int kind)
{
    Src *s = AllocVec(sizeof(Src), MEMF_ANY | MEMF_CLEAR);
    if (!s) return NULL;
    s->kind = kind;
    s->lcap = 1024;
    s->lo = AllocVec(s->lcap * 4, MEMF_ANY);
    s->ls = AllocVec(s->lcap * 2, MEMF_ANY);
    if (!s->lo || !s->ls) {
        src_free(s);
        return NULL;
    }
    src_reset(s);
    return s;
}

void src_reset(Src *s)
{
    s->nl = 1;
    s->lo[0] = 0;
    s->ls[0] = ST_DEFAULT;
    s->st = ST_DEFAULT;
    s->scanned = 0;
    s->esc = 0;
}

static int endswith(const char *s, const char *e)
{
    int a = (int)strlen(s), b = (int)strlen(e), i;
    if (a < b) return 0;
    for (i = 0; i < b; i++) {
        int c = (unsigned char)s[a - b + i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != e[i]) return 0;
    }
    return 1;
}

/* Latin-1, UTF-8 or the PC's CP437, by looking at the start */
void src_guess(Src *s)
{
    long n = src_get(s, 0, sbuf, 4096), i;
    int utf = 0, bad = 0;
    if (s->encset) return;
    for (i = 0; i < n; i++) {
        int c = sbuf[i], k, need;
        if (c < 0x80) continue;
        need = c >= 0xC2 && c <= 0xDF ? 1 : c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : -1;
        if (need < 0) {
            bad++;
            continue;
        }
        for (k = 1; k <= need && i + k < n; k++)
            if ((sbuf[i + k] & 0xC0) != 0x80) break;
        if (k <= need && i + k < n) bad++;
        else utf++;
        i += k - 1;
    }
    if (utf && !bad) s->enc = ENC_UTF8;
    else if (endswith(s->name, ".ans") || endswith(s->name, ".nfo") ||
             endswith(s->name, ".diz") || endswith(s->name, ".asc"))
        s->enc = ENC_CP437;
    else
        s->enc = ENC_LATIN1;
}

Src *src_file(const char *path, char *err)
{
    BPTR l = Lock((STRPTR)path, SHARED_LOCK);
    struct FileInfoBlock *fib;
    Src *s;
    err[0] = 0;
    if (!l) {
        Fault(IoErr(), (STRPTR)"", (STRPTR)err, 80);
        return NULL;
    }
    if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL) {
        int dir = Examine(l, fib) && fib->fib_DirEntryType > 0;
        FreeDosObject(DOS_FIB, fib);
        if (dir) {
            UnLock(l);
            strcpy(err, ": is a drawer");
            return NULL;
        }
    }
    UnLock(l);
    if (!(s = newsrc(SK_FILE))) {
        strcpy(err, ": out of memory");
        return NULL;
    }
    strncpy(s->name, path, PATHMAX - 1);
    strncpy(s->title, path, PATHMAX - 1);
    if (!(s->fh = Open((STRPTR)path, MODE_OLDFILE))) {
        Fault(IoErr(), (STRPTR)"", (STRPTR)err, 80);
        src_free(s);
        return NULL;
    }
    Seek(s->fh, 0, OFFSET_END);
    s->size = Seek(s->fh, 0, OFFSET_BEGINNING);
    if (s->size < 0) s->size = 0;
    src_guess(s);
    return s;
}

static int addblock(Src *s)
{
    if (s->nblk == s->blkcap) {
        long nc = s->blkcap ? s->blkcap * 2 : 16;
        UBYTE **nb = AllocVec(nc * sizeof(UBYTE *), MEMF_ANY);
        if (!nb) return 0;
        if (s->blk) {
            memcpy(nb, s->blk, s->nblk * sizeof(UBYTE *));
            FreeVec(s->blk);
        }
        s->blk = nb;
        s->blkcap = nc;
    }
    if (!(s->blk[s->nblk] = AllocVec(BLK, MEMF_ANY))) return 0;
    s->nblk++;
    return 1;
}

static int append(Src *s, const UBYTE *d, long n)
{
    while (n > 0) {
        long in = s->size % BLK, k = BLK - in;
        if (s->size / BLK >= s->nblk && !addblock(s)) return 0;
        if (k > n) k = n;
        memcpy(s->blk[s->size / BLK] + in, d, k);
        s->size += k;
        d += k;
        n -= k;
    }
    return 1;
}

Src *src_text(const char *t, long n, const char *title)
{
    Src *s = newsrc(SK_MEM);
    if (!s) return NULL;
    strncpy(s->title, title, PATHMAX - 1);
    if (!append(s, (const UBYTE *)t, n)) {
        src_free(s);
        return NULL;
    }
    s->done = 1;
    s->enc = ENC_LATIN1;
    s->encset = 1;
    return s;
}

static void pipe_send(Src *s)
{
    struct FileHandle *fh = BADDR(s->in);
    s->pkt->dp_Type = ACTION_READ;
    s->pkt->dp_Arg1 = fh->fh_Arg1;
    s->pkt->dp_Arg2 = (LONG)s->rbuf;
    s->pkt->dp_Arg3 = RBUF;
    SendPkt(s->pkt, fh->fh_Type, s->port);
    s->pending = 1;
}

Src *src_pipe(BPTR in)
{
    Src *s = newsrc(SK_MEM);
    struct FileHandle *fh;
    if (!s) return NULL;
    strcpy(s->title, "(pipe)");
    s->in = in;
    fh = BADDR(in);
    if (!in || !fh->fh_Type) {          /* NIL: */
        s->done = 1;
        s->enc = ENC_LATIN1;
        return s;
    }
    /* what DOS has buffered already is the start */
    if (fh->fh_Buf && fh->fh_Pos < fh->fh_End && fh->fh_Pos >= 0)
        append(s, (UBYTE *)BADDR(fh->fh_Buf) + fh->fh_Pos, fh->fh_End - fh->fh_Pos);
    s->port = CreateMsgPort();
    s->pkt = AllocDosObject(DOS_STDPKT, NULL);
    s->rbuf = AllocVec(RBUF, MEMF_PUBLIC);
    if (!s->port || !s->pkt || !s->rbuf) {
        s->done = 1;
        return s;
    }
    pipe_send(s);
    return s;
}

int src_poll(Src *s)
{
    int got = 0;
    if (s->kind != SK_MEM || !s->pending) return 0;
    while (s->pending && GetMsg(s->port)) {
        long n = s->pkt->dp_Res1;
        s->pending = 0;
        if (n > 0 && append(s, s->rbuf, n)) {
            got = 1;
            pipe_send(s);
        } else
            s->done = 1;
        got = 1;
    }
    if (got && s->enc == ENC_AUTO && (s->size >= 4096 || s->done)) {
        src_guess(s);
        src_reset(s);                   /* indexed again knowing the encoding */
    }
    return got;
}

ULONG src_sigs(Src *s)
{
    return s->kind == SK_MEM && s->pending ? 1UL << s->port->mp_SigBit : 0;
}

int src_grow(Src *s)
{
    long n;
    if (s->kind != SK_FILE) return 0;
    Seek(s->fh, 0, OFFSET_END);
    n = Seek(s->fh, 0, OFFSET_CURRENT);
    if (n == s->size) return 0;
    if (n < s->size) src_reset(s);     /* written anew: from the start */
    s->size = n;
    uncache(s);
    return 1;
}

void src_free(Src *s)
{
    long i;
    if (!s) return;
    uncache(s);
    if (s->pending) {
        /* a read still out: a moment for it, else the port, the packet and
         * the buffer stay (the pipe may write into them later); no signal */
        for (i = 0; i < 20 && !GetMsg(s->port); i++) Delay(2);
        if (i == 20) {
            s->port->mp_Flags = PA_IGNORE;
            s->port = NULL;
            s->pkt = NULL;
            s->rbuf = NULL;
        }
    }
    if (s->pkt) FreeDosObject(DOS_STDPKT, s->pkt);
    if (s->port) DeleteMsgPort(s->port);
    if (s->rbuf) FreeVec(s->rbuf);
    if (s->fh) Close(s->fh);
    for (i = 0; i < s->nblk; i++) FreeVec(s->blk[i]);
    if (s->blk) FreeVec(s->blk);
    if (s->lo) FreeVec(s->lo);
    if (s->ls) FreeVec(s->ls);
    FreeVec(s);
}

long src_get(Src *s, long off, UBYTE *d, long n)
{
    long got = 0;
    if (off >= s->size) return 0;
    if (off + n > s->size) n = s->size - off;
    if (s->kind == SK_MEM) {
        while (got < n) {
            long in = (off + got) % BLK, k = BLK - in;
            if (k > n - got) k = n - got;
            memcpy(d + got, s->blk[(off + got) / BLK] + in, k);
            got += k;
        }
        return got;
    }
    while (got < n) {
        long at = (off + got) / CB * CB, in = off + got - at, k;
        int i, best = 0;
        for (i = 0; i < NC; i++)
            if (cache[i].s == s && cache[i].off == at) break;
        if (i == NC) {
            for (i = 0; i < NC; i++) {
                if (!cache[i].s) {
                    best = i;
                    break;
                }
                if (cache[i].use < cache[best].use) best = i;
            }
            i = best;
            cache[i].s = s;
            cache[i].off = at;
            Seek(s->fh, at, OFFSET_BEGINNING);
            cache[i].len = Read(s->fh, cache[i].d, CB);
            if (cache[i].len < 0) cache[i].len = 0;
        }
        cache[i].use = ++usectr;
        if (in >= cache[i].len) break;
        k = cache[i].len - in;
        if (k > n - got) k = n - got;
        memcpy(d + got, cache[i].d + in, k);
        got += k;
    }
    return got;
}

/* ---- the line index -------------------------------------------------------- */

static int addline(Src *s, ULONG off)
{
    if (s->nl == s->lcap) {
        long nc = s->lcap * 2;
        ULONG *lo = AllocVec(nc * 4, MEMF_ANY);
        UWORD *ls = AllocVec(nc * 2, MEMF_ANY);
        if (!lo || !ls) {
            if (lo) FreeVec(lo);
            if (ls) FreeVec(ls);
            return 0;
        }
        memcpy(lo, s->lo, s->nl * 4);
        memcpy(ls, s->ls, s->nl * 2);
        FreeVec(s->lo);
        FreeVec(s->ls);
        s->lo = lo;
        s->ls = ls;
        s->lcap = nc;
    }
    s->lo[s->nl] = off;
    s->ls[s->nl] = s->st;
    s->nl++;
    return 1;
}

/* the colours only matter for what is drawn, but they carry from line to
 * line: the escape sequences are followed while indexing */
static UBYTE special[256];      /* bytes the indexer stops at: \n, ESC, CSI */

void src_scan(Src *s, long bytes)
{
    int csi9b = s->enc != ENC_UTF8;
    special['\n'] = special[27] = special[0x9B] = 1;
    while (bytes > 0 && s->scanned < s->size) {
        long n = src_get(s, s->scanned, sbuf, bytes < 4096 ? bytes : 4096), i;
        if (n <= 0) break;
        for (i = 0; i < n; i++) {
            int c;
            if (!s->esc) {              /* the plain stretch, quickly */
                while (i < n && !special[sbuf[i]]) i++;
                if (i == n) break;
                if (sbuf[i] == 0x9B && !csi9b) continue;
            }
            c = sbuf[i];
            if (c == '\n') {
                s->esc = 0;
                if (!addline(s, s->scanned + i + 1)) return;
                continue;
            }
            switch (s->esc) {
            case 0:
                if (c == 27) s->esc = 1;
                else if (c == 0x9B && csi9b) s->esc = 2, s->np = 0, s->pv[0] = 0;
                break;
            case 1:
                if (c == '[') s->esc = 2, s->np = 0, s->pv[0] = 0;
                else if (c == ']') s->esc = 3;
                else s->esc = 0;
                break;
            case 2:
                if (c >= '0' && c <= '9') s->pv[s->np] = s->pv[s->np] * 10 + c - '0';
                else if (c == ';') {
                    if (s->np < 15) s->np++;
                    s->pv[s->np] = 0;
                } else if (c >= 0x40 && c <= 0x7E) {
                    if (c == 'm') s->st = sgr_apply(s->st, s->pv, s->np + 1);
                    s->esc = 0;
                }
                break;
            case 3:                     /* OSC: to BEL or ESC \ */
                if (c == 7) s->esc = 0;
                else if (c == 27) s->esc = 1;
                break;
            }
        }
        s->scanned += n;
        bytes -= n;
    }
}

int src_line(Src *s, long i)
{
    while (i + 1 >= s->nl && s->scanned < s->size) src_scan(s, SCAN);
    return i < src_lines(s);
}

long src_lineend(Src *s, long i)
{
    return i + 1 < s->nl ? (long)s->lo[i + 1] : s->scanned;
}

long src_lines(Src *s)
{
    return s->nl - ((long)s->lo[s->nl - 1] >= s->scanned ? 1 : 0);
}

int src_full(Src *s)
{
    return s->scanned >= s->size && (s->kind == SK_FILE || s->done);
}

long src_lineat(Src *s, long off)
{
    long a = 0, z;
    while (s->scanned <= off && s->scanned < s->size) src_scan(s, SCAN);
    z = s->nl - 1;
    while (a < z) {
        long m = (a + z + 1) / 2;
        if ((long)s->lo[m] <= off) a = m;
        else z = m - 1;
    }
    return a;
}
