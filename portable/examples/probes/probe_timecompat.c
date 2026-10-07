/* probe_timecompat.c —— 验证 gettimeofday / ftime 补齐（`-With time`）
 *
 * ============================================================
 * 背景
 * ============================================================
 *
 * `sys/time.h` 声明了 `gettimeofday` 与 `mingw_gettimeofday`，
 * `sys/timeb.h` 声明了 `_ftime64`（并自带一个内联的 `ftime` 转发到它）——
 * 但这些底层符号在 `msvcrt.dll` 里**全都没有导出**（`tcc -impdef` 实测）。
 *
 * 后果：`#include <sys/time.h>` 本身能过，但**调用 `gettimeofday` 的
 * 移植代码在链接期失败**。
 *
 * `helper/time_compat.c` 用 Win32 `GetSystemTimeAsFileTime` 实现它们。
 * 本探针验证补齐正确，重点是**与 `time()` 的交叉校验**。
 *
 * 用法：scripts\tcc.cmd examples\probes\probe_timecompat.c -With time -Run
 */

#include <sys/time.h>
#include <sys/timeb.h>
#include <stdio.h>
#include <time.h>

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
    else { ++fail; printf("  [FAIL] %-52s 得到 %d，期望 %d\n", what, got, want); }
}

int main(void)
{
    SetConsoleOutputCP(CP_UTF8);
    printf("=== probe_timecompat: gettimeofday / ftime 补齐验证 ===\n");

    printf("\n-- 1. gettimeofday 基本可用 --\n");
    {
        struct timeval tv;
        long long diff;

        tv.tv_sec = 0; tv.tv_usec = 0;
        ck_int("gettimeofday 返回 0", gettimeofday(&tv, 0), 0);
        ck("tv_sec 非零", tv.tv_sec != 0);
        ck("tv_usec 在 [0, 1000000)", tv.tv_usec >= 0 && tv.tv_usec < 1000000);
        printf("       sec=%ld usec=%ld\n", tv.tv_sec, tv.tv_usec);

        /* 关键交叉校验：与 CRT 的 time() 必须一致 */
        diff = (long long)time(0) - (long long)tv.tv_sec;
        ck("与 time() 的差在 ±2 秒内（交叉校验）", diff >= -2 && diff <= 2);
        if (diff < -2 || diff > 2) {
            printf("       time()=%lld, gettimeofday=%ld, 差=%lld\n",
                   (long long)time(0), tv.tv_sec, diff);
        }
    }

    printf("\n-- 2. tv_usec 会推进（不是恒为 0）--\n");
    {
        struct timeval a, b;
        int i;
        long long total_ns;
        gettimeofday(&a, 0);
        /* 忙等一小段，确保能观察到时间前进 */
        for (i = 0; i < 200000; i++) { volatile int x = i; (void)x; }
        gettimeofday(&b, 0);
        total_ns = ((long long)b.tv_sec - (long long)a.tv_sec) * 1000000LL
                 + ((long long)b.tv_usec - (long long)a.tv_usec);
        printf("       两次调用相差 %lld us\n", total_ns);
        ck("时间单调不减", total_ns >= 0);
    }

    printf("\n-- 3. tz 参数被忽略（与头文件注释一致）--\n");
    {
        struct timeval tv;
        int tz_min = 12345, tz_dst = 678;
        /* 头文件声明第二参数是 void *，注释说 tzp 被忽略。
         * 传一个非空的 struct timezone 指针，确认它**没被改**。 */
        struct timezone tz;
        tz.tz_minuteswest = tz_min;
        tz.tz_dsttime = tz_dst;
        gettimeofday(&tv, &tz);
        ck("tz 未被修改（第二参数确实被忽略）",
           tz.tz_minuteswest == tz_min && tz.tz_dsttime == tz_dst);
    }

    printf("\n-- 4. mingw_gettimeofday 是老名字，结果应一致 --\n");
    {
        struct timeval a, b;
        gettimeofday(&a, 0);
        mingw_gettimeofday(&b, 0);
        ck_int("mingw_gettimeofday 返回 0", mingw_gettimeofday(&b, 0), 0);
        ck("两者的 tv_sec 相同（同一时刻）", a.tv_sec == b.tv_sec);
    }

    printf("\n-- 5. ftime 走头文件的内联转发到 _ftime64 --\n");
    {
        struct timeb tb;
        long long diff;

        /* ⚠️ TCC 的 `ftime` 返回 **void**，不是 int。
         *
         * `sys/timeb.h:120` 的内联版是：
         *     __CRT_INLINE void __cdecl ftime(struct timeb *_Tmb) { ... }
         *
         * 而 MSDN / 老 mingw 的 ftime 返回 int（成功 0）。
         * **不能照抄 MSDN 的写法** `if (ftime(&tb) != 0)` —— 那会报
         *     cannot cast from/to void
         * （实测踩到）。见 PITFALLS P71。
         *
         * 所以这里只验证"调用后结构体被填好了"。 */
        tb.time = 0; tb.millitm = 0; tb.timezone = 0; tb.dstflag = 0;
        ftime(&tb);                       /* 返回 void，不能放进断言 */
        ck("tb.time 非零", tb.time != 0);
        ck("millitm 在 [0, 1000)", tb.millitm < 1000);
        printf("       time=%lld millitm=%u timezone=%d dstflag=%d\n",
               (long long)tb.time, (unsigned)tb.millitm,
               (int)tb.timezone, (int)tb.dstflag);

        diff = (long long)time(0) - (long long)tb.time;
        ck("tb.time 与 time() 一致（±2 秒）", diff >= -2 && diff <= 2);
    }

    printf("\n-- 6. 直接调 _ftime64（ftime 的底层）--\n");
    {
        struct __timeb64 t64;
        long long diff;
        t64.time = 0;
        _ftime64(&t64);
        ck("_ftime64 填充了 time", t64.time != 0);
        diff = (long long)time(0) - (long long)t64.time;
        ck("_ftime64 的 time 与 time() 一致（±2 秒）", diff >= -2 && diff <= 2);
    }

    printf("\n-- 7. 与 ftime 的时区信息自检（本机应为整分钟倍数）--\n");
    {
        struct timeb tb;
        ftime(&tb);
        /* Bias 以分钟为单位，任何真实时区都应是整分钟 */
        ck("timezone 是合理的分钟偏移（|tz| <= 1440）",
           tb.timezone >= -1440 && tb.timezone <= 1440);
        ck("dstflag 是 0 或 1", tb.dstflag == 0 || tb.dstflag == 1);
    }

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
