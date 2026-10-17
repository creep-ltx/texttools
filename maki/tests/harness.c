/* harness - Maki's indexing and searching run on the PC, on real files:
 *   harness FILE PATTERN [back]   prints the matching line, or "not found",
 * and how long it took. The screen is a stand-in grid (tui_* below). */
#include <time.h>
#define main maki_main
#include "../src/main.c"
#undef main
ULONG fake_signals;
int tui_cols = 80, tui_rows = 25, tui_cterm, tui_utf8 = 1, tui_cellw, tui_cellh;
void tui_console(BPTR in) { (void)in; }
int tui_possible(void) { return 1; }
int tui_open(void) { return 1; }
void tui_close(void) {}
void tui_suspend(void) {}
void tui_resume(void) {}
void tui_keyafter(void) {}
void tui_put(int c, int r, const char *s, int l, int f, int b, int a) { (void)c; (void)r; (void)s; (void)l; (void)f; (void)b; (void)a; }
void tui_putc(int c, int r, ULONG ch, int f, int b, int a) { (void)c; (void)r; (void)ch; (void)f; (void)b; (void)a; }
void tui_fill(int c, int r, int w, ULONG ch, int f, int b, int a) { (void)c; (void)r; (void)w; (void)ch; (void)f; (void)b; (void)a; }
void tui_clear(void) {}
void tui_cursor(int c, int r, int on) { (void)c; (void)r; (void)on; }
void tui_flush(void) {}
void tui_redraw(void) {}
int tui_resized(void) { return 0; }
void tui_title(const char *t) { (void)t; }
int tui_wait(TuiEv *e, long us, ULONG x, ULONG *g) { (void)e; (void)us; (void)x; (void)g; return 0; }
void tui_beep(void) {}
ULONG tui_box(int w) { (void)w; return '+'; }
long tui_charwidth(ULONG c) { return c >= 0x1100 && c < 0xA000 ? 2 : 1; }
void keyname(int k, char *d, int s) { (void)k; (void)s; d[0] = 0; }
int keys_all(int a, char *n, int m) { (void)a; (void)m; n[0] = 0; return 0; }
int keys_find(int k) { (void)k; return 0; }
void keys_load(void) {}
char keysbad[120];
const char *const actdesc[A_COUNT];

int main(int argc, char **argv)
{
    char err[100];
    clock_t t0;
    if (argc < 3) return 2;
    src_init();
    lb = AllocVec(MAXLB + MAXC, 0);
    lc = AllocVec(MAXC * sizeof(LCell), 0);
    lp = lb + MAXLB;
    files[0].s = src_file(argv[1], err);
    if (!files[0].s) { printf("cannot open%s\n", err); return 1; }
    files[0].mline = -1;
    nfiles = 1;
    strcpy(findtext, argv[2]);
    t0 = clock();
    search(&files[0], argc > 3);
    printf("%s: match line %ld, top %ld, %.3f s, msg \"%s\"\n", argv[2], files[0].mline + 1, files[0].tl + 1,
           (double)(clock() - t0) / CLOCKS_PER_SEC, msg);
    return 0;
}
