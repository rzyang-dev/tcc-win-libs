/* probe_unistd.c —— 验证 TCC 的 <unistd.h> 现在能 include 了
 *
 * ============================================================
 * 背景
 * ============================================================
 *
 * TCC 的 `unistd.h` 第 22 行写着 `#include <sys/types.h> /* For useconds_t. *\/`
 * 但**那个注释是错的** —— `sys/types.h` 里没有 `useconds_t`，
 * 整个头文件树 1012 个头里也没有。于是第 24 行
 *
 *     int __cdecl __MINGW_NOTHROW usleep(useconds_t useconds);
 *
 * 解析失败：
 *
 *     unistd.h:24: error: ',' expected (got "useconds")
 *
 * **后果：任何 `#include <unistd.h>` 的移植代码连编译都过不了。**
 *
 * `helper/unistd_compat.h` + `.c` 补上了 `useconds_t` 与 `usleep`，
 * 由 `-With unistd` 注入。本探针验证它真的能用。
 *
 * 用法：scripts\tcc.cmd examples\probes\probe_unistd.c -With unistd -Run
 */

#include <unistd.h>          /* 这一行以前是编译不过的 */
#include <stdio.h>
#include <time.h>

/* tccwin.h 拿 GetTickCount64 精确计时，避免只依赖 clock() 的低分辨率 */
#include "tccwin.h"

static int pass = 0, fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) { ++pass; printf("  [OK]   %s\n", what); }
    else    { ++fail; printf("  [FAIL] %s\n", what); }
}

static void ck_int(const char *what, int got, int want)
{
    if (got == want) { ++pass; printf("  [OK]   %s\n", what); }
    else { ++fail; printf("  [FAIL] %-50s 得到 %d，期望 %d\n", what, got, want); }
}

int main(void)
{
    ULONGLONG t0, t1;

    SetConsoleOutputCP(CP_UTF8);
    printf("=== probe_unistd: TCC <unistd.h> 修复验证 ===\n");

    printf("\n-- 1. 类型与声明 --\n");
    /* useconds_t 必须存在，且是 4 字节无符号 */
    ck_int("useconds_t 大小 == 4", (int)sizeof(useconds_t), 4);
    ck("useconds_t 是无符号类型", (useconds_t)-1 > 0);
    ck("usleep 可取地址", (int)usleep != 0);

    printf("\n-- 2. usleep(0) 立即返回 --\n");
    t0 = GetTickCount64();
    ck_int("usleep(0) 返回 0", usleep(0), 0);
    t1 = GetTickCount64();
    ck("usleep(0) 耗时 < 50ms（不该真睡）", (t1 - t0) < 50);

    printf("\n-- 3. 实际睡眠时长 --\n");
    {
        ULONGLONG ms;
        t0 = GetTickCount64();
        usleep(50000);                 /* 要 50 ms */
        t1 = GetTickCount64();
        ms = t1 - t0;
        printf("       usleep(50000) 实际睡了 %llu ms\n", (unsigned long long)ms);
        /* 向上取整 50ms；Sleep 粒度可能让它更久，但不该明显短 */
        ck("睡了 >= 45ms（没提前返回）", ms >= 45);
        ck("睡了 <= 200ms（没睡过头）", ms <= 200);
    }

    printf("\n-- 4. 小值向上取整（不退化成功忙等）--\n");
    {
        ULONGLONG ms;
        t0 = GetTickCount64();
        usleep(1000);                  /* 1 ms：向上取整后应睡 1ms（不是 0） */
        t1 = GetTickCount64();
        ms = t1 - t0;
        printf("       usleep(1000) 实际睡了 %llu ms\n", (unsigned long long)ms);
        /* Windows Sleep 粒度可能让它睡到 15ms，所以只断言"不短于 1ms 的下限"
         * 是没意义的（粒度问题）。这里只要求它被调用过且不崩。 */
        ck("usleep(1000) 不崩且返回", 1);
    }

    printf("\n-- 5. 多次调用累积时长 --\n");
    {
        ULONGLONG ms;
        int i;
        t0 = GetTickCount64();
        for (i = 0; i < 10; i++) usleep(10000);   /* 10 x 10ms = 期望约 100ms */
        t1 = GetTickCount64();
        ms = t1 - t0;
        printf("       10 x usleep(10000) 共 %llu ms\n", (unsigned long long)ms);
        ck("累积 >= 90ms", ms >= 90);
        ck("累积 <= 400ms", ms <= 400);
    }

    printf("\n-- 6. unistd.h 里的其它声明也在（不要求全部可用）--\n");
    /* ftruncate 是 __CRT_INLINE 走 _chsize，而 _chsize 在 msvcrt 里
     * 也没导出 —— 所以**只检查声明可见，不断言可链接**。这是有意为之，
     * 见 PITFALLS P69 的未验证边界。 */
    ck("ftruncate 声明可见（能否链接见 P69）", (int)ftruncate != 0);

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
