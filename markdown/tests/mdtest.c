/* mdtest - md.c run on the PC: a Markdown file made into rows, printed
 * with ANSI colours (or plain with -p), and what was found beside them.
 *   mdtest [-w WIDTH] [-p] [-a] [-l] FILE
 * -a: ASCII only (a terminal without Unicode), -l: list links, headings,
 * tasks and pictures too. Used by run.sh, and to look at the result. */
#include <stdio.h>
#include "../src/md.c"
#include "../src/syntax.c"

int tabsize = 8;
const char *basename_(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
int tui_cterm = 1, tui_utf8 = 1;
ULONG tui_box(int which)
{
    static const ULONG uni[] = { 0x2500, 0x2502, 0x256D, 0x256E, 0x2570, 0x256F, 0x25B8, 0x00B7 };
    static const char asc[] = "-|++++>.";
    return tui_utf8 ? uni[which] : (unsigned char)asc[which];
}

static void put8(ULONG c)
{
    char b[4];
    int n = enc8(c, b);
    fwrite(b, 1, n, stdout);
}

int main(int argc, char **argv)
{
    int width = 72, plain = 0, list = 0, i;
    const char *path = NULL;
    FILE *f;
    static char text[1 << 18];
    long n;
    Buf b;
    Doc d;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-w") && i + 1 < argc) width = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-p")) plain = 1;
        else if (!strcmp(argv[i], "-a")) tui_utf8 = 0;
        else if (!strcmp(argv[i], "-l")) list = 1;
        else path = argv[i];
    }
    if (!path || !(f = fopen(path, "rb"))) return 2;
    n = (long)fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    memset(&b, 0, sizeof(b));
    b.enc = ENC_UTF8;
    {
        long k, start = 0, lines = 1;
        for (k = 0; k < n; k++) if (text[k] == '\n') lines++;
        b.l = calloc(lines, sizeof(Line));
        for (k = 0; k <= n; k++)
            if (k == n || text[k] == '\n') {
                b.l[b.n].s = text + start;
                b.l[b.n].len = k - start;
                b.n++;
                start = k + 1;
            }
        if (b.n > 1 && b.l[b.n - 1].len == 0) b.n--;
    }
    md_unicode = tui_utf8;
    md_cellw = 8;
    md_cellh = 16;
    md_maxpicrows = 12;
    memset(&d, 0, sizeof(d));
    md_render(&d, &b, width, path);
    for (i = 0; i < d.nrows; i++) {
        RRow *r = &d.rows[i];
        int k, lf = -1, lb = -1, la = -1;
        if (!plain) printf("\x1b[2m%4ld\x1b[0m ", r->src + 1);
        for (k = 0; k < r->len; k++) {
            RCell *c = &d.cells[r->cell + k];
            if (!plain && (c->fg != lf || c->bg != lb || c->at != la)) {
                printf("\x1b[0");
                if (c->at & TA_BOLD) printf(";1");
                if (c->at & TA_DIM) printf(";2");
                if (c->at & TA_ITAL) printf(";3");
                if (c->at & TA_UNDER) printf(";4");
                if (c->at & TA_REV) printf(";7");
                if (c->at & TA_STRIKE) printf(";9");
                if (c->fg != TC_DEFAULT) printf(";%d", c->fg < 8 ? 30 + c->fg : 90 + c->fg - 8);
                if (c->bg != TC_DEFAULT) printf(";%d", c->bg < 8 ? 40 + c->bg : 100 + c->bg - 8);
                printf("m");
                lf = c->fg, lb = c->bg, la = c->at;
            }
            put8(c->c);
        }
        if (!plain) printf("\x1b[0m");
        if (r->pic >= 0) printf("   <picture %d>", r->pic);
        printf("\n");
    }
    if (list) {
        printf("-- %ld rows, %ld cells, %ld links, %ld headings, %ld tasks, %ld pictures%s\n",
               d.nrows, d.ncells, d.nlinks, d.nheads, d.ntasks, d.npics, d.failed ? " FAILED" : "");
        for (i = 0; i < d.nheads; i++)
            printf("head %d row %ld src %ld '%s' #%s\n", d.heads[i].level, d.heads[i].row,
                   d.heads[i].src + 1, md_str(&d, d.heads[i].text), md_str(&d, d.heads[i].anchor));
        for (i = 0; i < d.nspans; i++)
            printf("span row %ld %d-%d link %d -> %s\n", d.spans[i].row, d.spans[i].c0, d.spans[i].c1,
                   d.spans[i].link, md_str(&d, d.links[d.spans[i].link - 1].url));
        for (i = 0; i < d.ntasks; i++)
            printf("task row %ld col %d src %ld scol %ld done %d\n", d.tasks[i].row, d.tasks[i].col,
                   d.tasks[i].src + 1, d.tasks[i].scol, d.tasks[i].done);
        for (i = 0; i < d.npics; i++)
            printf("pic row %ld col %d %dx%d path '%s' alt '%s'\n", d.pics[i].row, d.pics[i].col,
                   d.pics[i].cols, d.pics[i].rows, md_str(&d, d.pics[i].path), md_str(&d, d.pics[i].alt));
    }
    md_free(&d);
    free(b.l);
    return 0;
}
