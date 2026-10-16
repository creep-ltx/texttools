/* md - Markdown made into rows of coloured cells for the screen.
 * md_render() reads a Buf and fills a Doc: rows of cells wrapped to a
 * width, each knowing the source line it came from, and beside them the
 * links, headings, pictures and task boxes found. */
#ifndef MD_H
#define MD_H

#include <exec/types.h>

typedef struct {
    ULONG c;                    /* a code point */
    UBYTE fg, bg, at, fl;       /* colours (TC_*), attributes (TA_*) */
} RCell;

typedef struct {
    long cell;                  /* its first cell in cells[] */
    long src;                   /* the source line it came from */
    short len;                  /* cells */
    short pic;                  /* a picture starts on this row: its index, else -1 */
} RRow;

typedef struct { long row; short c0, c1, link; } RSpan;     /* a link on a row: cells c0..c1-1 */
typedef struct { long url, title; } RLink;                  /* pool offsets */
typedef struct { long row, src, text, anchor; short level; } RHead;
typedef struct {
    long row, src, path, alt;   /* path: the file, as AmigaDOS writes it ("" remote) */
    short col, cols, rows;      /* where on its rows, how big in cells */
} RPic;
typedef struct { long row, src, scol; short col, done; } RTask;  /* scol: the byte of '[' */

typedef struct Doc {
    RRow *rows;   long nrows, crows;
    RCell *cells; long ncells, ccells;
    RSpan *spans; long nspans, cspans;
    RLink *links; long nlinks, clinks;
    RHead *heads; long nheads, cheads;
    RPic *pics;   long npics, cpics;
    RTask *tasks; long ntasks, ctasks;
    char *pool;   long npool, cpool;    /* strings, UTF-8; offset 0 is "" */
    int width;                  /* what it was wrapped to */
    int stamp;                  /* the settings it was made with (view.c) */
    long rev;                   /* the Buf's rev it was made from (-1: never) */
    int failed;                 /* out of memory: what is there is cut short */
} Doc;

struct Buf;

/* set by the caller before md_render */
extern int md_unicode;          /* the terminal shows Unicode (box lines, bullets) */
extern int md_cellw, md_cellh;  /* a cell in pixels (CTerm), 0: no pictures */
extern int md_pictures;         /* show pictures (else a line saying which) */
extern int md_maxpicrows;       /* the tallest a picture may be, in rows */

void md_render(Doc *d, struct Buf *b, int width, const char *path);
void md_free(Doc *d);
void md_cleanup(void);                          /* free the work buffers kept between renders */
const char *md_str(const Doc *d, long off);
long md_rowof(const Doc *d, long srcline);       /* the first row of that line (or before) */
int md_anchor(const Doc *d, const char *name);   /* the heading with that anchor, or -1 */
int md_link_at(const Doc *d, long row, int col); /* the link (1..) on that cell, or 0 */

#endif
