/* unistd_compat.c - <unistd.h> 的 usleep 实现
 *
 * 背景与"为什么 <unistd.h> 只要 include 就编译不过"见 unistd_compat.h。
 *
 * 本文件只放**实现**，声明在 unistd_compat.h（由 `-With unistd` 用
 * `-include` 注入）。分开是因为 `-include` 与"编进目标文件"是两条路径 ——
 * 如果放同一个文件，会得到 `'usleep' defined twice`（实测踩到）。
 */

#include "unistd_compat.h"
#include "tccwin.h"          /* 要 Sleep() */

int usleep(useconds_t useconds)
{
    /* 0：让出一次时间片即可，不睡 —— 与 POSIX 语义接近（usleep(0) 立即返回） */
    if (useconds == 0) {
        Sleep(0);
        return 0;
    }

    /* 微秒 -> 毫秒，**向上取整**。
     * 向下取整会让 usleep(1000)（=1ms）变成 Sleep(0) 提前返回，
     * 在"轮询等待"的代码里会退化成忙等。 */
    {
        DWORD ms = (DWORD)((useconds + 999u) / 1000u);
        if (ms == 0) ms = 1;
        Sleep(ms);
    }
    return 0;
}

/* ============================================================
 * 未验证边界
 * ============================================================
 *
 *   - 精度受 `Sleep()` 限制：Windows 的 Sleep 最小粒度约 1~15.6 ms
 *     （取决于系统定时器分辨率），所以 `usleep(500)` 实际会睡 1 ms 以上。
 *     **不用于精确延时。**
 *   - 返回值恒为 0，不模拟被信号打断（EINTR）的情形。
 *   - 只补了 `usleep`。`unistd.h` 里的 `ftruncate` 走
 *     `__CRT_INLINE` + `_chsize`，而 `_chsize` 在 msvcrt.dll 里
 *     **也没有导出**（实测）—— 这一条**没修**，用到 ftruncate 的代码
 *     仍会在链接期失败。见 PITFALLS P69。
 *   - 未测多线程下 usleep（Sleep 本身线程安全）。
 */
