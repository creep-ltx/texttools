/* A stand-in for the Amiga headers: enough of exec and dos, on Linux
 * files, to run Fude's Markdown renderer on the PC (tests/run.sh).
 * (From Maki's tests.) */
#ifndef FAKE_AMIGA_H
#define FAKE_AMIGA_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef unsigned char UBYTE; typedef unsigned short UWORD; typedef int32_t LONG;
typedef uint32_t ULONG; typedef short WORD; typedef char *STRPTR; typedef void *APTR;
typedef intptr_t BPTR; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define BADDR(x) ((void *)(x))
#define MKBADDR(x) ((BPTR)(x))
#define MEMF_ANY 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 0x10000
#define SHARED_LOCK -2
#define MODE_OLDFILE 1005
#define MODE_NEWFILE 1006
#define OFFSET_BEGINNING -1
#define OFFSET_CURRENT 0
#define OFFSET_END 1
#define DOS_FIB 2
#define DOS_STDPKT 3
#define ACTION_READ 'R'
#define PA_IGNORE 2
#define SIGBREAKF_CTRL_C (1 << 12)
#define TAG_END 0
#define SYS_Input 1
#define SYS_Output 2
struct DateStamp { LONG ds_Days, ds_Minute, ds_Tick; };
struct FileInfoBlock { LONG fib_DirEntryType; };
struct FileHandle { void *fh_Type; LONG fh_Arg1; BPTR fh_Buf; LONG fh_Pos, fh_End; };
struct DosPacket { LONG dp_Type, dp_Res1, dp_Arg1, dp_Arg2, dp_Arg3; };
struct MsgPort { UBYTE mp_Flags, mp_SigBit; };
struct Task { char *tc_SPUpper, *tc_SPLower; };
struct StackSwapStruct { APTR stk_Lower; ULONG stk_Upper; APTR stk_Pointer; };
static inline void *AllocVec(ULONG n, ULONG f) { return (f & MEMF_CLEAR) ? calloc(1, n) : malloc(n); }
static inline void FreeVec(void *p) { free(p); }
static inline BPTR Lock(STRPTR p, LONG m) { FILE *f = fopen(p, "rb"); (void)m; return (BPTR)f; }
static inline void UnLock(BPTR l) { if (l) fclose((FILE *)l); }
static inline LONG Examine(BPTR l, struct FileInfoBlock *f) { (void)l; f->fib_DirEntryType = -3; return 1; }
static inline void *AllocDosObject(ULONG t, void *x) { (void)x; return calloc(1, t == DOS_FIB ? sizeof(struct FileInfoBlock) : sizeof(struct DosPacket)); }
static inline void FreeDosObject(ULONG t, void *p) { (void)t; free(p); }
static inline LONG IoErr(void) { return 205; }
static inline LONG Fault(LONG c, STRPTR h, STRPTR b, LONG n) { (void)c; (void)h; snprintf(b, n, ": not found"); return 1; }
static inline BPTR Open(STRPTR p, LONG m) { return (BPTR)fopen(p, m == MODE_OLDFILE ? "rb" : "wb"); }
static inline LONG Close(BPTR f) { return f ? fclose((FILE *)f) == 0 : 0; }
static inline LONG Seek(BPTR f, LONG o, LONG m) { long old = ftell((FILE *)f); fseek((FILE *)f, o, m == OFFSET_BEGINNING ? SEEK_SET : m == OFFSET_END ? SEEK_END : SEEK_CUR); return old; }
static inline LONG Read(BPTR f, APTR b, LONG n) { return (LONG)fread(b, 1, n, (FILE *)f); }
static inline LONG Write(BPTR f, const void *b, LONG n) { return (LONG)fwrite(b, 1, n, f ? (FILE *)f : stdout); }
static inline struct MsgPort *CreateMsgPort(void) { return calloc(1, sizeof(struct MsgPort)); }
static inline void DeleteMsgPort(struct MsgPort *p) { free(p); }
static inline void SendPkt(struct DosPacket *d, void *port, struct MsgPort *r) { (void)d; (void)port; (void)r; }
static inline void *GetMsg(struct MsgPort *p) { (void)p; return NULL; }
static inline void Delay(LONG t) { (void)t; }
extern ULONG fake_signals;
static inline ULONG SetSignal(ULONG n, ULONG m) { ULONG o = fake_signals; fake_signals = (fake_signals & ~m) | (n & m); return o; }
static inline LONG GetVar(STRPTR n, STRPTR b, LONG s, LONG f) { (void)n; (void)b; (void)s; (void)f; return -1; }
static inline LONG PutStr(STRPTR s) { return fputs(s, stdout); }
static inline BPTR Input(void) { return 0; }
static inline BPTR Output(void) { return 0; }
static inline LONG IsInteractive(BPTR f) { (void)f; return 0; }
static inline LONG SystemTags(STRPTR c, ...) { (void)c; return 0; }
static inline struct Task *FindTask(void *n) { static struct Task t; (void)n; return &t; }
static inline void StackSwap(struct StackSwapStruct *s) { (void)s; }
#endif
