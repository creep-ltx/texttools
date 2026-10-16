/* file - loading and saving. A file is kept as it is: its characters
 * (Latin-1, or UTF-8 when it is valid UTF-8 with something beyond
 * ASCII), its line ends (LF, or CR LF when every line has them), and
 * whether it ends with a new line. Saving goes to a scratch file beside
 * it first; only a whole copy replaces the old one, which keeps its
 * protection bits and comment. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include "sumi.h"

static char errbuf[100];

const char *file_err(void)
{
    return errbuf;
}

static void seterr(const char *what)
{
    char e[80];
    LONG io = IoErr();
    strcpy(errbuf, what);
    if (io && Fault(io, NULL, (STRPTR)e, sizeof(e))) {
        strcat(errbuf, ": ");
        strncat(errbuf, e, sizeof(errbuf) - strlen(errbuf) - 1);
    }
}

const char *basename_(const char *p)
{
    const char *s = p, *q;
    for (q = p; *q; q++)
        if (*q == '/' || *q == ':') s = q + 1;
    return s;
}

/* valid UTF-8 with at least one character beyond ASCII */
static int isutf8(const unsigned char *t, long n)
{
    long i = 0;
    int high = 0;
    while (i < n) {
        unsigned char c = t[i];
        int k, m;
        if (c < 0x80) {
            i++;
            continue;
        }
        if (c >= 0xC2 && c <= 0xDF) m = 1;
        else if (c >= 0xE0 && c <= 0xEF) m = 2;
        else if (c >= 0xF0 && c <= 0xF4) m = 3;
        else return 0;
        for (k = 1; k <= m; k++)
            if (i + k >= n || (t[i + k] & 0xC0) != 0x80) return 0;
        high = 1;
        i += m + 1;
    }
    return high;
}

int file_load(Buf *b, const char *path)
{
    BPTR lock, fh;
    struct FileInfoBlock *fib;
    long size, i, start, lines = 1, crs = 0;
    char *t;
    errbuf[0] = 0;
    strncpy(b->path, path, PATHMAX - 1);
    b->path[PATHMAX - 1] = 0;
    if (!(lock = Lock((STRPTR)path, SHARED_LOCK))) {
        if (IoErr() == ERROR_OBJECT_NOT_FOUND) return 0;   /* a new file */
        seterr("cannot open it");
        return -1;
    }
    if (!(fib = AllocDosObject(DOS_FIB, NULL))) {
        UnLock(lock);
        strcpy(errbuf, "out of memory");
        return -1;
    }
    if (!Examine(lock, fib) || fib->fib_DirEntryType > 0) {
        strcpy(errbuf, fib->fib_DirEntryType > 0 ? "that is a drawer" : "cannot look at it");
        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        return -1;
    }
    size = fib->fib_Size;
    b->prot = fib->fib_Protection;
    b->disk = fib->fib_Date;
    strncpy(b->comment, (char *)fib->fib_Comment, sizeof(b->comment) - 1);
    b->comment[sizeof(b->comment) - 1] = 0;
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    if (!(t = AllocVec(size + 1, MEMF_ANY))) {
        strcpy(errbuf, "not enough memory for it");
        return -1;
    }
    if (!(fh = Open((STRPTR)path, MODE_OLDFILE))) {
        seterr("cannot open it");
        FreeVec(t);
        return -1;
    }
    if (size && Read(fh, t, size) != size) {
        seterr("cannot read it");
        Close(fh);
        FreeVec(t);
        return -1;
    }
    Close(fh);
    b->hadfile = 1;
    b->enc = isutf8((unsigned char *)t, size) ? ENC_UTF8 : ENC_LATIN1;
    for (i = 0; i < size; i++)
        if (t[i] == '\n') {
            lines++;
            if (i > 0 && t[i - 1] == '\r') crs++;
        }
    b->finalnl = size == 0 || t[size - 1] == '\n';
    b->crlf = crs > 0 && crs == lines - 1;
    /* the text in: one insert, then no undo for it */
    start = 0;
    if (b->crlf) {                  /* the CRs go (they come back on saving) */
        long o = 0;
        for (i = 0; i < size; i++)
            if (!(t[i] == '\r' && i + 1 < size && t[i + 1] == '\n')) t[o++] = t[i];
        size = o;
    }
    if (b->finalnl && size > 0) size--;     /* the last line's new line is implied */
    (void)start;
    if (!buf_load(b, t, size)) {
        FreeVec(t);
        strcpy(errbuf, "not enough memory for it");
        return -1;
    }
    buf_clearundo(b);
    b->cur.line = b->cur.col = 0;
    b->hsok = 0;
    return 1;
}

int file_save(Buf *b, const char *path)
{
    char tmp[PATHMAX + 20], old[PATHMAX + 20], dir[PATHMAX];
    BPTR fh, lock;
    long i;
    int exists, ok = 1;
    UBYTE *wbuf;
    long wl = 0;
    errbuf[0] = 0;
    /* scratch names beside it, short (OFS takes 30 characters) */
    strncpy(dir, path, PATHMAX - 1);
    dir[PATHMAX - 1] = 0;
    dir[basename_(path) - path] = 0;
    {
        char tag[12];
        ULONG v = (ULONG)FindTask(NULL), k;
        for (k = 0; k < 8; k++) tag[k] = "0123456789abcdef"[(v >> (28 - 4 * k)) & 15];
        tag[8] = 0;
        strcpy(tmp, dir);
        strcat(tmp, "sumi-new-");
        strcat(tmp, tag);
        strcpy(old, dir);
        strcat(old, "sumi-old-");
        strcat(old, tag);
    }
    lock = Lock((STRPTR)path, SHARED_LOCK);
    exists = lock != 0;
    if (lock) UnLock(lock);
    if (!(wbuf = AllocVec(8192, MEMF_ANY))) {
        strcpy(errbuf, "out of memory");
        return 0;
    }
    if (!(fh = Open((STRPTR)(exists ? tmp : path), MODE_NEWFILE))) {
        seterr("cannot write it");
        FreeVec(wbuf);
        return 0;
    }
#define PUT(p, n) do { long nn = (n); const char *pp = (p); \
        while (nn > 0) { long m = nn > 8192 - wl ? 8192 - wl : nn; memcpy(wbuf + wl, pp, m); \
        wl += m; pp += m; nn -= m; if (wl == 8192) { if (Write(fh, wbuf, wl) != wl) ok = 0; wl = 0; } } } while (0)
    for (i = 0; i < b->n && ok; i++) {
        PUT(b->l[i].s, b->l[i].len);
        if (i < b->n - 1 || b->finalnl) {
            if (b->crlf) PUT("\r\n", 2);
            else PUT("\n", 1);
        }
    }
    if (ok && wl && Write(fh, wbuf, wl) != wl) ok = 0;
#undef PUT
    FreeVec(wbuf);
    if (!ok) seterr("writing failed");
    if (!Close(fh) && ok) {
        seterr("writing failed");
        ok = 0;
    }
    if (!ok) {
        DeleteFile((STRPTR)(exists ? tmp : path));
        return 0;
    }
    if (exists) {
        /* the whole new copy is there: it takes the old one's name */
        if (!Rename((STRPTR)path, (STRPTR)old)) {
            seterr("cannot replace it");
            DeleteFile((STRPTR)tmp);
            return 0;
        }
        if (!Rename((STRPTR)tmp, (STRPTR)path)) {
            seterr("cannot replace it");
            Rename((STRPTR)old, (STRPTR)path);
            DeleteFile((STRPTR)tmp);
            return 0;
        }
        if (backups) {
            /* the old version kept beside it as NAME.bak (the last one) */
            char bak[PATHMAX + 8];
            strncpy(bak, path, PATHMAX - 1);
            bak[PATHMAX - 1] = 0;
            strcat(bak, ".bak");
            DeleteFile((STRPTR)bak);
            if (!Rename((STRPTR)old, (STRPTR)bak)) DeleteFile((STRPTR)old);
        } else
            DeleteFile((STRPTR)old);
        SetProtection((STRPTR)path, b->prot);
        if (b->comment[0]) SetComment((STRPTR)path, (STRPTR)b->comment);
    }
    strncpy(b->path, path, PATHMAX - 1);
    b->path[PATHMAX - 1] = 0;
    b->hadfile = 1;
    b->saved = b->undo ? b->undo->serial : 0;
    return 1;
}
