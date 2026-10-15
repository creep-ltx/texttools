/* filecol.h - the family's file kinds and colours (ENVARC:FileColours),
 * CUtils' lib/cu_colours.c: the CU_K_ enum and its four functions. */
#ifndef FILECOL_H
#define FILECOL_H
#include <exec/types.h>
#include <dos/dos.h>
#ifndef FIBF_HIDDEN
#define FIBF_HIDDEN (1 << 7)     /* the h bit: not in the NDK's dos.h */
#endif
enum { CU_K_TEXT, CU_K_DRAWER, CU_K_LINK, CU_K_PROGRAM, CU_K_SCRIPT, CU_K_ARCHIVE,
       CU_K_PICTURE, CU_K_SOUND, CU_K_SOURCE, CU_K_ICON, CU_K_HIDDEN, CU_K_COUNT };
void cu_colours_load(void);
int cu_kind(const char *name, LONG type, LONG prot);
const char *cu_sgr(int kind, int cterm);   /* cterm: CTerm's numbers, else a plain window's */
int cu_iscterm(BPTR fh);                   /* the handle is a CTerm console */
#endif
