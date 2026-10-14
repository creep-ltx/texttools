/* Markdown - a Markdown viewer and editor that lives inside the terminal (a
 * CTerm tab or pane, or any Amiga console window). Sumi's editor, with
 * the Markdown shown as it reads: headings, lists, tables, code, links
 * to follow, pictures. Shared state and the functions the files call. */
#ifndef MARKDOWN_H
#define MARKDOWN_H

#include <exec/types.h>
#include <dos/dos.h>
#include "tui.h"

#define PATHMAX 256

/* ---- buf.c: a file's text, as lines of bytes, and its undo ---------- */

typedef struct {
    char *s;
    long len, cap;
    UBYTE hs;                   /* the highlighter's state where it starts */
    UBYTE own;                  /* s is its own (else it is in the loaded file's block) */
} Line;

typedef struct {
    long line;
    long col;                   /* a byte offset in the line */
} Pos;

typedef struct Undo {
    struct Undo *next;
    int ins;                    /* 1: text went in at a; 0: text went out from a */
    Pos a;
    char *text;
    long tlen;
    Pos before, after;          /* the cursor around it */
    long group, serial;
} Undo;

enum { ENC_LATIN1, ENC_UTF8 };

typedef struct Buf {
    Line *l;
    long n, cap;
    char path[PATHMAX];         /* "" for a new one never saved */
    int enc, crlf, finalnl;
    int lang;
    Pos cur, anchor;
    int sel;                    /* a selection from anchor to cur */
    long wantx;                 /* the column up/down keep to */
    long top, left;             /* the view */
    long topseg;                /* soft wrap: the first shown row of line top */
    Undo *undo, *redo;
    long group, saved;          /* the undo serial the file on disk matches */
    long hsok;                  /* highlight states known for lines < hsok */
    int typing;                 /* the last change was typing (one undo step) */
    ULONG prot;                 /* the file's protection bits, kept on save */
    char comment[80];           /* and its comment */
    int hadfile;                /* it exists on disk */
    char *bulk;                 /* the file as loaded: unchanged lines point into it */
    struct DateStamp disk;      /* the file's date when loaded or saved */
    long rev;                   /* changes so far (the view is made again when it moves) */
    int view;                   /* shown as Markdown, not as text to edit */
    long vtop;                  /* the view's first row */
    long frow;                  /* the view: the link or box chosen (its row, -1 none) */
    int fcol;                   /* ... and column */
    struct Doc *doc;            /* the Markdown made from it (NULL: not yet) */
} Buf;

Buf *buf_new(void);
void buf_free(Buf *b);
int buf_dirty(Buf *b);
Pos buf_insert(Buf *b, Pos at, const char *t, long n);  /* the end of what went in */
void buf_delete(Buf *b, Pos a, Pos z);                   /* a .. z (z after a) */
char *buf_text(Buf *b, Pos a, Pos z, long *len);         /* AllocVec'd, '\n' between lines */
void buf_group(Buf *b);                                  /* the next change is an undo step of its own */
int buf_undo(Buf *b);
int buf_redo(Buf *b);
long buf_next(Buf *b, long line, long col);              /* one character on (bytes) */
long buf_prev(Buf *b, long line, long col);
ULONG buf_char(Buf *b, long line, long col, int *len);   /* the code point there */
long buf_xof(Buf *b, long line, long col);               /* the screen column of a byte */
long buf_colat(Buf *b, long line, long x);               /* the byte at a screen column */
int pos_cmp(Pos a, Pos b);
void buf_clearundo(Buf *b);
int buf_load(Buf *b, char *text, long n);                /* text (AllocVec'd) becomes the lines */

/* ---- file.c --------------------------------------------------------- */
int file_load(Buf *b, const char *path);    /* 1 ok, 0 missing (a new file), -1 error */
int file_save(Buf *b, const char *path);    /* 1 ok */
const char *file_err(void);
const char *basename_(const char *p);

/* ---- syntax.c ------------------------------------------------------- */
enum { L_NONE, L_C, L_ASM, L_REXX, L_E, L_GUIDE, L_SCRIPT, L_PY, L_MD, L_SH, L_MAKE, L_JSON,
       L_HTML, L_LUA, L_BLITZ, L_INSTALL, L_COUNT };
/* what a byte is, for its colour */
enum { HL_TEXT, HL_COMMENT, HL_STRING, HL_KEYWORD, HL_TYPE, HL_NUMBER, HL_PREPROC, HL_LABEL };
extern const char *const langnames[L_COUNT];
int syn_guess(const char *path, Buf *b);
/* colours for a line's bytes (col[] as long as the line), the state
 * after it from the state before */
int syn_line(int lang, const char *s, long len, int state, UBYTE *col);

/* ---- clip.c --------------------------------------------------------- */
void clip_put(const char *t, long n, int enc);
char *clip_get(long *n, int enc);           /* AllocVec'd, in enc, '\n' lines */
void clip_exit(void);
char *clip_mine(const char *t, long n, int enc, long *out);
char *convert_text(const char *t, long n, int from, int to, long *out);

/* ---- keys.c --------------------------------------------------------- */
enum {
    A_NONE,
    A_UP, A_DOWN, A_LEFT, A_RIGHT, A_WORDLEFT, A_WORDRIGHT, A_LINESTART, A_LINEEND,
    A_PAGEUP, A_PAGEDOWN, A_FILESTART, A_FILEEND,
    A_SELUP, A_SELDOWN, A_SELLEFT, A_SELRIGHT, A_SELWORDLEFT, A_SELWORDRIGHT,
    A_SELLINESTART, A_SELLINEEND, A_SELPAGEUP, A_SELPAGEDOWN, A_SELFILESTART, A_SELFILEEND,
    A_SELECTALL, A_ENTER, A_BACKSPACE, A_DELETE, A_DELWORDLEFT, A_DELWORDRIGHT,
    A_TAB, A_UNTAB, A_CUT, A_COPY, A_PASTE, A_CUTLINE, A_DUPLICATE, A_UNDO, A_REDO,
    A_SAVE, A_SAVEAS, A_OPEN, A_NEWTAB, A_CLOSE, A_NEXTTAB, A_PREVTAB, A_QUIT,
    A_FIND, A_FINDNEXT, A_FINDPREV, A_REPLACE, A_GOTO, A_HELP, A_COMMAND,
    A_NUMBERS, A_ESCAPE,
    A_MATCH, A_COMMENT, A_JUSTIFY, A_SOFTWRAP, A_MOVEUP, A_MOVEDOWN, A_INSERT,
    A_MACROREC, A_MACROPLAY, A_WHITESPACE,
    A_VIEW, A_SPLIT, A_BOLD, A_FORMAT, A_OUTLINE, A_NEXTLINK, A_PREVLINK, A_FOLLOW, A_BACK,
    A_TASK, A_COPYLINK, A_NEXTHEAD, A_PREVHEAD, A_HALFDOWN, A_HALFUP, A_PICTURES, A_LINEUP,
    A_LINEDOWN,
    A_COUNT
};
extern const char *const actnames[A_COUNT];
extern const char *const actdesc[A_COUNT];
void keys_load(void);                       /* ENV(ARC):Markdown/Markdown.keys, written when missing */
int keys_find(int key, int view);           /* the action bound (editing or viewing), or A_NONE */
int keys_first(int act, char *name, int view);  /* its first key as a name ("^S"), 0 none */
void keyname(int key, char *dst, int shortform);
extern char keysbad[120];

/* ---- main.c --------------------------------------------------------- */
extern int tabsize, tabspaces, numbers, autoindent, backups;
extern int pictures, mdwidth;
void say(const char *msg, int err);
void prefs_save(void);
enum { P_FIND, P_FILE, P_REPL, P_GOTO, P_CMD, P_KINDS };
int prompt(const char *q, char *buf, int max, int kind);
int ask(const char *q, const char *choices);
int textrows(void);
int addtab(const char *path);
int samefile(const char *a, const char *b);
void drawall(void);
extern Buf *tabs[];
#define B_ (tabs[ct])                       /* the tab shown */
extern int ntabs, ct;
extern char findtext[128];
int findrow(const char *s, long len, long from, int back, long *at, long *ml);
void instext(Buf *b, const char *t, long n);

/* ---- view.c: the Markdown view ---------------------------------------- */
extern int split;                           /* editing with the view beside */
extern int ew;                              /* the editor's width (all, or the left half) */
int view_doc(Buf *b, int width);            /* made (again) when needed; FALSE no memory */
void view_draw(Buf *b, int x0, int w, int y0, int h, int full);
void view_pictures(void);                   /* after a flush: the pictures shown */
void view_forget(void);                     /* the pictures' cells drawn over: show them again */
void view_reset(void);                      /* the screen was cleared */
int view_action(int act);                   /* a key's action in the view; FALSE not one */
void view_mouse(TuiEv *e, int x0, int w, int y0, int h);
void view_tofile(Buf *b);                   /* the view's place to the cursor (into editing) */
void view_fromfile(Buf *b);                 /* the cursor's place to the view */
void view_follow_cursor(Buf *b, int crow);  /* split: the view beside, at the cursor's line */
void view_hints(void);
void view_status(void);
void view_close(Buf *b);                    /* a tab goes */
void outline(void);
int md_enter(Buf *b);                       /* Return in a list or quote: TRUE handled */
void md_bold(Buf *b);
void md_format(Buf *b);
void md_table(Buf *b);                      /* the table at the cursor made neat */
void md_toc(Buf *b);                        /* a list of the headings, as links, here */
int md_picsize(const char *path, int *w, int *h);
void md_resolve(const char *base, const char *url, char *out, int max);

#endif
