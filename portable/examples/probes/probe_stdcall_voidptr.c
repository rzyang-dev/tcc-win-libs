/* probe_stdcall_voidptr.c —— TCC 32 位缺陷：`__stdcall` 函数指针经
 * `void (*)(void)` 槽位中转后调用会崩
 *
 * ============================================================
 * 这个缺陷是什么
 * ============================================================
 *
 * SQLite 的 Windows 层用一张函数指针表访问所有 API（sqlite3.c L49294）：
 *
 *     typedef void (*sqlite3_syscall_ptr)(void);         // 无参、cdecl
 *     static struct win_syscall {
 *       const char *zName;
 *       sqlite3_syscall_ptr pCurrent;                    // <- void(*)(void) 槽位
 *       sqlite3_syscall_ptr pDefault;
 *     } aSyscall[] = {
 *       { "GetVersionExA", (SYSCALL)GetVersionExA, 0 },  // <- __stdcall 函数存进去
 *       ...
 *     };
 *     #define osGetVersionExA ((BOOL(WINAPI*)(LPOSVERSIONINFOA))aSyscall[34].pCurrent)
 *                                                       // <- 取出时强转回真实签名
 *
 * 即：**把 `__stdcall` 函数指针存进 `void(*)(void)` 槽位，取出时再强转成
 * 真实签名调用。**
 *
 * **这个模式在 TCC 0.9.27 的 32 位模式下会崩**，64 位正常。
 *
 * **精确边界**（实测，见第 2b 组断言）：
 *
 *   | 模式                                    | 32 位 | 64 位 |
 *   |-----------------------------------------|-------|-------|
 *   | `__cdecl` 经 `void(*)(void)` 槽位（无参/两参）| 正确 | 正确 |
 *   | **`__stdcall` 经同一模式**               | **崩** | 正确 |
 *
 * 所以中招的是**调用约定转换**，不是"函数指针经槽位"本身。
 * 这缩小了风险面：只有把 `__stdcall`（Win32 API 的默认约定）函数指针
 * 塞进 `void(*)(void)` 或其它 `__cdecl` 槽位时才会触发。

 * 崩溃码不止一种（观测到 0xC00000FD 栈溢出与 0xC0000005 访问违例），
 * 64 位正常。
 *
 * ============================================================
 * 实测证据
 * ============================================================
 *
 * | 操作                        | TCC 32 位        | TCC 64 位 |
 * |-----------------------------|------------------|-----------|
 * | 直接调用 __stdcall 函数      | 正确             | 正确      |
 * | 经 void(*)() 槽位中转后调用  | **崩（栈溢出）** | 正确      |
 *
 * ⚠️ **曾以为 32 位下取到的是 thunk —— 直接读机器码后推翻了。**
 *
 * 当时只看到"相邻函数地址差 17 字节"就推断是 thunk。实际 dump 起始字节：
 *
 *     32 位:  s_0 @00401011 : 55 89 E5 81 EC 00 00 00   (__stdcall)
 *             c_0 @00401038 : 55 89 E5 81 EC 00 00 00   (__cdecl)
 *
 * `55` = `push ebp` —— **两者都是正常函数序言，没有 thunk**。
 * 17 字节只是这些极简函数体本身的长度。
 *
 * 所以触发条件与 thunk 无关，而是**栈清理约定不一致**：32 位下 `__stdcall`
 * 由被调方清栈、`__cdecl` 由调用方清栈，把前者经后者槽位调用，
 * TCC 生成的两侧代码在栈清理上对不上。
 * **（具体是哪一侧的代码生成错了，未查明。）**
 *
 * 这解释了 SQLite 的 80 条 "pointer/integer" 警告 —— 它们全部出现在
 * 那些 `#define osXxx ((...WINAPI*)aSyscall[n].pCurrent)` 宏的使用处。
 *
 * 与 SQLite 崩溃的对应关系（见 probe_trace32 的追踪）：
 * 崩溃点在 `sqlite3_win32_is_nt()`（sqlite3.c L50188），
 * 它正文里第一件事就是 `osInterlockedCompareExchange(...)`。
 *
 * ============================================================
 * 这个探针的期望结果
 * ============================================================
 *
 *   - **64 位**：全部通过（`__stdcall` 经槽位中转可用）
 *   - **32 位**：崩溃。所以本探针**只在 64 位下注册进自检**。
 *     32 位运行时崩溃是**预期行为**，是缺陷本身，不是探针写错。
 *
 * 用法：
 *   scripts\tcc.cmd examples\probes\probe_stdcall_voidptr.c -Run          (64 位，应通过)
 *   scripts\tcc.cmd examples\probes\probe_stdcall_voidptr.c -Run -32      (32 位，预期崩溃)
 */

#include "tccwin.h"

#include <stdio.h>

static int pass = 0, fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) { ++pass; printf("  [OK]   %s\n", what); }
    else    { ++fail; printf("  [FAIL] %s\n", what); }
}

/* ---- 模拟 SQLite 的表：void(*)(void) 槽位 ---- */
typedef void (*SYSCALL)(void);

static DWORD  WINAPI f_std_0(void)              { return 0xAAAAu; }
static DWORD  WINAPI f_std_2(DWORD a, DWORD b)  { return a + b + 0x2222u; }
static DWORD  __cdecl f_cdecl_2(DWORD a, DWORD b) { return a + b + 0x3333u; }
static DWORD  __cdecl f_cdecl_0(void)            { return 0xC0DEu; }

static struct { const char *zName; SYSCALL pCurrent; } tab[4];

int main(void)
{
    int bits = (int)(sizeof(void *) * 8);

    SetConsoleOutputCP(CP_UTF8);
    printf("=== probe_stdcall_voidptr: __stdcall 经 void(*)() 槽位 ===\n");
    printf("    %d 位构建\n", bits);

    printf("\n-- 1. 直接调用（不经槽位）--\n");
    ck("f_std_0() == 0xAAAA", f_std_0() == 0xAAAAu);
    ck("f_std_2(1,2) == 0x2225", f_std_2(1, 2) == 0x2225u);
    ck("f_cdecl_2(1,2) == 0x3336", f_cdecl_2(1, 2) == 0x3336u);

    printf("\n-- 2. 填表（强转进 void(*)(void) 槽位，与 SQLite 相同）--\n");
    tab[0].zName = "f_std_0";   tab[0].pCurrent = (SYSCALL)f_std_0;
    tab[1].zName = "f_std_2";   tab[1].pCurrent = (SYSCALL)f_std_2;
    tab[2].zName = "f_cdecl_2"; tab[2].pCurrent = (SYSCALL)f_cdecl_2;
    tab[3].zName = "f_cdecl_0"; tab[3].pCurrent = (SYSCALL)f_cdecl_0;
    ck("槽位里存了非空函数指针",
       tab[0].pCurrent && tab[1].pCurrent && tab[2].pCurrent);
    printf("        tab[0]=%p tab[1]=%p tab[2]=%p\n",
           tab[0].pCurrent, tab[1].pCurrent, tab[2].pCurrent);

    printf("\n-- 2b. 边界：__cdecl 经同一槽位是安全的 --\n");
    /* 实测（TCC 32 位与 64 位）：
     *   __cdecl  经 void(*)(void) 槽位再转回真实签名 —— 两种位宽都正确
     *   __stdcall 经同一模式               —— 32 位崩，64 位正确
     * 所以中招的是**调用约定转换**，不是"函数指针经槽位"本身。 */
    {
        DWORD rc0 = ((DWORD(__cdecl *)(void))tab[3].pCurrent)();
        ck("经槽位调 f_cdecl_0() == 0xC0DE（__cdecl 安全）", rc0 == 0xC0DEu);
        printf("        得到 %08lX\n", (unsigned long)rc0);
    }
    {
        DWORD rc2 = ((DWORD(__cdecl *)(DWORD, DWORD))tab[2].pCurrent)(1, 2);
        ck("经槽位调 f_cdecl_2(1,2) == 0x3336（__cdecl 安全）", rc2 == 0x3336u);
        printf("        得到 %08lX\n", (unsigned long)rc2);
    }

    printf("\n-- 3. 按真实签名从槽位取出调用（SQLite 的 osXxx 模式）--\n");
    printf("        32 位下这一行会崩 —— 那正是本探针要证明的\n");
    {
        DWORD r0 = ((DWORD(WINAPI *)(void))tab[0].pCurrent)();
        ck("经槽位调 f_std_0() == 0xAAAA", r0 == 0xAAAAu);
        printf("        得到 %08lX\n", (unsigned long)r0);
    }
    {
        DWORD r1 = ((DWORD(WINAPI *)(DWORD, DWORD))tab[1].pCurrent)(1, 2);
        ck("经槽位调 f_std_2(1,2) == 0x2225", r1 == 0x2225u);
        printf("        得到 %08lX\n", (unsigned long)r1);
    }
    {
        DWORD r2 = ((DWORD(__cdecl *)(DWORD, DWORD))tab[2].pCurrent)(1, 2);
        ck("经槽位调 f_cdecl_2(1,2) == 0x3336", r2 == 0x3336u);
        printf("        得到 %08lX\n", (unsigned long)r2);
    }

    printf("\n-- 4. 位宽结论 --\n");
    if (bits == 64) {
        ck("64 位：__stdcall 经 void(*)() 槽位可用", fail == 0);
    } else {
        ck("32 位：本探针本不该跑到这里（预期在 -- 3. 崩溃）", 0);
    }

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
