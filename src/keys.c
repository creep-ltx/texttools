/* keys - every key is the person's: ENVARC:Sumi/Sumi.keys, one binding
 * a line ("<C-s> save", "<A-Left> wordleft", "<F3> findnext"; "none"
 * takes a key away; ';' starts a comment). Written with the defaults -
 * micro's and nano's Ctrl keys - when it is missing. ENV:Sumi/Sumi.keys
 * (the copy made at boot) is read first.
 *
 * Amiga facts behind the defaults: Right Amiga keys belong to CTerm's
 * menus and Left Amiga keys to its panes (a program in the terminal
 * never sees them); Ctrl-C..F come as signals (tui.c makes them keys);
 * Ctrl-H is Backspace, Ctrl-I Tab, Ctrl-M Return; the A1200 has no
 * Home, End or Page keys, so Ctrl/Alt+arrows do their work (CTerm sends
 * those to a program like Sumi, in xterm's form). */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include "sumi.h"

#define KEYFILE "ENVARC:Sumi/Sumi.keys"
#define KEYENV  "ENV:Sumi/Sumi.keys"
#define MAXB    160

const char *const actnames[A_COUNT] = {
    "none",
    "up", "down", "left", "right", "wordleft", "wordright", "linestart", "lineend",
    "pageup", "pagedown", "filestart", "fileend",
    "selup", "seldown", "selleft", "selright", "selwordleft", "selwordright",
    "sellinestart", "sellineend", "selpageup", "selpagedown", "selfilestart", "selfileend",
    "selectall", "enter", "backspace", "delete", "delwordleft", "delwordright",
    "tab", "untab", "cut", "copy", "paste", "cutline", "duplicate", "undo", "redo",
    "save", "saveas", "open", "newtab", "close", "nexttab", "prevtab", "quit",
    "find", "findnext", "findprev", "replace", "goto", "help", "command",
    "numbers", "escape",
    "match", "comment", "justify", "softwrap", "moveup", "movedown", "insert",
    "macrorecord", "macroplay", "whitespace"
};

const char *const actdesc[A_COUNT] = {
    "",
    "up a line", "down a line", "back a character", "on a character",
    "back a word", "on a word", "to the start of the line", "to the end of the line",
    "a page up", "a page down", "to the start of the file", "to the end of the file",
    "select up", "select down", "select back", "select on", "select back a word",
    "select on a word", "select to the line's start", "select to the line's end",
    "select a page up", "select a page down", "select to the file's start",
    "select to the file's end",
    "select everything", "a new line (indented as the one before)", "delete back",
    "delete on", "delete the word before", "delete the word after",
    "indent (a tab, or the selected lines)", "unindent", "cut", "copy", "paste",
    "cut the line", "duplicate the line (or selection)", "undo", "redo",
    "save", "save as...", "open a file in a new tab", "a new empty tab", "close the tab",
    "the next tab", "the previous tab", "quit (asks for what is not saved)",
    "find", "find the next", "find the previous", "find and replace", "go to a line",
    "help: these keys", "a command (save, open, goto, set...)", "line numbers on/off",
    "end the selection / the question",
    "to the matching bracket", "comment the line(s) on/off", "reflow the paragraph",
    "soft wrap on/off", "move the line(s) up", "move the line(s) down", "insert a file here",
    "record keys (again: stop)", "play the recorded keys", "show tabs and trailing spaces"
};

/* the defaults, as the file says them */
static const char defaults[] =
    "; Sumi's keys: one a line, KEY ACTION. <C-x> is Ctrl+X, <A-x> Alt, <S-x> Shift.\n"
    "; Keys: <Up> <Down> <Left> <Right> <Home> <End> <PgUp> <PgDn> <Ins> <Del> <BS>\n"
    "; <Tab> <Enter> <Esc> <Help> <F1>-<F12>, or a character. 'none' takes a key away.\n"
    "; (Right Amiga keys are CTerm's menus, Left Amiga keys its panes: not here.)\n"
    "\n; files and tabs\n"
    "<C-s> save\n<F2> saveas\n<C-o> open\n<C-t> newtab\n<C-q> close\n<F10> nexttab\n<F9> prevtab\n"
    "<C-w> quit\n"
    "\n; editing\n"
    "<C-z> undo\n<C-y> redo\n<C-c> copy\n<C-x> cut\n<C-v> paste\n<C-k> cutline\n"
    "<C-d> duplicate\n<C-a> selectall\n<Tab> tab\n<S-Tab> untab\n<Enter> enter\n"
    "<BS> backspace\n<Del> delete\n<A-BS> delwordleft\n<A-Del> delwordright\n"
    "\n; finding\n"
    "<C-f> find\n<F3> findnext\n<C-g> findnext\n<S-F3> findprev\n<C-r> replace\n<C-l> goto\n"
    "\n; moving (the A1200 has no Home/End/Page keys: Ctrl and Alt with the arrows)\n"
    "<Up> up\n<Down> down\n<Left> left\n<Right> right\n"
    "<A-Left> wordleft\n<A-Right> wordright\n<C-Left> linestart\n<C-Right> lineend\n"
    "<A-Up> pageup\n<A-Down> pagedown\n<C-Up> filestart\n<C-Down> fileend\n"
    "<Home> linestart\n<End> lineend\n<PgUp> pageup\n<PgDn> pagedown\n"
    "<C-Home> filestart\n<C-End> fileend\n"
    "<C-p> pageup\n<C-n> pagedown\n<C-b> linestart\n<C-e> lineend\n"
    "\n; selecting (Shift with the moves)\n"
    "<S-Up> selup\n<S-Down> seldown\n<S-Left> selleft\n<S-Right> selright\n"
    "<S-A-Left> selwordleft\n<S-A-Right> selwordright\n<S-C-Left> sellinestart\n"
    "<S-C-Right> sellineend\n<S-A-Up> selpageup\n<S-A-Down> selpagedown\n"
    "<S-C-Up> selfilestart\n<S-C-Down> selfileend\n<S-Home> sellinestart\n<S-End> sellineend\n"
    "\n; the rest\n"
    "<Help> help\n<F1> help\n<C-u> command\n<F4> numbers\n<Esc> escape\n"
    "<F5> match\n<F6> comment\n<F7> justify\n<F8> softwrap\n<C-A-Up> moveup\n"
    "<C-A-Down> movedown\n<S-F2> insert\n<S-F5> macrorecord\n<S-F6> macroplay\n"
    "<S-F4> whitespace\n";

typedef struct { int key, act; } Bind;
static Bind binds[MAXB];
static int nbinds;
char keysbad[120];

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int same(const char *a, const char *b)
{
    while (*a && *b && lower((unsigned char)*a) == lower((unsigned char)*b)) a++, b++;
    return !*a && !*b;
}

static const struct { const char *n; int k; } named[] = {
    { "Up", K_UP }, { "Down", K_DOWN }, { "Left", K_LEFT }, { "Right", K_RIGHT },
    { "Home", K_HOME }, { "End", K_END }, { "PgUp", K_PGUP }, { "PgDn", K_PGDN },
    { "Ins", K_INS }, { "Del", K_DEL }, { "BS", K_BS }, { "Tab", K_TAB },
    { "Enter", K_ENTER }, { "Return", K_ENTER }, { "Esc", K_ESC }, { "Help", K_HELP },
    { "Space", ' ' }, { NULL, 0 }
};

/* "<C-s>" "<S-A-Left>" "<F3>" "x" -> a key, 0 if not understood */
static int parsekey(const char *s)
{
    int m = 0, i;
    char name[16];
    const char *e;
    if (s[0] != '<' || !s[1]) return (unsigned char)s[0] && !s[1] ? (unsigned char)s[0] : 0;
    s++;
    if (!(e = strchr(s, '>'))) return 0;
    while (e - s > 2 && s[1] == '-') {
        int c = lower((unsigned char)s[0]);
        if (c == 'c') m |= TM_CTRL;
        else if (c == 's') m |= TM_SHIFT;
        else if (c == 'a' || c == 'm') m |= TM_ALT;
        else return 0;
        s += 2;
    }
    if (e - s >= (int)sizeof(name)) return 0;
    memcpy(name, s, e - s);
    name[e - s] = 0;
    if (!name[0]) return 0;
    if (!name[1]) {                     /* <C-s>: a letter with the modifiers */
        int c = (unsigned char)name[0];
        if (m & TM_CTRL) c = lower(c);
        return c | m;
    }
    if ((name[0] == 'F' || name[0] == 'f') && name[1] >= '1' && name[1] <= '9') {
        int n = name[1] - '0';
        if (name[2] >= '0' && name[2] <= '9') n = n * 10 + name[2] - '0';
        if (n >= 1 && n <= 12) return K_F(n) | m;
        return 0;
    }
    for (i = 0; named[i].n; i++)
        if (same(name, named[i].n)) return named[i].k | m;
    return 0;
}

void keyname(int key, char *d, int shortform)
{
    int b = K_BASE(key), i;
    d[0] = 0;
    if (shortform) {
        /* ^S, M-Left, S-F3: as nano shows them */
        if (key & TM_SHIFT) strcat(d, "S-");
        if (key & TM_ALT) strcat(d, "M-");
        if ((key & TM_CTRL) && b < 128 && b > ' ') {
            int l = (int)strlen(d);
            d[l] = '^';
            d[l + 1] = (char)(b >= 'a' && b <= 'z' ? b - 32 : b);
            d[l + 2] = 0;
            return;
        }
        if (key & TM_CTRL) strcat(d, "^");
    } else {
        strcat(d, "<");
        if (key & TM_SHIFT) strcat(d, "S-");
        if (key & TM_CTRL) strcat(d, "C-");
        if (key & TM_ALT) strcat(d, "A-");
    }
    if (b >= K_F1 && b <= K_F(12)) {
        int n = b - K_F1 + 1, l = (int)strlen(d);
        d[l++] = 'F';
        if (n >= 10) d[l++] = '1';
        d[l++] = (char)('0' + n % 10);
        d[l] = 0;
    } else {
        for (i = 0; named[i].n; i++)
            if (named[i].k == b) {
                strcat(d, named[i].n);
                break;
            }
        if (!named[i].n) {
            int l = (int)strlen(d);
            d[l] = (char)b;
            d[l + 1] = 0;
        }
    }
    if (!shortform) strcat(d, ">");
}

static void addbind(int key, int act)
{
    int i;
    for (i = 0; i < nbinds; i++)
        if (binds[i].key == key) {
            binds[i].act = act;         /* a later line wins */
            return;
        }
    if (nbinds < MAXB) {
        binds[nbinds].key = key;
        binds[nbinds].act = act;
        nbinds++;
    }
}

static void parse(char *text)
{
    char *line = text;
    while (line && *line) {
        char *nl = strchr(line, '\n'), *k, *a, *e;
        int key, act;
        if (nl) *nl = 0;
        k = line;
        while (*k == ' ' || *k == '\t') k++;
        if (*k && *k != ';' && *k != '#') {
            for (e = k; *e && *e != ' ' && *e != '\t'; e++) ;
            a = e;
            while (*a == ' ' || *a == '\t') a++;
            *e = 0;
            for (e = a; *e && *e != ' ' && *e != '\t' && *e != '\r'; e++) ;
            *e = 0;
            key = parsekey(k);
            for (act = 0; act < A_COUNT && !same(a, actnames[act]); act++) ;
            if (!key || act == A_COUNT) {
                if (!keysbad[0]) {
                    strcpy(keysbad, "Sumi.keys: not understood: ");
                    strncat(keysbad, k, 40);
                }
            } else
                addbind(key, act);
        }
        line = nl ? nl + 1 : NULL;
    }
}

static char *readall(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *t;
    long n;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    if (n < 0 || n > 65536 || !(t = AllocVec(n + 1, MEMF_ANY))) {
        Close(fh);
        return NULL;
    }
    n = Read(fh, t, n);
    Close(fh);
    t[n > 0 ? n : 0] = 0;
    return t;
}

static void writeto(const char *dir, const char *path)
{
    BPTR l = Lock((STRPTR)dir, SHARED_LOCK), fh;
    if (!l) l = CreateDir((STRPTR)dir);
    if (l) UnLock(l);
    if ((fh = Open((STRPTR)path, MODE_NEWFILE)) != 0) {
        Write(fh, (APTR)defaults, sizeof(defaults) - 1);
        Close(fh);
    }
}

void keys_load(void)
{
    char *t, *d;
    keysbad[0] = 0;
    nbinds = 0;
    /* the defaults first, the person's file over them: a key Sumi gains in
     * a new version is there without editing the file, and the file wins */
    if ((d = AllocVec(sizeof(defaults), MEMF_ANY)) != NULL) {
        memcpy(d, defaults, sizeof(defaults));
        parse(d);
        FreeVec(d);
    }
    t = readall(KEYENV);
    if (!t) t = readall(KEYFILE);
    if (!t) {
        /* first start: the defaults, written for the person to change */
        writeto("ENVARC:Sumi", KEYFILE);
        writeto("ENV:Sumi", KEYENV);
        if ((t = AllocVec(sizeof(defaults), MEMF_ANY)) != NULL) memcpy(t, defaults, sizeof(defaults));
    }
    if (t) {
        parse(t);
        FreeVec(t);
    }
}

int keys_find(int key)
{
    int i;
    for (i = 0; i < nbinds; i++)
        if (binds[i].key == key) return binds[i].act;
    return A_NONE;
}

int keys_first(int act, char *name)
{
    int i;
    for (i = 0; i < nbinds; i++)
        if (binds[i].act == act) {
            keyname(binds[i].key, name, 1);
            return binds[i].key;
        }
    name[0] = 0;
    return 0;
}
