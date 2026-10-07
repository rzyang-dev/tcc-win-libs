/* 02-winapi.c - Windows API 与现代 API
 *
 * 演示两件事：
 *   1. tccwin.h 必须放在 <windows.h> 之前 —— 它做了三件事，
 *      解决 TCC 0.9.27 头文件层面无法用现代 API 的问题。
 *   2. 现代 API（GetTickCount64 / GlobalMemoryStatusEx / 条件变量 /
 *      InterlockedExchangeAdd）在这套工具链里是可用的。
 *
 * 编译： scripts\tcc.cmd examples\02-winapi.c -Run
 */
#include "tccwin.h"          /* 必须第一行；它内部会 include <windows.h> */
#include <stdio.h>

int main(void) {
    printf("=== Windows API ===\n");

    /* --- 现代内核 API --- */
    ULONGLONG up = GetTickCount64();
    printf("uptime        = %llu ms (%.2f h)\n", up, up / 3600000.0);

    MEMORYSTATUSEX ms;
    memset(&ms, 0, sizeof ms);
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms))
        printf("total RAM     = %.1f GiB, load %lu%%\n",
               ms.ullTotalPhys / 1073741824.0, ms.dwMemoryLoad);

    printf("active CPUs   = %lu\n", GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));

    /* --- 高精度时钟 --- */
    FILETIME a, b;
    GetSystemTimePreciseAsFileTime(&a);
    Sleep(50);
    GetSystemTimePreciseAsFileTime(&b);
    ULONGLONG da = ((ULONGLONG)a.dwHighDateTime << 32) | a.dwLowDateTime;
    ULONGLONG db = ((ULONGLONG)b.dwHighDateTime << 32) | b.dwLowDateTime;
    printf("Sleep(50)     = %.3f ms (sub-ms clock)\n", (db - da) / 10000.0);

    /* --- 原子操作：TCC 默认下 InterlockedExchangeAdd 无法链接 --- */
    volatile LONG counter = 0;
    for (int i = 0; i < 1000; i++) InterlockedExchangeAdd(&counter, 2);
    printf("interlocked   = %ld (expect 2000)\n", (long)counter);
    InterlockedIncrement(&counter);
    printf("after inc     = %ld\n", (long)counter);

    /* --- 条件变量（Vista+，TCC 头文件里完全没有） --- */
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    InitializeCriticalSectionEx(&cs, 4000, CRITICAL_SECTION_NO_DEBUG_INFO);
    InitializeConditionVariable(&cv);
    printf("cond var      = OK (InitializeConditionVariable)\n");
    DeleteCriticalSection(&cs);

    /* --- 类型宽度自检 --- */
    printf("\nsizes: DWORD=%d LARGE_INTEGER=%d SIZE_T=%d HANDLE=%d\n",
           (int)sizeof(DWORD), (int)sizeof(LARGE_INTEGER),
           (int)sizeof(SIZE_T), (int)sizeof(HANDLE));
    return 0;
}
