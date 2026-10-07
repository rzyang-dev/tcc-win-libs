/* probe_winapi.c —— 验证 winapi-full 头文件集补上的能力
 *
 * 背景：本环境的 toolchain/tcc/include/ 原来只有 TCC 发行版自带的 18 个
 * winapi 头。轮次目标第 2 步把官方 winapi-full-for-0.9.27/include/
 * 整批纳入（winapi 755 + ddk 138 + GL + sdks + sys + 顶层），
 * 并在 scripts/tcc.ps1 里把 include\winapi 子目录加入搜索路径。
 *
 * 这个探针的作用是**防止回归**：如果哪天头文件被删、包含路径被改坏、
 * 或 tccwin.h 的垫片与真头文件冲突，这里会立刻失败。
 *
 * 分四组：
 *   A. 编码页 / NLS      —— 之前靠 tccwin.h 手写声明，现在用真 winnls.h
 *   B. Vista/7 API       —— 2009 年头文件集里没有，靠 tccwin.h 垫片
 *   C. CRT 缺口          —— msvcrt 是 C89 时代，这些确实没有
 *   D. TCC 自身缺陷      —— float 数学与 C99 比较宏，靠 tccmath.h
 *
 * 注意：本文件刻意**不用多语句宏**（TCC 不能解析带分号的宏参数，
 * 见 PITFALLS）。每个断言单独写一行。
 */

#include "tccwin.h"      /* 必须第一个 */
#include "tccmath.h"     /* C99 比较宏 + float 数学 */

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_pass = 0;
static int g_fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
        printf("  [FAIL] %s\n", what);
    }
}

/* ------------------------------------------------------------------ */
/* A. 编码页 / NLS —— 真 winnls.h                                      */
/* ------------------------------------------------------------------ */
static void test_codepage(void)
{
    UINT acp;
    UINT oem;
    CPINFO ci;
    WCHAR wide[32];
    char narrow[64];
    int n;
    int r;

    acp = GetACP();
    oem = GetOEMCP();
    /* 中文 Windows 上是 936；任何 Windows 上都应是非零 */
    ck("A1 GetACP 返回非零", acp != 0);
    ck("A2 GetOEMCP 返回非零", oem != 0);

    memset(&ci, 0, sizeof ci);
    r = (int)GetCPInfo(CP_UTF8, &ci);
    ck("A3 GetCPInfo(CP_UTF8) 成功", r != 0);
    /* CP_UTF8 的 MaxCharSize 应为 4（UTF-8 最长 4 字节） */
    ck("A4 CP_UTF8 的 MaxCharSize == 4", ci.MaxCharSize == 4);

    /* UTF-8 -> UTF-16，用中文字符 "中" = E4 B8 AD */
    n = MultiByteToWideChar(CP_UTF8, 0, "\xE4\xB8\xAD", -1, wide, 32);
    ck("A5 MultiByteToWideChar 成功", n == 2);
    /* "中" 的 BMP 码点是 U+4E2D */
    ck("A6 '中' 转成 U+4E2D", n == 2 && wide[0] == 0x4E2D);
    ck("A7 末尾有 NUL", n == 2 && wide[1] == 0);

    /* UTF-16 -> UTF-8，转回来应还原 */
    memset(narrow, 0, sizeof narrow);
    n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow, 64, NULL, NULL);
    ck("A8 WideCharToMultiByte 成功", n == 4);
    ck("A9 往返一致", n == 4 && memcmp(narrow, "\xE4\xB8\xAD", 3) == 0);

    /* NLS 比较：CSTR_LESS_THAN == 1 */
    r = CompareStringA(0x0409, 0, "a", -1, "b", -1);
    ck("A10 CompareStringA 返回 CSTR_LESS_THAN", r == CSTR_LESS_THAN);
    r = CompareStringA(0x0409, 0, "b", -1, "a", -1);
    ck("A11 CompareStringA 反向返回 CSTR_GREATER_THAN", r == CSTR_GREATER_THAN);

    ck("A12 IsDBCSLeadByte 可取地址", (int)IsDBCSLeadByte != 0);
    ck("A13 GetSystemDefaultLCID 非零", GetSystemDefaultLCID() != 0);
    ck("A14 LCMapStringEx 可取地址", (int)LCMapStringEx != 0);
    ck("A15 CompareStringOrdinal 可取地址", (int)CompareStringOrdinal != 0);

    {
        WORD types[4];
        memset(types, 0, sizeof types);
        r = (int)GetStringTypeW(CT_CTYPE1, L"a", 1, types);
        ck("A16 GetStringTypeW 成功", r != 0);
        /* 'a' 应同时是小写字母与字母 */
        ck("A17 'a' 是 C1_LOWER|C1_ALPHA",
           (types[0] & C1_LOWER) != 0 && (types[0] & C1_ALPHA) != 0);
    }
}

/* ------------------------------------------------------------------ */
/* B. Vista / 7 时代的 API —— 2009 年头文件集没有，靠垫片              */
/* ------------------------------------------------------------------ */
static unsigned long __stdcall tpool_cb(void *a, void *b)
{
    (void)a;
    (void)b;
    return 0;
}

static void test_modern_api(void)
{
    ULONGLONG tick;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    INIT_ONCE once;
    SYNCHRONIZATION_BARRIER barrier;
    DWORD ncpu;
    TOKEN_ELEVATION elev;
    DWORD need;
    HANDLE tok;
    int r;

    /* GetTickCount64 返回自开机以来的毫秒数，应为非零 */
    tick = GetTickCount64();
    ck("B1 GetTickCount64 非零", tick != 0);

    r = (int)InitializeCriticalSectionEx(&cs, 0, 0);
    ck("B2 InitializeCriticalSectionEx 成功", r != 0);
    if (r) {
        EnterCriticalSection(&cs);
        LeaveCriticalSection(&cs);
        DeleteCriticalSection(&cs);
        ck("B3 临界区可进出", 1);
    } else {
        ck("B3 临界区可进出", 0);
    }

    /* CONDITION_VARIABLE：初始化 + 用超时唤醒避免挂死 */
    InitializeConditionVariable(&cv);
    InitializeCriticalSection(&cs);
    EnterCriticalSection(&cs);
    r = (int)SleepConditionVariableCS(&cv, &cs, 1);   /* 1ms 超时 */
    LeaveCriticalSection(&cs);
    DeleteCriticalSection(&cs);
    /* 超时返回 0 且 GetLastError == ERROR_TIMEOUT */
    ck("B4 SleepConditionVariableCS 超时行为正确",
       r == 0 && GetLastError() == ERROR_TIMEOUT);

    memset(&once, 0, sizeof once);
    memset(&barrier, 0, sizeof barrier);
    /* INIT_ONCE 只含一个 PVOID（32 位 4 字节 / 64 位 8 字节），
 * 原来断言 >= 8 只在 64 位成立 —— 32 位下会误报失败。
 * 改成"至少能装下一个指针"。 */
ck("B5 INIT_ONCE 尺寸合理", sizeof(INIT_ONCE) >= sizeof(void *));
    ck("B6 SYNCHRONIZATION_BARRIER 可取地址", (int)InitializeSynchronizationBarrier != 0);

    ncpu = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    ck("B7 GetActiveProcessorCount > 0", ncpu > 0);
    ck("B8 GetActiveProcessorCount == 本机 CPU 数", ncpu == (DWORD)8);

    ck("B9 CreateThreadpoolWork 可取地址", (int)CreateThreadpoolWork != 0);
    ck("B10 Correlated 回调签名可编译", (int)tpool_cb != 0);

    /* TOKEN_ELEVATION：需要打开自己的进程 token（advapi32） */
    tok = NULL;
    r = (int)OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
    ck("B11 OpenProcessToken 成功（需 -ladvapi32）", r != 0 && tok != NULL);
    if (r && tok) {
        need = 0;
        memset(&elev, 0, sizeof elev);
        r = (int)GetTokenInformation(tok, TokenElevation, &elev, sizeof elev, &need);
        ck("B12 GetTokenInformation(TokenElevation) 成功", r != 0);
        /* 值只可能是 0 或 1；进程正常运行时通常非提权 */
        ck("B13 TokenIsElevated 是 0 或 1", elev.TokenIsElevated == 0 || elev.TokenIsElevated == 1);
        CloseHandle(tok);
    } else {
        ck("B12 GetTokenInformation(TokenElevation) 成功", 0);
        ck("B13 TokenIsElevated 是 0 或 1", 0);
    }

    /* 文件信息类：取本进程 exe 的名字长度 */
    {
        char path[MAX_PATH];
        DWORD got = GetModuleFileNameA(NULL, path, MAX_PATH);
        ck("B14 GetModuleFileNameA 成功", got > 0 && got < MAX_PATH);
        if (got > 0) {
            HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, 0, NULL);
            ck("B15 能打开自身 exe", h != INVALID_HANDLE_VALUE);
            if (h != INVALID_HANDLE_VALUE) {
                /* FileStandardInfo 需要 FILE_STANDARD_INFO（垫片提供） */
                FILE_STANDARD_INFO fsi;
                memset(&fsi, 0, sizeof fsi);
                r = (int)GetFileInformationByHandleEx(h, FileStandardInfo, &fsi, sizeof fsi);
                ck("B16 GetFileInformationByHandleEx 成功", r != 0);
                ck("B17 自身 exe 大小 > 0", r != 0 && fsi.EndOfFile.QuadPart > 0);
                CloseHandle(h);
            } else {
                ck("B16 GetFileInformationByHandleEx 成功", 0);
                ck("B17 自身 exe 大小 > 0", 0);
            }
        } else {
            ck("B15 能打开自身 exe", 0);
            ck("B16 GetFileInformationByHandleEx 成功", 0);
            ck("B17 自身 exe 大小 > 0", 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* C. CRT 缺口 —— msvcrt 是 C89 时代                                   */
/* ------------------------------------------------------------------ */
static void test_crt(void)
{
    void *p;
    char err[128];
    size_t n;
    char *end = NULL;
    long long v;
    unsigned int cw;

    p = _aligned_malloc(256, 64);
    ck("C1 _aligned_malloc 成功", p != NULL);
    if (p) {
        ck("C2 返回地址 64 字节对齐", ((size_t)p % 64) == 0);
        memset(p, 0xAB, 256);
        ck("C3 可用且可写", ((unsigned char *)p)[255] == 0xAB);
        _aligned_free(p);
    } else {
        ck("C2 返回地址 64 字节对齐", 0);
        ck("C3 可用且可写", 0);
    }

    memset(err, 0, sizeof err);
    strerror_s(err, sizeof err, 2);
    ck("C4 strerror_s 写入非空消息", err[0] != '\0');

    n = strnlen("abc", 10);
    ck("C5 strnlen 返回实际长度 3", n == 3);
    n = strnlen("abcdef", 3);
    ck("C6 strnlen 受 maxlen 限制", n == 3);

    v = strtoll("-1234567890123", &end, 10);
    ck("C7 strtoll 解析正确", v == -1234567890123LL);
    ck("C8 strtoll 的 endptr 指向末尾", end != NULL && *end == '\0');

    cw = _controlfp(0, 0);
    ck("C9 _controlfp 可查询（返回非零控制字）", cw != 0);

    ck("C10 _statusfp 可调用", _statusfp() == 0 || _statusfp() != 0);

    /* _set_se_translator 是**故意不声明**的：msvcrt.dll 里没有导出，
     * 且它依赖 SEH 翻译机制（TCC 没有）。这里断言"确实拿不到"，
     * 这样如果哪天有人误加了声明，自检会提醒去核实它是否真能链接。 */
    ck("C11 _set_se_translator 未被声明（有意为之）",
#ifdef _set_se_translator
       0
#else
       1
#endif
      );
}

/* ------------------------------------------------------------------ */
/* D. TCC 自身缺陷 —— float 数学与 C99 比较宏                          */
/* ------------------------------------------------------------------ */
static void test_tcc_gaps(void)
{
    double nan_val;
    float f;

    /* tccmath.h 补的 4 个 float 函数 */
    f = fabsf(-3.5f);
    ck("D1 fabsf 正确", f == 3.5f);
    f = truncf(2.9f);
    ck("D2 truncf 正确", f == 2.0f);
    f = truncf(-2.9f);
    ck("D3 truncf 负数向零取整", f == -2.0f);
    f = (float)cbrt(27.0);
    ck("D4 cbrt 正确", f > 2.99f && f < 3.01f);
    f = nearbyintf(2.5f);
    ck("D5 nearbyintf 可调用", f == 2.0f || f == 3.0f);   /* 取决于舍入模式 */

    /* 未被 tccmath.h 覆盖的也应可用（TCC 自带） */
    f = sqrtf(16.0f);
    ck("D6 sqrtf 可用（TCC 自带）", f == 4.0f);
    f = powf(2.0f, 10.0f);
    ck("D7 powf 可用（TCC 自带）", f == 1024.0f);

    /* C99 比较宏 */
    ck("D8 isgreater(2,1)", isgreater(2.0, 1.0) != 0);
    ck("D9 !isgreater(1,2)", isgreater(1.0, 2.0) == 0);
    ck("D10 isgreaterequal(2,2)", isgreaterequal(2.0, 2.0) != 0);
    ck("D11 isless(1,2)", isless(1.0, 2.0) != 0);
    ck("D12 islessequal(2,2)", islessequal(2.0, 2.0) != 0);
    ck("D13 islessgreater(1,2)", islessgreater(1.0, 2.0) != 0);
    ck("D14 !islessgreater(2,2)", islessgreater(2.0, 2.0) == 0);

    /* NaN 语义：C99 7.12.14 —— 除 isunordered 外，NaN 参与的比较全为 false */
    nan_val = 0.0 / 0.0;
    ck("D15 isunordered(nan,1)", isunordered(nan_val, 1.0) != 0);
    ck("D16 !isunordered(1,2)", isunordered(1.0, 2.0) == 0);
    ck("D17 !isgreater(nan,1)", isgreater(nan_val, 1.0) == 0);
    ck("D18 !isless(nan,1)", isless(nan_val, 1.0) == 0);
    ck("D19 !isgreaterequal(nan,1)", isgreaterequal(nan_val, 1.0) == 0);
    ck("D20 !islessequal(nan,1)", islessequal(nan_val, 1.0) == 0);
    ck("D21 !islessgreater(nan,nan)", islessgreater(nan_val, nan_val) == 0);
}

int main(void)
{
    SetConsoleOutputCP(CP_UTF8);

    printf("=== probe_winapi: winapi-full 头文件集与新补能力 ===\n");
    test_codepage();
    test_modern_api();
    test_crt();
    test_tcc_gaps();

    printf("=== %d 通过, %d 失败 ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
