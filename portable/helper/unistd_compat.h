/* unistd_compat.h - 补 <unistd.h> 缺失的 useconds_t，并声明 usleep
 *
 * ============================================================
 * 为什么需要这个头
 * ============================================================
 *
 * TCC 的 `toolchain/tcc/include/unistd.h` 第 21-25 行：
 *
 *     #ifndef __NO_ISOCEXT
 *     #include <sys/types.h>        // 注释写着 "For useconds_t."
 *     int __cdecl __MINGW_NOTHROW usleep(useconds_t useconds);
 *     #endif
 *
 * **那个注释是错的。** 实测：
 *   - `sys/types.h` 只定义 time_t / ino_t / dev_t / pid_t / mode_t /
 *     off_t / off64_t，**没有 `useconds_t`**
 *   - 整个头文件树（1012 个头）里都**没有 `useconds_t` 的 typedef**
 *
 * 于是 `useconds_t` 被当成未知标识符，第 24 行解析失败：
 *
 *     unistd.h:24: error: ',' expected (got "useconds")
 *
 * **后果：任何 `#include <unistd.h>` 的移植代码连编译都过不了**
 * —— 不是链接失败，是**编译失败**。
 *
 * ============================================================
 * 用法
 * ============================================================
 *
 *     scripts\tcc.cmd app.c -With unistd
 *
 * `-With unistd` 会用 `-include` 在**用户源码之前**注入本头文件，
 * 于是 `<unistd.h>` 被包含时 `useconds_t` 已经存在，能正常解析；
 * 同时把 `helper/unistd_compat.c` 编进目标文件提供 `usleep` 实现。
 *
 * 手动等价写法：
 *
 *     tcc -include helper\unistd_compat.h app.c helper\unistd_compat.c ...
 *
 * 「未验证边界」见 unistd_compat.c 末尾（主要是 Sleep 精度约 1~15.6 ms）。
 */

#ifndef TCWIN_UNISTD_COMPAT_H
#define TCWIN_UNISTD_COMPAT_H

/* MSVC/mingw 的 useconds_t 是无符号 32 位 */
#ifndef _USEONDS_T_DEFINED
#define _USEONDS_T_DEFINED
typedef unsigned int useconds_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 实现在 helper/unistd_compat.c */
int usleep(useconds_t useconds);

#ifdef __cplusplus
}
#endif

#endif /* TCWIN_UNISTD_COMPAT_H */
