/* syntax - colours for code: comments, strings, keywords, types,
 * numbers, preprocessor lines and labels, per language. A line is
 * coloured from the state the line before left (inside a block comment,
 * inside a long string), so a change only colours again what it moved. */
#include <exec/types.h>
#include <string.h>
#include "sumi.h"

const char *const langnames[L_COUNT] = {
    "text", "C", "68k asm", "ARexx", "Amiga E", "AmigaGuide", "AmigaDOS", "Python", "Markdown"
};

/* states between lines */
#define ST_NONE  0
#define ST_BLOCK 1              /* inside a block comment */
#define ST_LONG  2              /* inside a Python long string */

static const char *const kw_c[] = {
    "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
    "return", "goto", "sizeof", "typedef", "struct", "union", "enum", "static", "extern",
    "const", "volatile", "register", "inline", "auto", "class", "public", "private",
    "protected", "new", "delete", "this", "template", "namespace", "using", "virtual",
    "true", "false", "NULL", "TRUE", "FALSE", NULL
};
static const char *const ty_c[] = {
    "void", "char", "short", "int", "long", "float", "double", "signed", "unsigned",
    "BYTE", "UBYTE", "WORD", "UWORD", "LONG", "ULONG", "APTR", "BPTR", "BOOL", "STRPTR",
    "size_t", "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t", "bool", NULL
};
static const char *const kw_rexx[] = {
    "address", "arg", "by", "call", "do", "drop", "else", "end", "exit", "expose", "for",
    "forever", "if", "interpret", "iterate", "leave", "nop", "numeric", "options", "otherwise",
    "parse", "procedure", "pull", "push", "queue", "results", "return", "say", "select",
    "shell", "signal", "source", "then", "to", "trace", "until", "upper", "value", "var",
    "when", "while", "with", "on", "off", "error", "syntax", "failure", "break_c", NULL
};
static const char *const kw_e[] = {
    "PROC", "ENDPROC", "DEF", "IF", "ENDIF", "ELSE", "ELSEIF", "THEN", "FOR", "ENDFOR", "TO",
    "STEP", "DO", "WHILE", "ENDWHILE", "REPEAT", "UNTIL", "LOOP", "ENDLOOP", "SELECT", "CASE",
    "DEFAULT", "ENDSELECT", "RETURN", "OBJECT", "ENDOBJECT", "MODULE", "CONST", "ENUM", "SET",
    "RAISE", "EXCEPT", "HANDLE", "JUMP", "IS", "OF", "NEW", "END", "AND", "OR", "NOT", "BUT",
    "EXIT", "OPT", "INC", "DEC", "ASM", "PRIVATE", "PUBLIC", "SUPER", "NIL", "TRUE", "FALSE", NULL
};
static const char *const ty_e[] = {
    "PTR", "ARRAY", "LIST", "STRING", "CHAR", "INT", "LONG", "REAL", NULL
};
static const char *const kw_script[] = {
    "if", "else", "endif", "skip", "lab", "echo", "set", "setenv", "unset", "unsetenv",
    "assign", "path", "run", "execute", "failat", "quit", "wait", "ask", "resident",
    "version", "stack", "prompt", "alias", "copy", "delete", "rename", "makedir", "cd",
    "list", "dir", "mount", "protect", "type", "loadwb", "endcli", "endshell", "newshell",
    "warn", "error", "fail", "exists", "not", "val", "eq", "gt", "ge", "makelink", "avail",
    "setpatch", "addbuffers", "binddrivers", "iprefs", "conclip", "setclock", "info", NULL
};
static const char *const kw_py[] = {
    "and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del",
    "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is",
    "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with",
    "yield", "None", "True", "False", "self", NULL
};
static const char *const kw_asm[] = {
    "section", "code", "data", "bss", "dc", "ds", "dcb", "include", "incbin", "incdir",
    "macro", "endm", "equ", "set", "xdef", "xref", "even", "cnop", "rept", "endr", "if",
    "ifd", "ifnd", "ifeq", "ifne", "else", "endc", "endif", "org", "rsreset", "rs", "end",
    "machine", "opt", "align", "public", "near", "far", NULL
};

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int isid(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c >= 0xC0;
}

static int inlist(const char *const *l, const char *s, long n, int nocase)
{
    for (; *l; l++) {
        long i;
        if ((long)strlen(*l) != n) continue;
        for (i = 0; i < n; i++)
            if ((nocase ? lower((unsigned char)s[i]) : (unsigned char)s[i]) != (unsigned char)(*l)[i])
                break;
        if (i == n) return 1;
    }
    return 0;
}

static void paint(UBYTE *col, long a, long b, int c)
{
    for (; a < b; a++) col[a] = (UBYTE)c;
}

/* the guess: by name, then by the first line */
int syn_guess(const char *path, Buf *b)
{
    const char *n = basename_(path), *e = strrchr(n, '.');
    char ext[12];
    int i;
    ext[0] = 0;
    if (e) {
        for (i = 0; i < 11 && e[i + 1]; i++) ext[i] = (char)lower((unsigned char)e[i + 1]);
        ext[i] = 0;
    }
    if (!strcmp(ext, "c") || !strcmp(ext, "h") || !strcmp(ext, "cpp") || !strcmp(ext, "cc") ||
        !strcmp(ext, "hpp") || !strcmp(ext, "cxx")) return L_C;
    if (!strcmp(ext, "s") || !strcmp(ext, "asm") || !strcmp(ext, "i")) return L_ASM;
    if (!strcmp(ext, "rexx") || !strcmp(ext, "rx") || !strcmp(ext, "ced") || !strcmp(ext, "thor")) return L_REXX;
    if (!strcmp(ext, "e") || !strcmp(ext, "m")) return L_E;
    if (!strcmp(ext, "guide")) return L_GUIDE;
    if (!strcmp(ext, "py")) return L_PY;
    if (!strcmp(ext, "md") || !strcmp(ext, "markdown")) return L_MD;
    if (!strcmp(ext, "script") || !strcmp(ext, "sh")) return L_SCRIPT;
    {   /* S: scripts have no ending */
        char low[40];
        for (i = 0; i < 39 && n[i]; i++) low[i] = (char)lower((unsigned char)n[i]);
        low[i] = 0;
        if (strstr(low, "startup") || !strcmp(low, "shell-startup") || !strcmp(low, "user-startup"))
            return L_SCRIPT;
        if (!strncmp(path, "S:", 2) || !strncmp(path, "s:", 2)) return L_SCRIPT;
    }
    if (b && b->n > 0 && b->l[0].len >= 2) {
        const char *s = b->l[0].s;
        if (s[0] == '/' && s[1] == '*') return L_REXX;      /* ARexx starts with a comment */
        if (b->l[0].len >= 5 && !strncmp(s, "@data", 5)) return L_GUIDE;
        if (b->l[0].len >= 9 && !strncmp(s, "@database", 9)) return L_GUIDE;
        if (s[0] == '#' && s[1] == '!') return L_PY;
    }
    return L_NONE;
}

static long number(const char *s, long i, long len)
{
    if (s[i] == '$' || s[i] == '%') i++;     /* Amiga hex and binary */
    else if (s[i] == '0' && i + 1 < len && (s[i + 1] == 'x' || s[i + 1] == 'X')) i += 2;
    while (i < len && (isid((unsigned char)s[i]) || s[i] == '.')) i++;
    return i;
}

static long quoted(const char *s, long i, long len, char q, int esc)
{
    i++;
    while (i < len && s[i] != q) {
        if (esc && s[i] == '\\' && i + 1 < len) i++;
        i++;
    }
    return i < len ? i + 1 : len;
}

int syn_line(int lang, const char *s, long len, int st, UBYTE *col)
{
    long i = 0;
    memset(col, HL_TEXT, len);
    if (lang == L_NONE) return ST_NONE;
    if (lang == L_MD) {
        if (len && s[0] == '#') paint(col, 0, len, HL_KEYWORD);
        else if (len >= 3 && !strncmp(s, "```", 3)) paint(col, 0, len, HL_PREPROC);
        else
            for (i = 0; i < len; i++)
                if (s[i] == '`') {
                    long j = quoted(s, i, len, '`', 0);
                    paint(col, i, j, HL_STRING);
                    i = j - 1;
                }
        return ST_NONE;
    }
    if (lang == L_GUIDE) {
        if (len && s[0] == '@') {
            long j = 1;
            while (j < len && s[j] != ' ') j++;
            paint(col, 0, j, HL_KEYWORD);
            i = j;
        }
        for (; i < len; i++)
            if (s[i] == '@' && i + 1 < len && s[i + 1] == '{') {
                long j = i;
                while (j < len && s[j] != '}') j++;
                paint(col, i, j < len ? j + 1 : len, HL_STRING);
                i = j;
            }
        return ST_NONE;
    }
    if (lang == L_ASM) {
        long j;
        if (len && (s[0] == '*' || s[0] == ';')) {
            paint(col, 0, len, HL_COMMENT);
            return ST_NONE;
        }
        if (len && isid((unsigned char)s[0])) {     /* a label in the first column */
            for (j = 0; j < len && (isid((unsigned char)s[j]) || s[j] == '.'); j++) ;
            if (j < len && s[j] == ':') j++;
            paint(col, 0, j, HL_LABEL);
            i = j;
        }
        while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
        j = i;                                      /* the instruction */
        while (j < len && (isid((unsigned char)s[j]) || s[j] == '.')) j++;
        if (j > i) {
            long k = i;
            while (k < j && s[k] != '.') k++;
            paint(col, i, j, inlist(kw_asm, s + i, k - i, 1) ? HL_PREPROC : HL_KEYWORD);
        }
        for (i = j; i < len; i++) {
            if (s[i] == ';') {
                paint(col, i, len, HL_COMMENT);
                break;
            }
            if (s[i] == '"' || s[i] == '\'') {
                long e = quoted(s, i, len, s[i], 0);
                paint(col, i, e, HL_STRING);
                i = e - 1;
            } else if ((s[i] == '#' || s[i] == '$' || s[i] == '%' || (s[i] >= '0' && s[i] <= '9')) &&
                       (i == 0 || !isid((unsigned char)s[i - 1]))) {
                long e = number(s, s[i] == '#' ? i + 1 : i, len);
                paint(col, i, e, HL_NUMBER);
                i = e - 1;
            }
        }
        return ST_NONE;
    }
    if (lang == L_SCRIPT) {
        long j;
        while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i < len && s[i] == ';') {
            paint(col, i, len, HL_COMMENT);
            return ST_NONE;
        }
        j = i;
        while (j < len && s[j] != ' ' && s[j] != '\t') j++;
        if (j > i && inlist(kw_script, s + i, j - i, 1)) paint(col, i, j, HL_KEYWORD);
        else if (j > i && memchr(s + i, ':', j - i)) paint(col, i, j, HL_TYPE);
        for (i = j; i < len; i++) {
            if (s[i] == ';') {
                paint(col, i, len, HL_COMMENT);
                break;
            }
            if (s[i] == '"') {
                long e = quoted(s, i, len, '"', 1);
                paint(col, i, e, HL_STRING);
                i = e - 1;
            } else if (s[i] == '$' || (s[i] == '<' && i + 1 < len && s[i + 1] != ' ')) {
                long e = i + 1;
                while (e < len && (isid((unsigned char)s[e]) || s[e] == '>')) e++;
                paint(col, i, e, HL_PREPROC);
                i = e - 1;
            }
        }
        return ST_NONE;
    }
    /* C, ARexx, E, Python: the general kind */
    if (lang == L_C) {
        long j = 0;
        while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
        if (st == ST_NONE && j < len && s[j] == '#') {
            long e;
            for (e = j; e < len; e++) {
                if (s[e] == '/' && e + 1 < len && (s[e + 1] == '/' || s[e + 1] == '*')) break;
                if (s[e] == '"' || s[e] == '<') {
                    long q = quoted(s, e, len, s[e] == '<' ? '>' : '"', 1);
                    paint(col, e, q, HL_STRING);
                    e = q - 1;
                } else
                    col[e] = HL_PREPROC;
            }
            i = e;
        }
    }
    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        if (st == ST_BLOCK) {
            long j = i;
            while (j + 1 < len && !(s[j] == '*' && s[j + 1] == '/')) j++;
            if (j + 1 < len) {
                paint(col, i, j + 2, HL_COMMENT);
                i = j + 2;
                st = ST_NONE;
            } else {
                paint(col, i, len, HL_COMMENT);
                return ST_BLOCK;
            }
            continue;
        }
        if (st == ST_LONG) {
            long j = i;
            while (j + 2 < len && strncmp(s + j, "\"\"\"", 3)) j++;
            if (j + 2 < len) {
                paint(col, i, j + 3, HL_STRING);
                i = j + 3;
                st = ST_NONE;
            } else {
                paint(col, i, len, HL_STRING);
                return ST_LONG;
            }
            continue;
        }
        if (lang != L_PY && c == '/' && i + 1 < len && s[i + 1] == '*') {
            st = ST_BLOCK;
            col[i] = col[i + 1] = HL_COMMENT;
            i += 2;
            continue;
        }
        if ((lang == L_C && c == '/' && i + 1 < len && s[i + 1] == '/') ||
            (lang == L_E && c == '-' && i + 1 < len && s[i + 1] == '>') ||
            (lang == L_PY && c == '#')) {
            paint(col, i, len, HL_COMMENT);
            return st;
        }
        if (lang == L_PY && i + 2 < len && !strncmp(s + i, "\"\"\"", 3)) {
            st = ST_LONG;
            paint(col, i, i + 3, HL_STRING);
            i += 3;
            continue;
        }
        if (c == '"' || c == '\'') {
            long e = quoted(s, i, len, (char)c, lang == L_C || lang == L_PY || lang == L_E);
            paint(col, i, e, HL_STRING);
            i = e;
            continue;
        }
        if ((c >= '0' && c <= '9') || (lang == L_E && (c == '$' || c == '%'))) {
            if (i == 0 || !isid((unsigned char)s[i - 1])) {
                long e = number(s, i, len);
                paint(col, i, e, HL_NUMBER);
                i = e;
                continue;
            }
        }
        if (isid(c)) {
            long e = i;
            while (e < len && isid((unsigned char)s[e])) e++;
            if (lang == L_C) {
                if (inlist(kw_c, s + i, e - i, 0)) paint(col, i, e, HL_KEYWORD);
                else if (inlist(ty_c, s + i, e - i, 0)) paint(col, i, e, HL_TYPE);
            } else if (lang == L_REXX) {
                if (inlist(kw_rexx, s + i, e - i, 1)) paint(col, i, e, HL_KEYWORD);
                else if (e < len && s[e] == ':') paint(col, i, e + 1, HL_LABEL);
            } else if (lang == L_E) {
                if (inlist(kw_e, s + i, e - i, 0)) paint(col, i, e, HL_KEYWORD);
                else if (inlist(ty_e, s + i, e - i, 0)) paint(col, i, e, HL_TYPE);
            } else if (lang == L_PY) {
                if (inlist(kw_py, s + i, e - i, 0)) paint(col, i, e, HL_KEYWORD);
            }
            i = e;
            continue;
        }
        i++;
    }
    return st;
}
