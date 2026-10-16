/* keys - every key is the person's: ENVARC:Maki/Maki.keys, one binding
 * a line ("q quit", "<Space> pagedown", "<C-f> pagedown"; "none" takes a
 * key away; ';' starts a comment). Written with the defaults - less's
 * keys, and the arrows - when it is missing. ENV:Maki/Maki.keys (the
 * copy made at boot) is read first.
 *
 * Amiga facts behind the defaults: Right Amiga keys belong to CTerm's
 * menus and Left Amiga keys to its panes (a program in the terminal
 * never sees them); Ctrl-C..F come as signals (tui.c makes them keys);
 * the A1200 has no Home, End or Page keys, so Ctrl/Alt+arrows do their
 * work. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include "maki.h"

#define KEYFILE "ENVARC:Maki/Maki.keys"
#define KEYENV  "ENV:Maki/Maki.keys"
#define MAXB    160

const char *const actnames[A_COUNT] = {
    "none",
    "down", "up", "pagedown", "pageup", "halfdown", "halfup", "start", "end",
    "left", "right", "search", "rsearch", "next", "prev", "quit", "help",
    "numbers", "wrap", "follow", "edit", "nextfile", "prevfile", "info",
    "encoding", "mark", "gomark", "percent", "redraw", "stop", "goto"
};

const char *const actdesc[A_COUNT] = {
    "",
    "down a line", "up a line", "a page down", "a page up", "half a page down",
    "half a page up", "to the start (or to line N)", "to the end (or to line N)",
    "look left (long lines cut)", "look right (long lines cut)",
    "search forward", "search backward", "the next match", "the match before",
    "quit (from the help: back)", "help: these keys", "line numbers on/off",
    "long lines folded / cut", "follow: the end as it grows (a key stops)",
    "edit the file (Sumi, or ENV:EDITOR) at this line",
    "the next file", "the file before", "about the file",
    "the characters: guessed, Latin-1, UTF-8, PC (CP437)",
    "mark the place (then a letter)", "back to a mark (then a letter)",
    "to N percent of the file", "draw everything again",
    "stop (following, a number being typed)", "go to a line (asks)"
};

/* the defaults, as the file says them */
static const char defaults[] =
    "; Maki's keys: one a line, KEY ACTION. <C-x> is Ctrl+X, <A-x> Alt, <S-x> Shift.\n"
    "; Keys: <Up> <Down> <Left> <Right> <Home> <End> <PgUp> <PgDn> <Ins> <Del> <BS>\n"
    "; <Tab> <Enter> <Esc> <Help> <Space> <F1>-<F12>, or a character. 'none' takes a key away.\n"
    "; A number typed first counts: 5 Space is five pages, 120g line 120, 50% the middle.\n"
    "; (Right Amiga keys are CTerm's menus, Left Amiga keys its panes: not here.)\n"
    "\n; moving\n"
    "<Space> pagedown\nf pagedown\nz pagedown\n<PgDn> pagedown\n<A-Down> pagedown\n<C-v> pagedown\n"
    "b pageup\nw pageup\n<PgUp> pageup\n<A-Up> pageup\n<C-b> pageup\n<BS> pageup\n"
    "<Enter> down\nj down\ne down\n<Down> down\n<C-n> down\n"
    "k up\ny up\n<Up> up\n<C-p> up\n"
    "d halfdown\n<C-d> halfdown\nu halfup\n<C-u> halfup\n"
    "g start\n< start\n<Home> start\n<C-Up> start\n"
    "G end\n> end\n<End> end\n<C-Down> end\n"
    "<Left> left\n<Right> right\n"
    "% percent\np percent\n: goto\n<C-l> goto\n"
    "\n; searching\n"
    "/ search\n? rsearch\nn next\nN prev\n<C-f> search\n<F3> next\n<S-F3> prev\n"
    "\n; files\n"
    "] nextfile\n[ prevfile\n<F10> nextfile\n<F9> prevfile\n= info\n<C-g> info\nv edit\n"
    "\n; the rest\n"
    "q quit\nQ quit\nh help\nH help\n<Help> help\n<F1> help\n"
    "F follow\n<F4> numbers\n<F8> wrap\n<F7> encoding\n"
    "m mark\n' gomark\nr redraw\n<C-r> redraw\n<Esc> stop\n<C-c> stop\n";

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
                    strcpy(keysbad, "Maki.keys: not understood: ");
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
    /* the defaults first, the person's file over them: a key Maki gains in
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
        writeto("ENVARC:Maki", KEYFILE);
        writeto("ENV:Maki", KEYENV);
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

/* every key bound to act, short names with a space between */
int keys_all(int act, char *names, int max)
{
    int i, n = 0;
    names[0] = 0;
    for (i = 0; i < nbinds; i++)
        if (binds[i].act == act) {
            char k[24];
            keyname(binds[i].key, k, 1);
            if ((int)(strlen(names) + strlen(k) + 2) >= max) break;
            if (n++) strcat(names, " ");
            strcat(names, k);
        }
    return n;
}
