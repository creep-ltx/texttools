/* Maki - a pager that lives inside the terminal (a CTerm tab or pane, or
 * any Amiga console window): less for the Amiga. Shared state and the
 * functions the files call. */
#ifndef MAKI_H
#define MAKI_H

#include <exec/types.h>
#include <dos/dos.h>
#include "tui.h"

#define PATHMAX 256

/* what the bytes are */
enum { ENC_AUTO, ENC_LATIN1, ENC_UTF8, ENC_CP437, ENC_COUNT };

/* the colours in force at a line's start (ANSI colours carry over lines):
 * fg 5 bits, bg 5 bits, TA_* 3 bits */
#define ST_DEFAULT (TC_DEFAULT | TC_DEFAULT << 5)
#define ST_FG(s)   ((s) & 31)
#define ST_BG(s)   (((s) >> 5) & 31)
#define ST_AT(s)   (((s) >> 10) & 7)
UWORD sgr_apply(UWORD st, const int *pv, int n);

/* ---- src.c: what is shown - a file, a pipe, or text of our own ------- */

enum { SK_FILE, SK_MEM };

typedef struct Src {
    int kind;
    char name[PATHMAX];         /* the file ("" for a pipe or our text) */
    char title[PATHMAX];        /* what the status line calls it */
    BPTR fh;                    /* SK_FILE: open while shown */
    long size;                  /* the bytes there now */
    int done;                   /* SK_MEM: no more will come */
    UBYTE **blk;                /* SK_MEM: the bytes, in blocks */
    long nblk, blkcap;
    /* the line index: built as far as needed, never the whole file read */
    ULONG *lo;                  /* where line i starts */
    UWORD *ls;                  /* the colours in force there */
    long nl, lcap;              /* starts known (lo[0] = 0) */
    long scanned;               /* bytes indexed */
    int esc, np, pv[16];        /* the indexer's escape sequence so far */
    UWORD st;
    int enc;                    /* ENC_LATIN1/UTF8/CP437: as shown */
    int encset;                 /* chosen (not guessed) */
    /* a pipe: read with a packet of our own, so the screen never waits */
    BPTR in;
    struct MsgPort *port;
    struct DosPacket *pkt;
    UBYTE *rbuf;
    int pending;
} Src;

int src_init(void);
void src_exit(void);
Src *src_file(const char *path, char *err);   /* NULL: err says why */
Src *src_pipe(BPTR in);                       /* reading starts at once */
Src *src_text(const char *t, long n, const char *title);
Src *src_drawer(BPTR lock, const char *path, char *err);  /* drawer.c: a drawer listed */
void src_free(Src *s);
long src_get(Src *s, long off, UBYTE *d, long n);
int src_line(Src *s, long i);           /* line i exists (indexing up to it) */
long src_lineend(Src *s, long i);       /* after src_line: where it ends */
long src_lines(Src *s);                 /* lines known so far */
int src_full(Src *s);                   /* all indexed, and no more coming */
void src_scan(Src *s, long bytes);      /* index this much more (in idle time) */
int src_poll(Src *s);                   /* the pipe's news / the file grew: 1 */
int src_grow(Src *s);                   /* a file: grown on disk? (follow) */
ULONG src_sigs(Src *s);                 /* the signal its reading gives */
void src_guess(Src *s);
long src_lineat(Src *s, long off);      /* the line holding a byte */
void src_reset(Src *s);                 /* the index again (encoding changed) */

/* ---- keys.c ------------------------------------------------------------ */
enum {
    A_NONE,
    A_DOWN, A_UP, A_PAGEDOWN, A_PAGEUP, A_HALFDOWN, A_HALFUP, A_START, A_END,
    A_LEFT, A_RIGHT, A_SEARCH, A_RSEARCH, A_NEXT, A_PREV, A_QUIT, A_HELP,
    A_NUMBERS, A_WRAP, A_FOLLOW, A_EDIT, A_NEXTFILE, A_PREVFILE, A_INFO,
    A_ENCODING, A_MARK, A_GOMARK, A_PERCENT, A_REDRAW, A_STOP, A_GOTO,
    A_COUNT
};
extern const char *const actnames[A_COUNT];
extern const char *const actdesc[A_COUNT];
void keys_load(void);
int keys_find(int key);
int keys_all(int act, char *names, int max);    /* every key bound, "q Q" */
void keyname(int key, char *dst, int shortform);
extern char keysbad[120];

#endif
