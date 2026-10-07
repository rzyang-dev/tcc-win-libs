/* time_compat.c - 实现 TCC 声明了但链不上的 gettimeofday / ftime
 *
 * ============================================================
 * 缺口
 * ============================================================
 *
 * 实测（tcc -impdef %WINDIR%\System32\msvcrt.dll 查导出表）：
 *
 *     gettimeofday        无导出
 *     mingw_gettimeofday  无导出
 *     ftime / _ftime      无导出
 *     _ftime64            无导出
 *
 * 而头文件里**声明是有的**：
 *
 *   sys/time.h 第 42 行声明 mingw_gettimeofday，
 *   第 55-56 行声明 gettimeofday —— 两个都没实现。
 *
 *   sys/timeb.h 里 **ftime 根本没被声明** —— 只有 _ftime32 / _ftime64，
 *   而把 _ftime 映射到 _ftime32 的那行是**被注释掉的**：
 *
 *       L88:  //gr #define _ftime _ftime32
 *       L89:  #define _ftime32 _ftime
 *       L92:  #define _ftime _ftime64      <- 这一行是活的
 *
 *   于是 ftime(&tb) 走隐式声明（C89 按 int 返回），而真正被调用的
 *   _ftime64 也没有导出 —— 链接期失败。
 *
 * **后果**：include sys/time.h 本身能过，但调用 gettimeofday 的移植代码
 * 在**链接期**失败；ftime 更隐蔽，因为它走隐式声明，
 * 连编译警告都可能被忽略。
 *
 * ============================================================
 * 实现方式
 * ============================================================
 *
 * 都用 Win32 自己算，不依赖 msvcrt：
 *   GetSystemTimeAsFileTime 取 UTC，再转 Unix 纪元
 *   （1601 -> 1970 相差 11644473600 秒）
 *
 * 用法：scripts\tcc.cmd app.c -With time
 *
 * 「未验证边界」见文件末尾。
 */

#include "tccwin.h"

#include <sys/time.h>
#include <sys/timeb.h>
#include <time.h>

/* 1601-01-01 到 1970-01-01 之间的秒数（Windows FILETIME 纪元到 Unix 纪元） */
#define TCWIN_EPOCH_DIFF_SEC  11644473600ULL

/* 从 FILETIME 取出 Unix 秒与微秒 */
static void tcwin_split_filetime(ULONGLONG ft, long *sec, long *usec)
{
    ULONGLONG ticks;
    ULONGLONG secs;

    ticks = ft;
    ticks -= TCWIN_EPOCH_DIFF_SEC * 10000000ULL;    /* 转成自 1970 起的 100ns 数 */

    secs = ticks / 10000000ULL;
    *sec  = (long)secs;
    *usec = (long)((ticks % 10000000ULL) / 10ULL);   /* 100ns -> us */
}

/* ============================================================
 * gettimeofday
 * ============================================================
 *
 * 签名必须与 sys/time.h 的声明**逐字一致**：
 *
 *     int __cdecl gettimeofday(struct timeval *__restrict__,
 *                              void *__restrict__);
 *
 * 第二参数是 **void ***，不是 struct timezone *（尽管头文件的注释里
 * 管它叫 tzp）。写成 struct timezone * 会得到
 *
 *     incompatible types for redefinition of 'gettimeofday'
 *
 * （实测踩到）。POSIX/MSDN 的老写法在这里不能照抄，
 * **必须以这个头文件的实际声明为准**。
 *
 * void * 版本也没有 struct timezone 可用，所以直接忽略第二参数 ——
 * 这与头文件注释 "The timezone pointer arg is ignored" 一致。 */
int gettimeofday(struct timeval *tv, void *tzp)
{
    FILETIME ft;
    ULONGLONG v;

    (void)tzp;                  /* 明确忽略 */

    if (tv) {
        GetSystemTimeAsFileTime(&ft);
        v = ((ULONGLONG)ft.dwHighDateTime << 32) | (ULONGLONG)ft.dwLowDateTime;
        tcwin_split_filetime(v, &tv->tv_sec, &tv->tv_usec);
    }
    return 0;
}

/* mingw 的老名字，一些代码直接用这个。
 * 它声明的第二参数确实是 struct timezone *（sys/time.h:42），
 * 直接转给 gettimeofday 的 void * 即可。 */
int mingw_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    return gettimeofday(tv, (void *)tz);
}

/* ============================================================
 * _ftime64 —— **这个才是真正缺的**
 * ============================================================
 *
 * `sys/timeb.h:114-124` **已经提供了内联的 `ftime`**：
 *
 *     __CRT_INLINE void __cdecl ftime(struct timeb *_Tmb) {
 *       _ftime64((struct __timeb64 *)_Tmb);
 *     }
 *
 * 所以 ftime **不缺实现**，它只是个转发；真正缺导出的底层是 `_ftime64`
 * （第 98 行有声明，msvcrt.dll 无导出）。
 *
 * ⚠️ 因此**不要自己定义 `ftime`** —— 会得到
 *     redefinition of 'ftime'
 * （实测踩到）。只需提供 `_ftime64`。 */
void _ftime64(struct __timeb64 *t)
{
    FILETIME ft;
    ULONGLONG v;
    long sec = 0, usec = 0;
    TIME_ZONE_INFORMATION tzi;
    DWORD tzr;

    if (!t) return;

    GetSystemTimeAsFileTime(&ft);
    v = ((ULONGLONG)ft.dwHighDateTime << 32) | (ULONGLONG)ft.dwLowDateTime;
    tcwin_split_filetime(v, &sec, &usec);

    t->time    = (__time64_t)sec;
    t->millitm = (unsigned short)(usec / 1000);

    /* timezone 字段按 POSIX 语义是"本地与 UTC 相差的分钟数，西为正"。
     * Windows 的 Bias 定义正好是 UTC = 本地 + Bias，所以直接取 Bias。
     * 取不到就填 0。 */
    tzr = GetTimeZoneInformation(&tzi);
    if (tzr == TIME_ZONE_ID_INVALID) {
        t->timezone = 0;
        t->dstflag  = 0;
    } else {
        t->timezone = (short)tzi.Bias;
        t->dstflag  = (tzr == TIME_ZONE_ID_DAYLIGHT) ? 1 : 0;
    }
}

/* ============================================================
 * 未验证边界
 * ============================================================
 *
 *   - 只测过"能编译+链接+取到与 time() 一致的秒数"。**没有**与系统时间
 *     逐秒对照，也没有测跨时区/夏令时切换。
 *   - gettimeofday 的第二参数按 sys/time.h 的注释被**完全忽略**，
 *     所以依赖它拿时区的移植代码会拿到未修改的内存。
 *   - ftime 的 timezone 用 GetTimeZoneInformation().Bias，符号方向按
 *     POSIX 惯例（西为正）。**未在非 UTC+8 时区验证。**
 *   - _ftime32 **没实现**。
 *   - 精度受 GetSystemTimeAsFileTime 限制（通常约 15.6 ms，
 *     返回的 tv_usec 低位多为 0）。**不用于精确计时** ——
 *     要精确计时用 QueryPerformanceCounter。
 */
