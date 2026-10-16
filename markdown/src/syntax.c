/* syntax - colours for code: comments, strings, keywords, types,
 * numbers, preprocessor lines and labels, per language. A line is
 * coloured from the state the line before left (inside a block comment,
 * inside a long string), so a change only colours again what it moved. */
#include <exec/types.h>
#include <string.h>
#include "markdown.h"

const char *const langnames[L_COUNT] = {
    "text", "C", "68k asm", "ARexx", "Amiga E", "AmigaGuide", "AmigaDOS", "Python", "Markdown",
    "shell", "Makefile", "JSON", "HTML/XML", "Lua", "Blitz", "Installer"
};

/* states between lines */
#define ST_NONE  0
#define ST_BLOCK 1              /* inside a block comment */
#define ST_LONG  2              /* inside a Python long string */
#define ST_HTMLC 3              /* inside an HTML comment */
#define ST_LUAC  4              /* inside a Lua block comment */

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
static const char *const kw_sh[] = {
    "if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done", "case", "esac",
    "in", "function", "return", "exit", "break", "continue", "local", "export", "readonly",
    "echo", "cd", "set", "unset", "shift", "source", "test", "true", "false", "eval", "exec", NULL
};
static const char *const kw_lua[] = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while", NULL
};
/* Blitz Basic 2 and AmiBlitz: the language's own words (the thousands
 * of library commands stay plain) */
static const char *const kw_blitz[] = {
    "if", "then", "else", "elseif", "endif", "end", "while", "wend", "repeat", "until",
    "forever", "for", "to", "step", "next", "select", "case", "default", "goto", "gosub",
    "return", "pop", "statement", "function", "shared", "dim", "list", "newtype", "deftype",
    "usepath", "let", "and", "or", "not", "xor", "mod", "data", "read", "restore", "on",
    "maxlen", "exit", "macro", "blitz", "amiga", "qamiga", "setint", "clrint", "seterr",
    "clrerr", "errfail", "runerrson", "runerrsoff", "fast", "true", "false", "null",
    "include", "xinclude", "incbin", "incdir", NULL
};
static const char *const pp_blitz[] = {   /* compiler directives */
    "include", "xinclude", "incbin", "incdir", "cnif", "csif", "celse", "cend", "wbstartup",
    "nocli", "optimize", "syntax", "closeed", "noliblines", "useasm", "runerrson",
    "runerrsoff", NULL
};
/* the Installer's usual first words: how a script without a name is known */
static const char *const fn_install[] = {
    "set", "welcome", "if", "procedure", "message", "complete", "copyfiles", "copylib",
    "makedir", "askdir", "user", "transcript", "onerror", "trap", "startup", "working",
    "debug", "makeassign", "exit", "run", "execute", "select", "while", "until", "abort",
    "askchoice", "askbool", "askoptions", "textfile", "tooltype", "delete", "rename", NULL
};
static const char *const kw_json[] = { "true", "false", "null", NULL };
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
    if (!strcmp(ext, "script")) return L_SCRIPT;
    if (!strcmp(ext, "sh") || !strcmp(ext, "bash") || !strcmp(ext, "zsh")) return L_SH;
    if (!strcmp(ext, "mk") || !strcmp(ext, "mak")) return L_MAKE;
    if (!strcmp(ext, "json")) return L_JSON;
    if (!strcmp(ext, "html") || !strcmp(ext, "htm") || !strcmp(ext, "xml") || !strcmp(ext, "svg"))
        return L_HTML;
    if (!strcmp(ext, "lua")) return L_LUA;
    if (!strcmp(ext, "bb") || !strcmp(ext, "bb2") || !strcmp(ext, "ab2") || !strcmp(ext, "ab3") ||
        !strcmp(ext, "abs") || !strcmp(ext, "bba")) return L_BLITZ;
    if (!strcmp(ext, "install") || !strcmp(ext, "installer")) return L_INSTALL;
    {   /* S: scripts have no ending */
        char low[40];
        for (i = 0; i < 39 && n[i]; i++) low[i] = (char)lower((unsigned char)n[i]);
        low[i] = 0;
        if (strstr(low, "startup") || !strcmp(low, "shell-startup") || !strcmp(low, "user-startup"))
            return L_SCRIPT;
        if (!strcmp(low, "makefile") || !strcmp(low, "gnumakefile") || !strncmp(low, "makefile.", 9))
            return L_MAKE;
        if (!strncmp(path, "S:", 2) || !strncmp(path, "s:", 2)) return L_SCRIPT;
        if (!strncmp(low, "install", 7) && !e) return L_INSTALL;
    }
    if (b) {    /* an Installer script: lines that start with ( */
        long k, lim = b->n < 40 ? b->n : 40;
        for (k = 0; k < lim; k++) {
            const char *s = b->l[k].s;
            long j = 0, n2 = b->l[k].len;
            while (j < n2 && (s[j] == ' ' || s[j] == '\t')) j++;
            if (j >= n2 || s[j] == ';') continue;
            if (s[j] == '(') {
                long e = ++j;
                while (e < n2 && (isid((unsigned char)s[e]) || s[e] == '-')) e++;
                if (inlist(fn_install, s + j, e - j, 1)) return L_INSTALL;
            }
            break;
        }
    }
    if (b && b->n > 0 && b->l[0].len >= 2) {
        const char *s = b->l[0].s;
        if (s[0] == '/' && s[1] == '*') return L_REXX;      /* ARexx starts with a comment */
        if (b->l[0].len >= 5 && !strncmp(s, "@data", 5)) return L_GUIDE;
        if (b->l[0].len >= 9 && !strncmp(s, "@database", 9)) return L_GUIDE;
        if (s[0] == '#' && s[1] == '!') {
            long k;
            for (k = 2; k + 2 < b->l[0].len; k++)
                if (!strncmp(s + k, "sh", 2)) return L_SH;
            return L_PY;
        }
        if (s[0] == '<' && (s[1] == '?' || s[1] == '!' || s[1] == 'h' || s[1] == 'H')) return L_HTML;
        if (s[0] == '{' || s[0] == '[') return L_JSON;
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
    if (lang == L_BLITZ) {
        long j = 0;
        while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
        if (j + 1 < len && s[j] == '.' && isid((unsigned char)s[j + 1])) {  /* .label */
            long e = j + 1;
            while (e < len && isid((unsigned char)s[e])) e++;
            paint(col, j, e, HL_LABEL);
            i = e;
        }
        while (i < len) {
            unsigned char c = (unsigned char)s[i];
            if (c == ';') {
                paint(col, i, len, HL_COMMENT);
                break;
            }
            if (c == '"') {
                long e = quoted(s, i, len, '"', 0);
                paint(col, i, e, HL_STRING);
                i = e;
                continue;
            }
            if ((c == '#' || c == '!') && i + 1 < len && isid((unsigned char)s[i + 1])) {
                long e = i + 1;      /* #constant, !macro */
                while (e < len && isid((unsigned char)s[e])) e++;
                paint(col, i, e, HL_PREPROC);
                i = e;
                continue;
            }
            if ((c >= '0' && c <= '9') || ((c == '$' || c == '%') && i + 1 < len &&
                isid((unsigned char)s[i + 1]))) {
                if (i == 0 || !isid((unsigned char)s[i - 1])) {
                    long e = number(s, i, len);
                    paint(col, i, e, HL_NUMBER);
                    i = e;
                    continue;
                }
            }
            if (c == '.' && i + 1 < len && isid((unsigned char)s[i + 1])) {
                long e = i + 1;      /* a type: .w .l .q .f .s .b, or a NEWTYPE's */
                while (e < len && isid((unsigned char)s[e])) e++;
                paint(col, i, e, HL_TYPE);
                i = e;
                continue;
            }
            if (isid(c)) {
                long e = i;
                while (e < len && isid((unsigned char)s[e])) e++;
                if (inlist(pp_blitz, s + i, e - i, 1)) paint(col, i, e, HL_PREPROC);
                else if (inlist(kw_blitz, s + i, e - i, 1)) paint(col, i, e, HL_KEYWORD);
                if (e < len && s[e] == '$') e++;    /* a$: a string variable */
                i = e;
                continue;
            }
            i++;
        }
        return ST_NONE;
    }
    if (lang == L_INSTALL) {
        /* the Installer's Lisp: (function args...), ; comments, @variables */
        while (i < len) {
            unsigned char c = (unsigned char)s[i];
            if (c == ';') {
                paint(col, i, len, HL_COMMENT);
                break;
            }
            if (c == '"' || c == '\'') {
                long e = quoted(s, i, len, (char)c, 1);
                paint(col, i, e, HL_STRING);
                i = e;
                continue;
            }
            if (c == '(') {
                long e = i + 1;
                while (e < len && (s[e] == ' ' || s[e] == '\t')) e++;
                i = e;
                while (e < len && s[e] != ' ' && s[e] != '\t' && s[e] != '(' && s[e] != ')' &&
                       s[e] != '"' && s[e] != ';') e++;
                paint(col, i, e, HL_KEYWORD);
                i = e;
                continue;
            }
            if (c == '@') {
                long e = i + 1;
                while (e < len && (isid((unsigned char)s[e]) || s[e] == '-')) e++;
                paint(col, i, e, HL_PREPROC);
                i = e;
                continue;
            }
            if (((c >= '0' && c <= '9') || c == '$' || c == '%') &&
                (i == 0 || !isid((unsigned char)s[i - 1]))) {
                long e = number(s, i, len);
                paint(col, i, e, HL_NUMBER);
                i = e;
                continue;
            }
            if (isid(c)) {
                while (i < len && (isid((unsigned char)s[i]) || s[i] == '-')) i++;
                continue;
            }
            i++;
        }
        return ST_NONE;
    }
    if (lang == L_HTML) {
        while (i < len) {
            if (st == ST_HTMLC) {
                long j = i;
                while (j + 2 < len && strncmp(s + j, "-->", 3)) j++;
                if (j + 2 < len) {
                    paint(col, i, j + 3, HL_COMMENT);
                    i = j + 3;
                    st = ST_NONE;
                } else {
                    paint(col, i, len, HL_COMMENT);
                    return ST_HTMLC;
                }
                continue;
            }
            if (i + 3 < len && !strncmp(s + i, "<!--", 4)) {
                st = ST_HTMLC;
                continue;
            }
            if (s[i] == '<') {                  /* a tag: its name, its attributes' values */
                long j = i + 1;
                while (j < len && s[j] != '>') {
                    if (s[j] == '"' || s[j] == '\'') {
                        long e = quoted(s, j, len, s[j], 0);
                        paint(col, j, e, HL_STRING);
                        j = e;
                        continue;
                    }
                    col[j] = HL_KEYWORD;
                    j++;
                }
                col[i] = HL_KEYWORD;
                if (j < len) col[j++] = HL_KEYWORD;
                i = j;
                continue;
            }
            if (s[i] == '&') {                  /* an entity */
                long j = i;
                while (j < len && s[j] != ';' && s[j] != ' ') j++;
                paint(col, i, j < len ? j + 1 : len, HL_NUMBER);
                i = j;
            }
            i++;
        }
        return st;
    }
    if (lang == L_MAKE) {
        long j;
        if (len && s[0] == '\t') {             /* a recipe line: the shell's */
            for (i = 0; i < len; i++)
                if (s[i] == '$' && i + 1 < len && (s[i + 1] == '(' || s[i + 1] == '{')) {
                    long e = i;
                    while (e < len && s[e] != ')' && s[e] != '}') e++;
                    paint(col, i, e < len ? e + 1 : len, HL_PREPROC);
                    i = e;
                } else if (s[i] == '#') {
                    paint(col, i, len, HL_COMMENT);
                    break;
                }
            return ST_NONE;
        }
        for (j = 0; j < len && s[j] != ':' && s[j] != '=' && s[j] != '#'; j++) ;
        if (j < len && s[j] == ':' && (j + 1 >= len || s[j + 1] != '=')) paint(col, 0, j + 1, HL_LABEL);
        else if (j < len && s[j] == '=') paint(col, 0, j, HL_TYPE);
        for (i = 0; i < len; i++)
            if (s[i] == '#') {
                paint(col, i, len, HL_COMMENT);
                break;
            } else if (s[i] == '$' && i + 1 < len && (s[i + 1] == '(' || s[i + 1] == '{')) {
                long e = i;
                while (e < len && s[e] != ')' && s[e] != '}') e++;
                paint(col, i, e < len ? e + 1 : len, HL_PREPROC);
                i = e;
            } else if (len >= 6 && i == 0 && (!strncmp(s, "ifeq", 4) || !strncmp(s, "ifneq", 5) ||
                       !strncmp(s, "ifdef", 5) || !strncmp(s, "endif", 5) || !strncmp(s, "else", 4) ||
                       !strncmp(s, "include", 7) || !strncmp(s, "define", 6))) {
                long e = 0;
                while (e < len && s[e] != ' ') e++;
                paint(col, 0, e, HL_KEYWORD);
                i = e;
            }
        return ST_NONE;
    }
    if (lang == L_JSON) {
        for (i = 0; i < len; i++) {
            if (s[i] == '"') {
                long e = quoted(s, i, len, '"', 1), k = e;
                while (k < len && (s[k] == ' ' || s[k] == '\t')) k++;
                paint(col, i, e, k < len && s[k] == ':' ? HL_TYPE : HL_STRING);
                i = e - 1;
            } else if ((s[i] >= '0' && s[i] <= '9') || s[i] == '-') {
                long e = i + 1;
                while (e < len && ((s[e] >= '0' && s[e] <= '9') || s[e] == '.' || s[e] == 'e' ||
                                   s[e] == 'E' || s[e] == '+' || s[e] == '-')) e++;
                paint(col, i, e, HL_NUMBER);
                i = e - 1;
            } else if (isid((unsigned char)s[i])) {
                long e = i;
                while (e < len && isid((unsigned char)s[e])) e++;
                if (inlist(kw_json, s + i, e - i, 0)) paint(col, i, e, HL_KEYWORD);
                i = e - 1;
            }
        }
        return ST_NONE;
    }
    if (lang == L_SH) {
        for (i = 0; i < len; i++) {
            if (s[i] == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
                paint(col, i, len, HL_COMMENT);
                break;
            }
            if (s[i] == '"' || s[i] == '\'') {
                long e = quoted(s, i, len, s[i], s[i] == '"');
                paint(col, i, e, HL_STRING);
                i = e - 1;
            } else if (s[i] == '$') {
                long e = i + 1;
                if (e < len && (s[e] == '{' || s[e] == '(')) {
                    while (e < len && s[e] != '}' && s[e] != ')') e++;
                    if (e < len) e++;
                } else
                    while (e < len && (isid((unsigned char)s[e]) || s[e] == '?' || s[e] == '@')) e++;
                paint(col, i, e, HL_PREPROC);
                i = e - 1;
            } else if (isid((unsigned char)s[i]) && (i == 0 || !isid((unsigned char)s[i - 1]))) {
                long e = i;
                while (e < len && (isid((unsigned char)s[e]) || s[e] == '-')) e++;
                if (inlist(kw_sh, s + i, e - i, 0)) paint(col, i, e, HL_KEYWORD);
                i = e - 1;
            }
        }
        return ST_NONE;
    }
    if (lang == L_LUA) {
        while (i < len) {
            if (st == ST_LUAC) {
                long j = i;
                while (j + 1 < len && !(s[j] == ']' && s[j + 1] == ']')) j++;
                if (j + 1 < len) {
                    paint(col, i, j + 2, HL_COMMENT);
                    i = j + 2;
                    st = ST_NONE;
                } else {
                    paint(col, i, len, HL_COMMENT);
                    return ST_LUAC;
                }
                continue;
            }
            if (i + 1 < len && s[i] == '-' && s[i + 1] == '-') {
                if (i + 3 < len && s[i + 2] == '[' && s[i + 3] == '[') {
                    st = ST_LUAC;
                    paint(col, i, i + 4, HL_COMMENT);
                    i += 4;
                    continue;
                }
                paint(col, i, len, HL_COMMENT);
                return st;
            }
            if (s[i] == '"' || s[i] == '\'') {
                long e = quoted(s, i, len, s[i], 1);
                paint(col, i, e, HL_STRING);
                i = e;
                continue;
            }
            if (s[i] >= '0' && s[i] <= '9' && (i == 0 || !isid((unsigned char)s[i - 1]))) {
                long e = number(s, i, len);
                paint(col, i, e, HL_NUMBER);
                i = e;
                continue;
            }
            if (isid((unsigned char)s[i])) {
                long e = i;
                while (e < len && isid((unsigned char)s[e])) e++;
                if (inlist(kw_lua, s + i, e - i, 0)) paint(col, i, e, HL_KEYWORD);
                i = e;
                continue;
            }
            i++;
        }
        return st;
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
