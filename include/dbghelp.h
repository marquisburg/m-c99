/* <dbghelp.h> shim for c99mtlc.
 * dbghelp.dll is not in the libmtlc PE import set (kernel32/ucrtbase/msvcrt),
 * so these are static no-op stubs: symbolization APIs report failure and
 * callers degrade to address-only backtraces. */
#ifndef _DBGHELP_H
#define _DBGHELP_H

#include <windows.h>

#define SYMOPT_UNDNAME 0x00000002
#define SYMOPT_DEFERRED_LOADS 0x00000004
#define SYMOPT_LOAD_LINES 0x00000010
#define SYMOPT_FAIL_CRITICAL_ERRORS 0x00000200
#define SYMOPT_INCLUDE_32BIT_MODULES 0x00002000
#define SYMOPT_NO_PROMPTS 0x00080000
#define MAX_SYM_NAME 2000

typedef struct _SYMBOL_INFO {
  ULONG SizeOfStruct;
  ULONG TypeIndex;
  ULONG64 Reserved[2];
  ULONG Index;
  ULONG Size;
  ULONG64 ModBase;
  ULONG Flags;
  ULONG64 Value;
  ULONG64 Address;
  ULONG Register;
  ULONG Scope;
  ULONG Tag;
  ULONG NameLen;
  ULONG MaxNameLen;
  CHAR Name[1];
} SYMBOL_INFO, *PSYMBOL_INFO;

typedef struct _IMAGEHLP_LINE64 {
  DWORD SizeOfStruct;
  PVOID Key;
  DWORD LineNumber;
  PCHAR FileName;
  DWORD64 Address;
} IMAGEHLP_LINE64, *PIMAGEHLP_LINE64;

typedef enum {
  AddrMode1616,
  AddrMode1632,
  AddrModeReal,
  AddrModeFlat
} ADDRESS_MODE;

typedef struct _ADDRESS64 {
  DWORD64 Offset;
  WORD Segment;
  ADDRESS_MODE Mode;
} ADDRESS64;

typedef struct _KDHELP64 {
  DWORD64 Thread;
  DWORD ThCallbackStack;
  DWORD ThCallbackBStore;
  DWORD NextCallback;
  DWORD FramePointer;
  DWORD64 KiCallUserMode;
  DWORD64 KeUserCallbackDispatcher;
  DWORD64 SystemRangeStart;
  DWORD64 KiUserExceptionDispatcher;
  DWORD64 StackBase;
  DWORD64 StackLimit;
  DWORD BuildVersion;
  DWORD RetpolineStubFunctionTableSize;
  DWORD64 RetpolineStubFunctionTable;
  DWORD RetpolineStubOffset;
  DWORD RetpolineStubSize;
  DWORD64 Reserved0[2];
} KDHELP64;

typedef struct _STACKFRAME64 {
  ADDRESS64 AddrPC;
  ADDRESS64 AddrReturn;
  ADDRESS64 AddrFrame;
  ADDRESS64 AddrStack;
  ADDRESS64 AddrBStore;
  PVOID FuncTableEntry;
  DWORD64 Params[4];
  BOOL Far;
  BOOL Virtual;
  DWORD64 Reserved[3];
  KDHELP64 KdHelp;
} STACKFRAME64, *LPSTACKFRAME64;

#define IMAGE_FILE_MACHINE_AMD64 0x8664

typedef BOOL (*PREAD_PROCESS_MEMORY_ROUTINE64)(HANDLE h, DWORD64 addr,
                                               PVOID buf, DWORD n,
                                               LPDWORD read);
typedef PVOID (*PFUNCTION_TABLE_ACCESS_ROUTINE64)(HANDLE h, DWORD64 addr);
typedef DWORD64 (*PGET_MODULE_BASE_ROUTINE64)(HANDLE h, DWORD64 addr);
typedef DWORD64 (*PTRANSLATE_ADDRESS_ROUTINE64)(HANDLE h, HANDLE t,
                                                ADDRESS64 *a);

static DWORD SymSetOptions(DWORD opts) { return opts; }
static BOOL SymInitialize(HANDLE proc, PCSTR search_path, BOOL invade) {
  (void)proc;
  (void)search_path;
  (void)invade;
  return FALSE;
}
static BOOL SymCleanup(HANDLE proc) {
  (void)proc;
  return FALSE;
}
static BOOL SymFromAddr(HANDLE proc, DWORD64 addr, DWORD64 *disp,
                        PSYMBOL_INFO sym) {
  (void)proc;
  (void)addr;
  (void)disp;
  (void)sym;
  return FALSE;
}
static BOOL SymGetLineFromAddr64(HANDLE proc, DWORD64 addr, DWORD *disp,
                                 PIMAGEHLP_LINE64 line) {
  (void)proc;
  (void)addr;
  (void)disp;
  (void)line;
  return FALSE;
}
static PVOID SymFunctionTableAccess64(HANDLE proc, DWORD64 base) {
  (void)proc;
  (void)base;
  return (PVOID)0;
}
static DWORD64 SymGetModuleBase64(HANDLE proc, DWORD64 addr) {
  (void)proc;
  (void)addr;
  return 0;
}
static BOOL StackWalk64(DWORD machine, HANDLE proc, HANDLE thread,
                        LPSTACKFRAME64 frame, PVOID ctx,
                        PREAD_PROCESS_MEMORY_ROUTINE64 read_mem,
                        PFUNCTION_TABLE_ACCESS_ROUTINE64 fta,
                        PGET_MODULE_BASE_ROUTINE64 gmb,
                        PTRANSLATE_ADDRESS_ROUTINE64 xlate) {
  (void)machine;
  (void)proc;
  (void)thread;
  (void)frame;
  (void)ctx;
  (void)read_mem;
  (void)fta;
  (void)gmb;
  (void)xlate;
  return FALSE;
}

#endif /* _DBGHELP_H */
