/* clip - the Amiga clipboard (clipboard.device unit 0, IFF FTXT CHRS),
 * shared with every Workbench program. The clipboard's text is Latin-1
 * by custom: a UTF-8 file's text goes there as Latin-1 ('?' for what
 * Latin-1 lacks), and Sumi keeps the exact copy itself, so a copy and
 * paste between its own files loses nothing. */
#include <exec/memory.h>
#include <devices/clipboard.h>
#include <proto/exec.h>
#include <string.h>
#include "sumi.h"

static struct MsgPort *cport;
static struct IOClipReq *creq;
static int copen;
static char *mine;              /* the exact text last copied, */
static long minelen;
static int mineenc;
static char *minelat;           /* and what went to the clipboard for it */
static long minelatlen;

static int clip_open(void)
{
    if (copen) return 1;
    if (!(cport = CreateMsgPort())) return 0;
    if (!(creq = (struct IOClipReq *)CreateIORequest(cport, sizeof(*creq)))) {
        DeleteMsgPort(cport);
        return 0;
    }
    if (OpenDevice((STRPTR)"clipboard.device", 0, (struct IORequest *)creq, 0)) {
        DeleteIORequest((struct IORequest *)creq);
        DeleteMsgPort(cport);
        return 0;
    }
    copen = 1;
    return 1;
}

void clip_exit(void)
{
    if (copen) {
        CloseDevice((struct IORequest *)creq);
        DeleteIORequest((struct IORequest *)creq);
        DeleteMsgPort(cport);
        copen = 0;
    }
    if (mine) FreeVec(mine);
    if (minelat) FreeVec(minelat);
    mine = minelat = NULL;
}

/* UTF-8 to Latin-1 in place-safe output (dst as long as src) */
static long tolatin(const char *s, long n, char *d)
{
    long i = 0, o = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0xC2 && c <= 0xF4) {
            int m = c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4, k;
            unsigned long u = c & (0x7F >> m);
            for (k = 1; k < m && i + k < n && (s[i + k] & 0xC0) == 0x80; k++)
                u = (u << 6) | (s[i + k] & 0x3F);
            if (k == m) {
                d[o++] = u <= 0xFF ? (char)u : '?';
                i += m;
                continue;
            }
        }
        d[o++] = (char)c;
        i++;
    }
    return o;
}

static void put_be(UBYTE *p, ULONG v)
{
    p[0] = (UBYTE)(v >> 24);
    p[1] = (UBYTE)(v >> 16);
    p[2] = (UBYTE)(v >> 8);
    p[3] = (UBYTE)v;
}

static ULONG get_be(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

void clip_put(const char *t, long n, int enc)
{
    UBYTE *form;
    long lat, total;
    if (mine) FreeVec(mine);
    if (minelat) FreeVec(minelat);
    mine = minelat = NULL;
    if (!(mine = AllocVec(n + 1, MEMF_ANY))) return;
    memcpy(mine, t, n);
    minelen = n;
    mineenc = enc;
    if (!(minelat = AllocVec(n + 1, MEMF_ANY))) return;
    if (enc == ENC_UTF8) lat = tolatin(t, n, minelat);
    else {
        memcpy(minelat, t, n);
        lat = n;
    }
    minelatlen = lat;
    if (!clip_open()) return;
    total = 12 + 8 + lat + (lat & 1);
    if (!(form = AllocVec(total, MEMF_ANY))) return;
    memcpy(form, "FORM", 4);
    put_be(form + 4, total - 8);
    memcpy(form + 8, "FTXT", 4);
    memcpy(form + 12, "CHRS", 4);
    put_be(form + 16, lat);
    memcpy(form + 20, minelat, lat);
    if (lat & 1) form[20 + lat] = 0;
    creq->io_Command = CMD_WRITE;
    creq->io_Data = (STRPTR)form;
    creq->io_Length = total;
    creq->io_Offset = 0;
    creq->io_ClipID = 0;
    DoIO((struct IORequest *)creq);
    creq->io_Command = CMD_UPDATE;
    DoIO((struct IORequest *)creq);
    FreeVec(form);
}

/* Latin-1 to UTF-8 (dst three times... twice as long is enough) */
static long toutf8(const char *s, long n, char *d)
{
    long i, o = 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) d[o++] = (char)c;
        else {
            d[o++] = (char)(0xC0 | (c >> 6));
            d[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    return o;
}

/* text as the buffer wants it: from enc 'from' to 'to' (AllocVec'd) */
char *convert_text(const char *t, long n, int from, int to, long *out);

char *convert_text(const char *t, long n, int from, int to, long *out)
{
    char *d = AllocVec(n * 2 + 1, MEMF_ANY);
    long i;
    if (!d) return NULL;
    if (from == to) {
        memcpy(d, t, n);
        *out = n;
    } else if (to == ENC_UTF8) *out = toutf8(t, n, d);
    else *out = tolatin(t, n, d);
    for (i = 0; i < *out; i++)       /* lines as Sumi keeps them */
        if (d[i] == '\r') d[i] = '\n';
    d[*out] = 0;
    return d;
}

/* valid UTF-8 beyond ASCII? (clipboard text from a program of today) */
static int looksutf8(const UBYTE *t, long n)
{
    long i = 0;
    int high = 0;
    while (i < n) {
        int m, k;
        if (t[i] < 0x80) {
            i++;
            continue;
        }
        m = t[i] >= 0xC2 && t[i] <= 0xDF ? 1 : t[i] >= 0xE0 && t[i] <= 0xEF ? 2 :
            t[i] >= 0xF0 && t[i] <= 0xF4 ? 3 : -1;
        if (m < 0) return 0;
        for (k = 1; k <= m; k++)
            if (i + k >= n || (t[i + k] & 0xC0) != 0x80) return 0;
        high = 1;
        i += m + 1;
    }
    return high;
}

/* a paste through the terminal that is what Sumi last put on the
 * clipboard: the exact copy (the clipboard has only Latin-1) */
char *clip_mine(const char *t, long n, int enc, long *out)
{
    long i, j;
    if (!mine) return NULL;
    /* the terminal sends new lines as CRs */
    if (n != minelatlen) return NULL;
    for (i = 0, j = 0; i < n; i++, j++) {
        char a = t[i], c = minelat[j];
        if (a == '\r') a = '\n';
        if (a != c) return NULL;
    }
    return convert_text(mine, minelen, mineenc, enc, out);
}

char *clip_get(long *n, int enc)
{
    UBYTE head[12], *data = NULL, scratch[64];
    ULONG formlen;
    long got = 0;
    char *res = NULL;
    *n = 0;
    if (!clip_open()) goto own;
    creq->io_Command = CMD_READ;
    creq->io_Data = (STRPTR)head;
    creq->io_Length = 12;
    creq->io_Offset = 0;
    creq->io_ClipID = 0;
    DoIO((struct IORequest *)creq);
    if (creq->io_Actual == 12 && !memcmp(head, "FORM", 4) && !memcmp(head + 8, "FTXT", 4)) {
        formlen = get_be(head + 4);
        if (formlen > 4 && formlen < 0x1000000 && (data = AllocVec(formlen, MEMF_ANY))) {
            creq->io_Command = CMD_READ;
            creq->io_Data = (STRPTR)data;
            creq->io_Length = formlen - 4;
            DoIO((struct IORequest *)creq);
            got = creq->io_Actual;
        }
    }
    do {                                /* read it dry, or it stays held */
        creq->io_Command = CMD_READ;
        creq->io_Data = (STRPTR)scratch;
        creq->io_Length = sizeof(scratch);
        DoIO((struct IORequest *)creq);
    } while (!creq->io_Error && creq->io_Actual > 0);
    if (data) {
        long o = 0;
        while (o + 8 <= got) {
            ULONG id = get_be(data + o), sz = get_be(data + o + 4);
            if (sz > (ULONG)(got - o - 8)) sz = got - o - 8;
            if (id == 0x43485253) {     /* CHRS */
                UBYTE *t = data + o + 8;
                /* the clipboard still holds what Sumi put there: the exact copy */
                if (mine && (long)sz == minelatlen && !memcmp(t, minelat, sz))
                    res = convert_text(mine, minelen, mineenc, enc, n);
                else
                    res = convert_text((char *)t, sz, looksutf8(t, sz) ? ENC_UTF8 : ENC_LATIN1, enc, n);
                break;
            }
            o += 8 + sz + (sz & 1);
        }
        FreeVec(data);
    }
    if (res) return res;
own:
    if (mine) return convert_text(mine, minelen, mineenc, enc, n);
    return NULL;
}
