/* sqlite3_tcc.c - 用 TCC 编译 SQLite amalgamation 的包装文件
 *
 * 不要直接编译 sqlite3.c，用本文件代替。
 *
 * ============================================================
 * 三个必须的处理，缺一不可
 * ============================================================
 *
 * 1. **stddef.h / stdint.h 必须最先包含。**
 *    sqlite3.c 在 Windows 上会包含 <windows.h>，而 TCC 的 headers 在
 *    处理这条链时会把 stddef.h 卷进来两次，第二次虽被 _STDDEF_H 守卫
 *    跳过，但 TCC 的解析器仍会报错：
 *        ';' expected (got "typedef")
 *    错误位置显示为 stddef.h:4（其实是因为 #line 指令指向那里，很难反推）。
 *    先显式包含 stddef.h + stdint.h 就完全避开。
 *
 * 2. **必须定义 SQLITE_DISABLE_INTRINSIC。**
 *    sqlite3.c 的 sqlite3Multiply128() 有三条路径
 *    （见 sqlite3.c 里 SQLITE_USE_UINT128 段落）：
 *
 *      #if defined(SQLITE_USE_UINT128)          <- 要求 __GNUC__ / __clang__
 *        __uint128_t 乘法
 *      #elif defined(_WIN64) && !defined(SQLITE_DISABLE_INTRINSIC)
 *        __umulh(a, b)                          <- MSVC 内建
 *      #else
 *        手工 32 位分段乘法（可移植）
 *      #endif
 *
 *    **TCC 既没有 __GNUC__ 也没有 __clang__**，所以第一条不成立；
 *    但它定义了 _WIN64，于是落到第二条用了 MSVC 内建的 __umulh ——
 *    TCC 同样没有它，链接期报：
 *        tcc: error: undefined symbol '__umulh'
 *
 *    定义 SQLITE_DISABLE_INTRINSIC 就走到可移植的手工实现。
 *    代价只是 64x64->128 乘法慢一点（SQLite 只在少数地方用）。
 *
 * 3. tccwin.h 提供 TCC 缺失的 Win32 API 声明
 *    （MultiByteToWideChar / WideCharToMultiByte、__faststorefence 等，
 *     见 docs/PITFALLS.md P52 / P53）。
 *
 * 实测：SQLite 3.53.4（sqlite3.c 9.5 MB）编译约 1.6 秒产出 2.2 MB 目标文件。
 */
#include <stddef.h>
#include <stdint.h>

/* ============================================================
 * ⚠️ 32 位构建：**不要用**，会崩
 * ============================================================
 *
 * 实测（详细证据见 docs/PITFALLS.md P75）：
 *
 *   | 编译器            | 位宽 | sqlite3_initialize() |
 *   |-------------------|------|----------------------|
 *   | TCC 0.9.27        |  64  | 正常，全套 CRUD 通过  |
 *   | **TCC 0.9.27**    |**32**| **访问违例 0xC0000005** |
 *   | MinGW gcc 6.3.0   |  32  | 正常，全套 CRUD 通过  |
 *
 * 判定依据：**同一个 sqlite3.c 源码，gcc 32 位正常而 TCC 32 位崩** ——
 * 所以这是 TCC 的 32 位缺陷，不是 SQLite 的问题，也不是本项目包装的问题。
 *
 * 已排除的原因（都实测过，见 P75）：
 *   - long long 的除法/取模/移位/无符号（32 位下全部正确）
 *   - 线程原语 CRITICAL_SECTION / Interlocked 家族 / TLS / GetLastError
 *   - _beginthreadex 建线程（32 位下正常，线程真跑起来了）
 *   - malloc 的 8 字节对齐（32 位下 20/20 都对齐且读写正确）
 *   - 栈被 OSVERSIONINFOA 写坏（金丝雀未被破坏）
 *
 * **根因已经查明**（P75 的「根因找到了」小节）：
 *
 *   把 `__stdcall` 函数指针存进 `void (*)(void)` 槽位，取出时再强转回
 *   真实签名调用 —— 这个模式在 TCC 32 位下会栈溢出（0xC00000FD），
 *   64 位正常。
 *
 * SQLite 的 Windows 层正是这么做的（sqlite3.c L49294 的 aSyscall[] 表）：
 *
 *     typedef void (*sqlite3_syscall_ptr)(void);
 *     } aSyscall[] = { { "GetVersionExA", (SYSCALL)GetVersionExA, 0 }, ... };
 *     #define osGetVersionExA ((BOOL(WINAPI*)(LPOSVERSIONINFOA))aSyscall[34].pCurrent)
 *
 * 崩溃点在 `sqlite3_win32_is_nt()`（sqlite3.c L50188），它正文第一件事
 * 就是 `osInterlockedCompareExchange(...)`。
 *
 * 最小复现：examples\probes\probe_stdcall_voidptr.c（**10 项断言**）
 *     64 位：10/10 通过      32 位：崩溃（就是缺陷本身）
 *
 * **精确边界**：`__cdecl` 经同一槽位是**安全**的（两种位宽都正确），
 * 只有 `__stdcall` 中招。所以是**调用约定转换**的问题。
 *
 * ⚠️ **曾以为 32 位下取到的是 thunk —— 直接读机器码后推翻了。**
 * 实际 dump 起始字节，`__stdcall` 与 `__cdecl` 都是 `55 89 E5`
 * （push ebp / mov ebp,esp）—— **都是正常函数序言，没有 thunk**。
 * 17 字节的地址间隔只是极简函数体本身的长度。
 *
 * 所以触发条件是**栈清理约定不一致**：32 位下 `__stdcall` 由被调方清栈、
 * `__cdecl` 由调用方清栈，把前者经后者槽位调用，TCC 生成的两侧代码
 * 在栈清理上对不上。**（具体哪一侧代码生成错了，未查明。）**
 *
 * 这解释了 SQLite 32 位编译时的 **80 条** pointer/integer 警告
 * （64 位下同一条命令 **0 条**警告）—— 它们全部出现在
 * `#define osXxx ((...WINAPI*)aSyscall[n].pCurrent)` 这类宏的使用处。
 *
 * **未找到绕过办法**：试过在 `#include sqlite3.c` 之后重定义那些 `os*`
 * 宏为直接调用 —— 无效，因为函数体在 `#include` 时就编译完了。
 *
 * 所以这里**故意让它编译失败**：一个会崩的二进制比一个编译错误危险得多。
 *
 * 替代做法：
 *   - 用 64 位（默认，不加 `-32`）—— 实测完全正常
 *   - 32 位确实需要时，用 MinGW gcc：`C:\MinGW\bin\gcc.exe -m32`
 *
 * 若你确定要绕过（例如想自己排查这个 TCC 缺陷），定义
 * `SQLITE_TCC_ALLOW_BROKEN_32BIT` 即可继续。 */
#if defined(__i386__) && !defined(SQLITE_TCC_ALLOW_BROKEN_32BIT)
#error "SQLite 用 TCC 编 32 位会在 sqlite3_initialize() 崩溃（见 PITFALLS P75）。请用 64 位（去掉 -32），或改用 MinGW gcc -m32。确实要绕过就定义 SQLITE_TCC_ALLOW_BROKEN_32BIT。"
#endif

/* 必须在 sqlite3.c 之前：让 sqlite3Multiply128 走可移植路径（见上） */
#ifndef SQLITE_DISABLE_INTRINSIC
#define SQLITE_DISABLE_INTRINSIC 1
#endif

#include "tccwin.h"
#include "sqlite3.c"