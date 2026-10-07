/* tccwin.h - bridges TCC 0.9.27's bundled WinAPI headers (a 2006-era
 * w64 mingw-runtime snapshot) to modern Windows.
 *
 * WHY THIS EXISTS
 *   Two independent gaps in the TCC 0.9.27 distribution:
 *     1. LINKING  - lib/kernel32.def is a 2006 export list and lacks most
 *                   post-XP APIs. Fixed by regenerating it from the live DLL:
 *                     tcc -impdef C:\Windows\System32\kernel32.dll -o kernel32.def
 *                   then compiling with -L<dir-containing-that-def>.
 *     2. COMPILING - the headers have no prototypes or types for those APIs.
 *                   That is what this header supplies.
 *
 * USAGE - the compat header must come BEFORE <windows.h>:
 *   #include "tccwin.h"   // pulls in <windows.h> itself
 *   #include <stdio.h>
 *   tcc -D_WIN32_WINNT=0x0601 -Ltccimp prog.c -lkernelbase
 *
 * Everything is guarded so it stays a no-op against a complete WinAPI.
 */
#ifndef TCCWIN_H
#define TCCWIN_H

/* ---------------------------------------------------------------------------
 * WORKAROUND: broken Interlocked macros in TCC 0.9.27's winbase.h
 *
 * On __x86_64, winbase.h:927 macro-mangles every Interlocked name to the
 * compiler-intrinsic spelling, e.g.
 *     #define InterlockedExchangeAdd _InterlockedExchangeAdd
 * TCC supplies builtins for most of those, but NOT for InterlockedExchangeAdd
 * or InterlockedExchangeAdd64. They then become plain externs that no DLL
 * exports (`_InterlockedExchangeAdd` exists in no system DLL), so linking
 * fails with:  tcc: error: undefined symbol '_InterlockedExchangeAdd'
 *
 * Defining NOWINBASEINTERLOCK takes winbase.h's own #else branch (line ~970),
 * which declares the real, unmangled kernel32 exports.
 *
 * IMPORTANT: this must be defined BEFORE <windows.h> is first included.
 * ------------------------------------------------------------------------- */
#if !defined(NOWINBASEINTERLOCK)
#  define NOWINBASEINTERLOCK
#endif

#include <windows.h>

/* winbase.h's NOWINBASEINTERLOCK #else branch (line ~970) omits declarations
 * for InterlockedExchangeAdd / InterlockedExchangeAdd64 / InterlockedAdd, and
 * winnt.h additionally re-mangles the x86-64 spellings in a block guarded by
 * _AMD64_ that NOWINBASEINTERLOCK does not reach. Either way these three end up
 * unresolved ("undefined symbol 'InterlockedExchangeAdd'"), so they are
 * implemented below in terms of InterlockedCompareExchange. */
#if defined(__x86_64)
#  if defined(InterlockedExchangeAdd)
#    undef InterlockedExchangeAdd
#    undef InterlockedExchangeAdd64
#    undef InterlockedAdd
#  endif
#  define TCWIN_INTERLOCKED_ADD_FIXED 1
#elif defined(__i386__)
#  define TCWIN_INTERLOCKED_I386 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- restore the inline helpers skipped by NOWINBASEINTERLOCK --- */
#ifndef TCWIN_INTERLOCKED_HELPERS
#define TCWIN_INTERLOCKED_HELPERS
#  ifndef InterlockedExchangePointer
#    define InterlockedExchangePointer(Target, Value) \
        ((PVOID)InterlockedExchange((PLONG)(Target), (LONG)(INT_PTR)(Value)))
#  endif
#  ifndef InterlockedCompareExchangePointer
__CRT_INLINE PVOID InterlockedCompareExchangePointer(
    PVOID volatile *Destination, PVOID Exchange, PVOID Comperand) {
    return (PVOID)(INT_PTR)InterlockedCompareExchange(
        (PLONG)Destination, (LONG)(INT_PTR)Exchange, (LONG)(INT_PTR)Comperand);
}
#  endif
#  ifndef InterlockedAnd64
__CRT_INLINE LONGLONG InterlockedAnd64(LONGLONG volatile *Destination, LONGLONG Value) {
    LONGLONG Old;
    do { Old = *Destination; }
    while (InterlockedCompareExchange64(Destination, Old & Value, Old) != Old);
    return Old;
}
#  endif
#  ifndef InterlockedOr64
__CRT_INLINE LONGLONG InterlockedOr64(LONGLONG volatile *Destination, LONGLONG Value) {
    LONGLONG Old;
    do { Old = *Destination; }
    while (InterlockedCompareExchange64(Destination, Old | Value, Old) != Old);
    return Old;
}
#  endif
#  ifndef InterlockedXor64
__CRT_INLINE LONGLONG InterlockedXor64(LONGLONG volatile *Destination, LONGLONG Value) {
    LONGLONG Old;
    do { Old = *Destination; }
    while (InterlockedCompareExchange64(Destination, Old ^ Value, Old) != Old);
    return Old;
}
#  endif

/* The genuinely broken operations.
 *
 * On Windows 10 the real kernel32.dll exports NONE of the plain Interlocked*
 * names - they are compiler intrinsics supplied by the toolchain. TCC has
 * builtins for most spellings but not for these, so they are implemented here
 * in terms of InterlockedCompareExchange, which TCC DOES provide.
 *
 * Note: the bundled 0.9.27 kernel32.def lists InterlockedIncrement/Decrement/
 * Exchange/ExchangeAdd/CompareExchange as exports, but Windows 10's kernel32
 * does not actually export them - those entries are stale XP-era stubs. This
 * file therefore avoids depending on them entirely. */
#  ifdef TCWIN_INTERLOCKED_ADD_FIXED
__CRT_INLINE LONG InterlockedExchangeAdd(LONG volatile *Addend, LONG Value) {
    LONG Old;
    do { Old = *Addend; }
    while (InterlockedCompareExchange(Addend, Old + Value, Old) != Old);
    return Old;
}
__CRT_INLINE LONGLONG InterlockedExchangeAdd64(LONGLONG volatile *Addend, LONGLONG Value) {
    LONGLONG Old;
    do { Old = *Addend; }
    while (InterlockedCompareExchange64(Addend, Old + Value, Old) != Old);
    return Old;
}
__CRT_INLINE LONG InterlockedAdd(LONG volatile *Addend, LONG Value) {
    return InterlockedExchangeAdd(Addend, Value) + Value;
}
#  endif

/* --- i386: TCC provides NO interlocked builtins and the DLLs export none of
 * the mangled intrinsic names, so every operation is implemented here with
 * the x86 LOCK prefix directly. Verified under real thread contention. --- */
#  ifdef TCWIN_INTERLOCKED_I386
__CRT_INLINE LONG InterlockedIncrement(LONG volatile *Addend) {
    LONG old = 1;
    __asm__ __volatile__("lock ; xaddl %0, %1"
                         : "+r"(old), "+m"(*Addend) : : "memory");
    return old + 1;
}
__CRT_INLINE LONG InterlockedDecrement(LONG volatile *Addend) {
    LONG old = -1;
    __asm__ __volatile__("lock ; xaddl %0, %1"
                         : "+r"(old), "+m"(*Addend) : : "memory");
    return old - 1;
}
__CRT_INLINE LONG InterlockedExchangeAdd(LONG volatile *Addend, LONG Value) {
    __asm__ __volatile__("lock ; xaddl %0, %1"
                         : "+r"(Value), "+m"(*Addend) : : "memory");
    return Value;          /* xadd leaves the ORIGINAL value in the register */
}
__CRT_INLINE LONG InterlockedExchange(LONG volatile *Target, LONG Value) {
    __asm__ __volatile__("xchgl %0, %1"
                         : "+r"(Value), "+m"(*Target) : : "memory");
    return Value;
}
__CRT_INLINE LONG InterlockedCompareExchange(LONG volatile *Destination,
                                             LONG Exchange, LONG Comperand) {
    LONG prev;
    __asm__ __volatile__("lock ; cmpxchgl %2, %1"
                         : "=a"(prev), "+m"(*Destination)
                         : "r"(Exchange), "0"(Comperand)
                         : "memory");
    return prev;
}
__CRT_INLINE LONG InterlockedAdd(LONG volatile *Addend, LONG Value) {
    return InterlockedExchangeAdd(Addend, Value) + Value;
}
__CRT_INLINE LONG64 InterlockedCompareExchange64(LONG64 volatile *Destination,
                                                 LONG64 Exchange, LONG64 Comperand) {
    LONG64 prev;
    __asm__ __volatile__("lock ; cmpxchg8b %1"
                         : "=A"(prev), "+m"(*Destination)
                         : "b"((LONG)(Exchange & 0xFFFFFFFF)),
                           "c"((LONG)((Exchange >> 32) & 0xFFFFFFFF)),
                           "0"(Comperand)
                         : "memory");
    return prev;
}
__CRT_INLINE LONG64 InterlockedExchangeAdd64(LONG64 volatile *Addend, LONG64 Value) {
    LONG64 Old;
    do { Old = *Addend; }
    while (InterlockedCompareExchange64(Addend, Old + Value, Old) != Old);
    return Old;
}
__CRT_INLINE LONG64 InterlockedIncrement64(LONG64 volatile *Addend) {
    return InterlockedExchangeAdd64(Addend, 1) + 1;
}
__CRT_INLINE LONG64 InterlockedDecrement64(LONG64 volatile *Addend) {
    return InterlockedExchangeAdd64(Addend, -1) - 1;
}
__CRT_INLINE LONG64 InterlockedExchange64(LONG64 volatile *Target, LONG64 Value) {
    LONG64 Old;
    do { Old = *Target; }
    while (InterlockedCompareExchange64(Target, Value, Old) != Old);
    return Old;
}
#  endif /* TCWIN_INTERLOCKED_I386 */
#endif /* TCWIN_INTERLOCKED_HELPERS */

/* ======================= missing types ================================== */

/* --- INIT_ONCE (Vista+ one-time initialization) --- */
#ifndef TCWIN_INIT_ONCE_DEFINED
#define TCWIN_INIT_ONCE_DEFINED
typedef struct _TCWIN_INIT_ONCE {
    PVOID Ptr;
} INIT_ONCE, *PINIT_ONCE;
typedef WINBOOL (CALLBACK *PINIT_ONCE_FN)(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context);
#define INIT_ONCE_STATIC_INIT { 0 }
#endif

/* --- CONDITION_VARIABLE (Vista+) --- */
#ifndef TCWIN_CONDITION_VARIABLE_DEFINED
#define TCWIN_CONDITION_VARIABLE_DEFINED
typedef PVOID CONDITION_VARIABLE, *PCONDITION_VARIABLE;
#define CONDITION_VARIABLE_INIT { 0 }
#endif

/* --- SYNCHRONIZATION_BARRIER (Win8+) ---
 *
 * 类型与三个函数都是 Win8 才有的，2009 年的头文件集里没有，必须自己补。
 * 结构体尺寸以 MSDN 为准（2 个 HANDLE + 4 个 ULONG）。
 *
 * 实测（probe_winapi.c）：只定义类型不声明函数，`InitializeSynchronizationBarrier`
 * 会报 undeclared —— 这类"半个 API"很容易漏，故三者一起补。 */
#ifndef TCWIN_SYNC_BARRIER_DEFINED
#define TCWIN_SYNC_BARRIER_DEFINED
typedef struct _SYNCHRONIZATION_BARRIER {
    HANDLE Reserved1[2];
    ULONG  Reserved2;
    ULONG  Reserved3;
    ULONG  Reserved4;
    ULONG  Reserved5;
} SYNCHRONIZATION_BARRIER, *PSYNCHRONIZATION_BARRIER;

#define SYNCHRONIZATION_BARRIER_FLAGS_SPIN_ONLY  0x01
#define SYNCHRONIZATION_BARRIER_FLAGS_BLOCK_ONLY 0x02
#define SYNCHRONIZATION_BARRIER_FLAGS_NO_DELETE  0x04

WINBASEAPI WINBOOL WINAPI InitializeSynchronizationBarrier(
    PSYNCHRONIZATION_BARRIER lpBarrier, LONG lTotalThreads, LONG lSpinCount);
WINBASEAPI WINBOOL WINAPI EnterSynchronizationBarrier(
    PSYNCHRONIZATION_BARRIER lpBarrier, DWORD dwFlags);
WINBASEAPI WINBOOL WINAPI DeleteSynchronizationBarrier(
    PSYNCHRONIZATION_BARRIER lpBarrier);
#endif

/* --- file information classes (Vista+) ---
 * The real WinAPI type is an enum; a DWORD-sized typedef is ABI-identical
 * for passing to the exported functions and avoids clashing with any partial
 * definition that may exist in the bundled headers. */
#ifndef FILE_INFO_BY_HANDLE_CLASS
#ifndef TCWIN_FILE_INFO_CLASS_DEFINED
#define TCWIN_FILE_INFO_CLASS_DEFINED
typedef DWORD FILE_INFO_BY_HANDLE_CLASS;

/* Values match the real WinAPI FILE_INFO_BY_HANDLE_CLASS numbering. */
#define FileBasicInfo      ((FILE_INFO_BY_HANDLE_CLASS)0)
#define FileStandardInfo   ((FILE_INFO_BY_HANDLE_CLASS)1)
#define FileNameInfo       ((FILE_INFO_BY_HANDLE_CLASS)2)
#define FileRenameInfo     ((FILE_INFO_BY_HANDLE_CLASS)3)
#define FileDispositionInfo ((FILE_INFO_BY_HANDLE_CLASS)4)
#define FileAllocationInfo ((FILE_INFO_BY_HANDLE_CLASS)5)
#define FileEndOfFileInfo  ((FILE_INFO_BY_HANDLE_CLASS)6)
#define FileStreamInfo     ((FILE_INFO_BY_HANDLE_CLASS)7)
#endif
#endif

#ifndef TCWIN_FILE_INFO_STRUCTS_DEFINED
#define TCWIN_FILE_INFO_STRUCTS_DEFINED
typedef struct _TCWIN_FILE_BASIC_INFO {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    DWORD         FileAttributes;
} FILE_BASIC_INFO, *PFILE_BASIC_INFO;

typedef struct _TCWIN_FILE_STANDARD_INFO {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    DWORD         NumberOfLinks;
    WINBOOL       DeletePending;
    WINBOOL       Directory;
} FILE_STANDARD_INFO, *PFILE_STANDARD_INFO;

typedef struct _TCWIN_FILE_NAME_INFO {
    DWORD FileNameLength;
    WCHAR FileName[1];
} FILE_NAME_INFO, *PFILE_NAME_INFO;
#endif /* TCWIN_FILE_INFO_STRUCTS_DEFINED */

/* --- processor topology (Win7+) --- */
#ifndef TCWIN_PROC_TOPOLOGY_DEFINED
#define TCWIN_PROC_TOPOLOGY_DEFINED
#ifndef ALL_PROCESSOR_GROUPS
#  define ALL_PROCESSOR_GROUPS 0xFFFF
#endif

typedef struct _TCWIN_PROCESSOR_GROUP_INFO {
    BYTE  MaximumProcessorCount;
    BYTE  ActiveProcessorCount;
    BYTE  Reserved[38];
    KAFFINITY ActiveProcessorMask;
} TCWIN_PROCESSOR_GROUP_INFO;

typedef struct _TCWIN_GROUP_RELATIONSHIP {
    WORD                     MaximumGroupCount;
    WORD                     ActiveGroupCount;
    BYTE                     Reserved[20];
    TCWIN_PROCESSOR_GROUP_INFO GroupInfo[1];
} TCWIN_GROUP_RELATIONSHIP;

typedef struct _TCWIN_PROCESSOR_RELATIONSHIP {
    BYTE           Flags;
    BYTE           EfficiencyClass;
    BYTE           Reserved[20];
    WORD           GroupCount;
    TCWIN_GROUP_RELATIONSHIP GroupMask;
} TCWIN_PROCESSOR_RELATIONSHIP;

typedef struct _TCWIN_NUMA_NODE_RELATIONSHIP {
    DWORD          NodeNumber;
    BYTE           Reserved[18];
    WORD           GroupCount;
    TCWIN_GROUP_RELATIONSHIP GroupMask;
} TCWIN_NUMA_NODE_RELATIONSHIP;

typedef struct _TCWIN_CACHE_RELATIONSHIP {
    BYTE  Level;
    BYTE  Associativity;
    WORD  LineSize;
    DWORD CacheSize;
    int   Type;
    BYTE  Reserved[18];
    WORD  GroupCount;
    TCWIN_GROUP_RELATIONSHIP GroupMask;
} TCWIN_CACHE_RELATIONSHIP;

typedef struct _TCWIN_SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX {
    LOGICAL_PROCESSOR_RELATIONSHIP Relationship;
    DWORD Size;
    union {
        TCWIN_PROCESSOR_RELATIONSHIP Processor;
        TCWIN_NUMA_NODE_RELATIONSHIP NumaNode;
        TCWIN_CACHE_RELATIONSHIP      Cache;
        TCWIN_GROUP_RELATIONSHIP      Group;
    } u;
} SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, *PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX;
#endif /* TCWIN_PROC_TOPOLOGY_DEFINED */

/* --- locale / NLS -------------------------------------------------------
 *
 * TCC 精简头文件集里**没有 winnls.h**，而且它的 windows.h 把
 * `#include <winnls.h>` 注释掉了（windows.h:70），于是：
 *
 *   - NLSVERSIONINFO / LPNLSVERSIONINFO 未定义
 *   - CompareStringA / GetStringTypeA / GetStringTypeW 无声明
 *   - LCMapStringEx 的声明缺参数类型
 *
 * 修法不是在本文件里补 typedef，而是**用真实 SDK 头文件** ——
 * 把它从 winapi-full-for-0.9.27 取来放进 toolchain/tcc/include/winapi/，
 * 然后在这里包含。理由（见 PITFALLS P61）：
 *
 *   垫片曾经自己 typedef 了一个 4 字段版 NLSVERSIONINFO
 *   （多 dwEffectiveId + guidCustomVersion），而真实 SDK 的是 3 字段版。
 *   两者不可共存，且**真头文件必须优先** —— 否则结构体大小与 API
 *   期望不一致，会静默读错内存。
 *
 * 常量仍用 #ifndef 守卫：真头文件定义了就不重复。 */
#include <winnls.h>

#ifndef TCWIN_LOCALE_DEFINED
#define TCWIN_LOCALE_DEFINED
#ifndef LOCALE_NAME_INVARIANT
#  define LOCALE_NAME_INVARIANT L""
#  define LOCALE_NAME_SYSTEM_DEFAULT L"!x-sys-default-locale"
#endif
#ifndef LCMAP_UPPERCASE
#  define LCMAP_UPPERCASE 0x00000200
#  define LCMAP_LOWERCASE 0x00000100
#endif
#endif

/* ======================= missing prototypes ============================= */

/* --- timing --- */
WINBASEAPI ULONGLONG WINAPI GetTickCount64(VOID);
WINBASEAPI VOID WINAPI GetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime);
WINBASEAPI WINBOOL WINAPI QueryThreadCycleTime(HANDLE ThreadHandle, PULONG64 CycleTime);
WINBASEAPI WINBOOL WINAPI QueryProcessCycleTime(HANDLE ProcessHandle, PULONG64 CycleTime);

/* --- critical sections / one-time init --- */
#ifndef CRITICAL_SECTION_NO_DEBUG_INFO
#  define CRITICAL_SECTION_NO_DEBUG_INFO 0x01000000
#endif
WINBASEAPI WINBOOL WINAPI InitializeCriticalSectionEx(
    LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount, DWORD Flags);
WINBASEAPI VOID WINAPI InitOnceInitialize(PINIT_ONCE InitOnce);
WINBASEAPI WINBOOL WINAPI InitOnceExecuteOnce(
    PINIT_ONCE InitOnce, PINIT_ONCE_FN InitFn, PVOID Parameter, LPVOID *Context);

/* --- condition variables --- */
WINBASEAPI VOID WINAPI InitializeConditionVariable(PCONDITION_VARIABLE ConditionVariable);
WINBASEAPI WINBOOL WINAPI SleepConditionVariableCS(
    PCONDITION_VARIABLE ConditionVariable, PCRITICAL_SECTION CriticalSection, DWORD dwMilliseconds);
WINBASEAPI WINBOOL WINAPI SleepConditionVariableSRW(
    PCONDITION_VARIABLE ConditionVariable, PVOID SRWLock, DWORD dwMilliseconds, ULONG Flags);
WINBASEAPI VOID WINAPI WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable);
WINBASEAPI VOID WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable);

/* --- wait-on-address (Win8+) --- */
WINBASEAPI WINBOOL WINAPI WaitOnAddress(
    volatile VOID *Address, PVOID CompareAddress, SIZE_T AddressSize, DWORD dwMilliseconds);
WINBASEAPI VOID WINAPI WakeByAddressSingle(PVOID Address);
WINBASEAPI VOID WINAPI WakeByAddressAll(PVOID Address);

/* --- processor topology --- */
WINBASEAPI DWORD WINAPI GetActiveProcessorCount(WORD GroupNumber);
WINBASEAPI WORD  WINAPI GetActiveProcessorGroupCount(VOID);
WINBASEAPI WINBOOL WINAPI GetLogicalProcessorInformationEx(
    LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType,
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buffer, PDWORD ReturnedLength);

/* --- threads --- */
WINBASEAPI HRESULT WINAPI SetThreadDescription(HANDLE hThread, PCWSTR lpThreadDescription);
WINBASEAPI HRESULT WINAPI GetThreadDescription(HANDLE hThread, PWSTR *ppszThreadDescription);
WINBASEAPI DWORD WINAPI SetThreadErrorMode(DWORD dwNewMode, LPDWORD lpOldMode);
WINBASEAPI DWORD WINAPI GetThreadErrorMode(VOID);

/* --- paths / files --- */
WINBASEAPI DWORD WINAPI GetTempPath2A(DWORD nBufferLength, LPSTR lpBuffer);
WINBASEAPI DWORD WINAPI GetTempPath2W(DWORD nBufferLength, LPWSTR lpBuffer);
WINBASEAPI DWORD WINAPI GetFinalPathNameByHandleA(
    HANDLE hFile, LPSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags);
WINBASEAPI DWORD WINAPI GetFinalPathNameByHandleW(
    HANDLE hFile, LPWSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags);
#ifndef FILE_NAME_NORMALIZED
#  define FILE_NAME_NORMALIZED 0x0
#  define VOLUME_NAME_DOS      0x0
#  define VOLUME_NAME_GUID     0x1
#endif
WINBASEAPI WINBOOL WINAPI SetFileInformationByHandle(
    HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
    LPVOID lpFileInformation, DWORD dwBufferSize);
WINBASEAPI WINBOOL WINAPI GetFileInformationByHandleEx(
    HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
    LPVOID lpFileInformation, DWORD dwBufferSize);

/* --- thread pool (Vista+) --- */
#ifndef TCWIN_THREADPOOL_DEFINED
#define TCWIN_THREADPOOL_DEFINED
typedef struct _TCWIN_TP_WORK   *PTP_WORK;
typedef struct _TCWIN_TP_TIMER  *PTP_TIMER;
typedef struct _TCWIN_TP_WAIT   *PTP_WAIT;
typedef struct _TCWIN_TP_POOL   *PTP_POOL;
typedef struct _TCWIN_TP_CALLBACK_ENVIRON *PTP_CALLBACK_ENVIRON;

/* Callback signatures. The WORK/WAIT/TIMER INSTANCE pointer is actually
 * PTP_CALLBACK_INSTANCE in the real SDK; it starts with a pointer-sized
 * field in practice, so PVOID is a safe stand-in for passing through. */
typedef VOID (CALLBACK *PTP_WORK_CALLBACK)(PVOID Instance, PVOID Context, PTP_WORK Work);
typedef VOID (CALLBACK *PTP_SIMPLE_CALLBACK)(PVOID Instance, PVOID Context);
typedef VOID (CALLBACK *PTP_TIMER_CALLBACK)(PVOID Instance, PVOID Context, PTP_TIMER Timer);
typedef VOID (CALLBACK *PTP_WAIT_CALLBACK)(PVOID Instance, PVOID Context, PTP_WAIT Wait, DWORD WaitResult);

typedef struct _TCWIN_TP_CALLBACK_ENVIRON_V3 {
    DWORD Version;
    PTP_POOL Pool;
    PVOID CleanupGroup;
    PVOID CleanupGroupCancelCallback;
    PVOID RaceDll;
    PVOID ActivationContext;
    PVOID FinalizationCallback;
    union {
        DWORD Flags;
        struct {
            DWORD LongFunction : 1;
            DWORD Persistent   : 1;
            DWORD Private      : 30;
        } s;
    } u;
} TP_CALLBACK_ENVIRON, *PTP_CALLBACK_ENVIRON_V3;
#endif

WINBASEAPI WINBOOL WINAPI TrySubmitThreadpoolCallback(
    PTP_SIMPLE_CALLBACK pfns, PVOID pv, PTP_CALLBACK_ENVIRON pcbe);
WINBASEAPI PTP_WORK WINAPI CreateThreadpoolWork(
    PTP_WORK_CALLBACK pfnwk, PVOID pv, PTP_CALLBACK_ENVIRON pcbe);
WINBASEAPI VOID WINAPI SubmitThreadpoolWork(PTP_WORK pwk);
WINBASEAPI VOID WINAPI WaitForThreadpoolWorkCallbacks(PTP_WORK pwk, WINBOOL fCancelPendingCallbacks);
WINBASEAPI VOID WINAPI CloseThreadpoolWork(PTP_WORK pwk);

/* --- advapi32: token information classes & SID helpers (Vista+) --- */
#ifndef TCWIN_TOKEN_INFO_DEFINED
#define TCWIN_TOKEN_INFO_DEFINED
/* The bundled winnt.h predates these enumerators. The numeric values are the
 * stable ABI values from the Windows SDK. */
#ifndef TokenIntegrityLevel
#  define TokenIntegrityLevel ((TOKEN_INFORMATION_CLASS)25)
#endif
#ifndef TokenElevation
#  define TokenElevation      ((TOKEN_INFORMATION_CLASS)20)
#endif
#ifndef TokenElevationType
#  define TokenElevationType  ((TOKEN_INFORMATION_CLASS)18)
#endif
#ifndef TokenUIAccess
#  define TokenUIAccess       ((TOKEN_INFORMATION_CLASS)26)
#endif

#ifndef SE_GROUP_INTEGRITY
#  define SE_GROUP_INTEGRITY 0x00000020L
#endif

typedef struct _TCWIN_TOKEN_MANDATORY_LABEL {
    SID_AND_ATTRIBUTES Label;
} TOKEN_MANDATORY_LABEL, *PTOKEN_MANDATORY_LABEL;

typedef struct _TCWIN_TOKEN_ELEVATION {
    DWORD TokenIsElevated;
} TOKEN_ELEVATION, *PTOKEN_ELEVATION;

typedef struct _TCWIN_TOKEN_ELEVATION_TYPE {
    int ElevationType;
} TOKEN_ELEVATION_TYPE, *PTOKEN_ELEVATION_TYPE;
#endif /* TCWIN_TOKEN_INFO_DEFINED */

WINADVAPI WINBOOL WINAPI ConvertSidToStringSidA(PSID Sid, LPSTR *StringSid);
WINADVAPI WINBOOL WINAPI ConvertSidToStringSidW(PSID Sid, LPWSTR *StringSid);
WINADVAPI WINBOOL WINAPI ConvertStringSidToSidA(LPCSTR StringSid, PSID *Sid);
WINADVAPI LONG WINAPI RegDeleteTreeA(HKEY hKey, LPCSTR lpSubKey);
WINADVAPI LONG WINAPI RegDeleteTreeW(HKEY hKey, LPCWSTR lpSubKey);

/* --- string / locale --- */
WINBASEAPI int WINAPI CompareStringOrdinal(
    LPCWCH lpString1, int cchCount1, LPCWCH lpString2, int cchCount2, WINBOOL bIgnoreCase);
WINBASEAPI int WINAPI LCMapStringEx(
    LPCWSTR lpLocaleName, DWORD dwMapFlags, LPCWSTR lpSrcStr, int cchSrc,
    LPWSTR lpDestStr, int cchDest, LPNLSVERSIONINFO lpVersionInformation,
    LPVOID lpReserved, LPARAM sortHandle);

/* --- 代码页与区域查询：**不再由本垫片声明** -----------------------------
 *
 * GetACP / GetOEMCP / GetCPInfo / IsDBCSLeadByte / IsDBCSLeadByteEx /
 * GetSystemDefaultLCID / GetUserDefaultLCID / GetThreadLocale
 * 现在都由 <winapi/winnls.h> 提供（见本文件顶部对该头的包含）。
 *
 * 垫片曾自己声明这 8 个，并自造了一个 TCWIN_CPINFO 结构体。
 * 补上真 winnls.h 之后，`GetCPInfo` 出现
 *     incompatible types for redefinition
 * （真头文件用的是 CPINFO），导致所有用到 tccwin.h 的文件都编不过。
 *
 * 保留这段说明是为了提醒：**缺声明先查 winapi-full 里有没有对应头文件**，
 * 而不是在这里补。详见 PITFALLS P61。 */
/* --- __faststorefence ---------------------------------------------------
 *
 * TCC 的 winnt.h:1161 只**声明**了它：
 *     VOID __faststorefence(VOID);
 *     #define MemoryBarrier __faststorefence
 *
 * 实测两个现象：
 *   - 单独编译 sqlite3.c 得到 .o 后再链接，会报
 *       tcc: error: undefined symbol '__faststorefence'
 *     （TCC 自带实现只在它自己的代码生成路径里生效，不跨 .o）
 *   - 但如果这里直接定义成普通函数，又会报
 *       '__faststorefence' defined twice
 *     （TCC 在单文件编译时确实会提供一份）
 *
 * 所以用 **weak 符号**：有 TCC 自带版本时用它的，没有时用这里的。
 * 这是"补缺口但不与编译器抢定义"的标准做法。 */
#if defined(__TINYC__)
#  ifndef TCWIN_FASTSTOREFENCE_DEFINED
#  define TCWIN_FASTSTOREFENCE_DEFINED
__attribute__((weak)) void __faststorefence(void) {
#    if defined(_WIN64) || defined(__x86_64__)
    __asm__ __volatile__("xchgq %%rax, %%rax" ::: "memory", "rax");
#    else
    __asm__ __volatile__("lock; addl $0,(%%esp)" ::: "memory");
#    endif
}
#  endif
#endif
/* --- 代码页与宽字符转换 -----------------------------------------------
 *
 * TCC 的 windows.h **完全不提供** MultiByteToWideChar / WideCharToMultiByte
 * 的声明，也不提供 CP_UTF8 等代码页常量，但 kernel32.dll 里有这些导出
 * （已在 lib\tccdefs\kernel32.def 中）。很多库（SQLite、各种解析器）
 * 在 Windows 路径上都要用，所以在这里补齐。
 *
 * 用法：与其它 tccwin.h 提供的 API 一样，无需额外操作。
 *     #include "tccwin.h"
 *     int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, count);
 */
#ifndef TCWIN_CODEPAGE_DEFINED
#define TCWIN_CODEPAGE_DEFINED
#  define CP_ACP        0
#  define CP_OEMCP      1
#  define CP_MACCP      2
#  define CP_THREAD_ACP 3
#  define CP_SYMBOL     42
#  define CP_UTF7       65000
#  define CP_UTF8       65001
#  define MB_ERR_INVALID_CHARS  0x00000008
#  define WC_ERR_INVALID_CHARS  0x00000080
#  define WC_NO_BEST_FIT_CHARS  0x00000400
#  define WC_COMPOSITECHECK     0x00000200
#  define WC_DISCARDNS          0x00000010
#  define WC_SEPCHARS           0x00000020
#  define WC_DEFAULTCHAR        0x00000040
#endif
/* --- 代码页常量 ---------------------------------------------------------
 *
 * **函数声明已全部移除** —— GetACP / GetOEMCP / GetCPInfo /
 * IsDBCSLeadByte(Ex) / GetSystemDefaultLCID / GetUserDefaultLCID /
 * GetThreadLocale / MultiByteToWideChar / WideCharToMultiByte
 * 这 10 个现在都由 <winapi/winnls.h> 提供（本文件顶部已包含）。
 *
 * 垫片曾经自己声明它们并自造了一个 TCWIN_CPINFO 结构体。问题是
 * 真头文件用 CPINFO，于是 `GetCPInfo` 出现
 *     incompatible types for redefinition
 * 而且是**所有**用到 tccwin.h 的文件都编不过。
 *
 * 这是"往缺失的头文件上打补丁"的典型代价：垫片与真头文件会漂移。
 * 正确做法是把缺的头文件补上（见 PITFALLS P61）。
 *
 * 只保留 winnls.h 里**没有**的常量。 */
#ifndef TCWIN_CODEPAGE_DEFINED
#define TCWIN_CODEPAGE_DEFINED
#  ifndef WC_ERR_INVALID_CHARS
#    define WC_ERR_INVALID_CHARS  0x00000080
#  endif
#  ifndef WC_NO_BEST_FIT_CHARS
#    define WC_NO_BEST_FIT_CHARS  0x00000400
#  endif
#  ifndef WC_COMPOSITECHECK
#    define WC_COMPOSITECHECK     0x00000200
#  endif
#  ifndef WC_DISCARDNS
#    define WC_DISCARDNS          0x00000010
#  endif
#  ifndef WC_SEPCHARS
#    define WC_SEPCHARS           0x00000020
#  endif
#  ifndef WC_DEFAULTCHAR
#    define WC_DEFAULTCHAR        0x00000040
#  endif
#endif

/* ======================= CRT 缺口（不是 Win32）=========================
 *
 * 这几项与 Win32 无关，是 **msvcrt.dll 时代差**造成的：TCC 链的是系统
 * msvcrt.dll，它是 C89 时代的，而 TCC 自带的头文件里**没有这些原型**。
 *
 * **实测要点：它们在 msvcrt.dll 里都有导出，所以只需要补声明，不需要自己实现。**
 * 验证方式：`tcc -impdef %WINDIR%\System32\msvcrt.dll` 生成导出表后逐个查，
 * 再写一段自己补原型的代码，确认编译+链接+真实调用都通过。
 *
 *   _aligned_malloc / _aligned_free  有导出 -> 补声明即可
 *   _controlfp / _statusfp           有导出 -> 补声明即可
 *   strerror_s                       有导出 -> 补声明即可
 *   strnlen                          有导出 -> 补声明即可
 *   _strtoi64 / _strtoui64           有导出 -> strtoll/strtoull 转发到它们
 *
 * **反例**：`_set_se_translator` 在 msvcrt.dll 里**没有导出**，
 * 且它依赖 SEH 翻译机制（TCC 没有），所以**故意不声明** —— 声明了也链不上。
 * 移植 MSVC 代码时遇到它必须改用别的方式做结构化异常处理。
 *
 * 注意 `strtoll` / `strtoull` / `strtof` / `strtold` 的**实现**在
 * `helper/msvcrt_compat.c` 里（转发到 `_strtoi64` 等），此处只放声明。
 * 直接用这些函数的程序需要同时编译该文件；`-With argparse` / `-With toml`
 * 会自动带上。见 PITFALLS P7。 */
#ifndef TCWIN_CRT_COMPAT_DECL
#define TCWIN_CRT_COMPAT_DECL

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TCWIN_ALIGNED_MALLOC_DECLARED
#define TCWIN_ALIGNED_MALLOC_DECLARED
void * __cdecl _aligned_malloc(size_t _Size, size_t _Alignment);
void   __cdecl _aligned_free(void *_Memory);
void * __cdecl _aligned_realloc(void *_Memory, size_t _Size, size_t _Alignment);
#endif

#ifndef TCWIN_FLOAT_CTRL_DECLARED
#define TCWIN_FLOAT_CTRL_DECLARED
unsigned int __cdecl _controlfp(unsigned int _NewValue, unsigned int _Mask);
unsigned int __cdecl _statusfp(void);
#define _RC_CHOP   0x00000300
#define _RC_UP     0x00000200
#define _RC_DOWN   0x00000100
#define _RC_NEAR   0x00000000
#define _MCW_RC    0x00000300
#define _MCW_PC    0x00030000
#define _PC_24     0x00020000
#define _PC_53     0x00010000
#define _PC_64     0x00000000
#endif

#ifndef TCWIN_STRERROR_S_DECLARED
#define TCWIN_STRERROR_S_DECLARED
int    __cdecl strerror_s(char *_Buffer, size_t _SizeInBytes, int _ErrNum);
size_t __cdecl strnlen(const char *_Str, size_t _MaxCount);
#endif

#ifndef TCWIN_STRTOLL_DECLARED
#define TCWIN_STRTOLL_DECLARED
/* 实现在 helper/msvcrt_compat.c */
long long          __cdecl strtoll(const char *_Str, char **_EndPtr, int _Radix);
unsigned long long __cdecl strtoull(const char *_Str, char **_EndPtr, int _Radix);
float              __cdecl strtof(const char *_Str, char **_EndPtr);
long double        __cdecl strtold(const char *_Str, char **_EndPtr);
#endif

#ifdef __cplusplus
}
#endif

#ifndef TCWIN_CBRT_DECL_WIN
#define TCWIN_CBRT_DECL_WIN
/* 实现在 helper/msvcrt_compat.c。
 * 实测：msvcrt.dll 里连**双精度** cbrt 都没有导出，所以不能靠"转发到双精度版"
 * 解决（这与 strtof 那类"头里有声明但库不导出"不同）。 */
double __cdecl cbrt(double _X);
float  __cdecl cbrtf(float _X);
#endif

/* --- inet_pton / inet_ntop -------------------------------------------
 *
 * **实测：整个头文件集（1012 个，含 winapi/ 755 个）里都没有这两个的声明**，
 * 但 ws2_32.dll 里有导出（用 `tcc -impdef %WINDIR%\System32\ws2_32.dll` 查过）。
 * 原因是本头文件集来自 2009 年的 mingw-64，那时这两个还没进 ws2tcpip.h。
 *
 * 这是个**静默出错**的隐患，不只是"缺声明"：
 *
 *     PCSTR inet_ntop(...)      // 返回的是**指针**
 *
 * 缺原型时 C89 隐式声明按 int 返回 —— 32 位下侥幸能用，64 位下指针被
 * 截断成 4 字节，随后解引用就是访问违例或读到垃圾。与 PITFALLS P2 同类
 * （GetACP 隐式声明污染了同函数里随后的 MultiByteToWideChar 调用）。
 *
 * 签名取自 MSDN。只在已包含过 winsock 时声明，避免纯 Win32 程序被引入
 * socket 类型。 */
/* 注意：**不能**加 `#if defined(_WINSOCK2API_)` 这样的守卫。
 * tccwin.h 按约定是第一个被包含的头，那时 _WINSOCK2API_ 还没定义；
 * 而 winsock2.h 之后又包含 tccwin.h 时会被 TCCWIN_H 守卫挡住，
 * 于是带这个守卫的声明**永远不会被编译到**（实测踩过）。
 * socklen_t 用 typedef 自身的守卫防重复定义。 */
#ifndef TCWIN_INET_PTON_DECLARED
#define TCWIN_INET_PTON_DECLARED
#ifndef TCWIN_SOCKLEN_T
#define TCWIN_SOCKLEN_T
typedef int socklen_t;
#endif
__declspec(dllimport) INT   __stdcall inet_pton(INT Family, LPCSTR pszAddrString, PVOID pAddrBuf);
__declspec(dllimport) PCSTR __stdcall inet_ntop(INT Family, const VOID *pAddr, PSTR pStringBuf, size_t StringBufSize);
#endif

#endif /* TCWIN_CRT_COMPAT_DECL */

#ifdef __cplusplus
}
#endif
#endif /* TCCWIN_H */
