/* dirent_compat.c - 实现 TCC 的 <dirent.h> 里**声明了但链不上**的函数
 *
 * ============================================================
 * 为什么需要这个文件
 * ============================================================
 *
 * TCC 自带 `toolchain/tcc/include/dirent.h`，它**完整声明了 POSIX 目录接口**：
 *
 *     struct dirent { long d_ino; unsigned short d_reclen, d_namlen; char *d_name; };
 *     typedef struct { struct _finddata_t dd_dta; struct dirent dd_dir;
 *                      long dd_handle; int dd_stat; char dd_name[1]; } DIR;
 *     DIR* opendir(const char*);   struct dirent* readdir(DIR*);
 *     int  closedir(DIR*);         void rewinddir(DIR*);
 *     long telldir(DIR*);          void seekdir(DIR*, long);
 *
 * 但**这些符号 msvcrt.dll 一个都没有导出**（实测：用
 * `tcc -impdef %WINDIR%\System32\msvcrt.dll` 生成导出表后逐个查，
 * opendir / readdir / closedir / rewinddir / telldir / seekdir **全部无导出**）。
 *
 * 于是任何 `#include <dirent.h>` 的移植代码都在**链接期**失败：
 *
 *     undefined symbol 'opendir'
 *
 * 这是"头文件谎报能力"—— 比缺头文件更隐蔽，因为**编译能过**。
 *
 * ============================================================
 * 实现方式
 * ============================================================
 *
 * TCC 的 `DIR` 结构体本身就是**照着 Win32 的 `_findfirst` 族设计的**
 * （字段名 dd_dta / dd_handle / dd_stat 与 mingw 的实现一一对应），
 * 而 `_findfirst` / `_findnext` / `_findclose` 在 msvcrt.dll 里**有导出**
 * （经 TCC 的 msvcrt.def stub 可达）。
 *
 * 所以这里用那三个函数把 6 个窄字符函数补齐 —— 与 mingw 原始实现同构，
 * **不需要改 dirent.h**（"不改 TCC 头文件"是本项目的原则）。
 *
 * 用法：
 *     scripts\tcc.cmd app.c -With dirent       （tcc.ps1 已登记）
 *
 * ============================================================
 * ⚠️ 宽字符版（_wopendir 等 6 个）**故意不实现** —— 有硬障碍
 * ============================================================
 *
 * `dirent.h` 还声明了 `_wopendir` / `_wreaddir` / `_wclosedir` /
 * `_wrewinddir` / `_wtelldir` / `_wseekdir`。这 6 个**不做**，原因不是懒：
 *
 * 1. TCC 的 `_WDIR.dd_dta` 字段类型是 `struct _wfinddata_t`（32 位时间，
 *    约 50 字节）；而 msvcrt.dll **只导出 `_wfindfirst64` / `_wfindnext64`**，
 *    它们要求 `struct _wfinddata64_t`（64 位时间，约 560 字节）。
 *    类型与尺寸都对不上。
 *
 * 2. msvcrt.dll **没有** `_wfindfirst64i32` 的导出，而 TCC 的 io.h 恰好把
 *    `_wfindfirst` 宏定义到那个名字：
 *
 *        io.h:147   #define _wfindfirst _wfindfirst64i32
 *
 *    所以 `_wfindfirst(L"*", ...)` 链接期报
 *    `undefined symbol '_wfindfirst64i32'`（实测）。
 *
 * 3. 绕过它的唯一办法是**改 dirent.h 的 `_WDIR` 定义**（把 dd_dta 换成
 *    `_wfinddata64_t`）—— 那就违反"不改 TCC 头文件"的原则了。
 *
 * **结论：TCC 的宽字符目录接口在本环境下无法实现，属于真实死路。**
 * 用窄字符 `opendir`（本文件已提供），或改用 `tinydir`（本环境自带，
 * 走 Win32 `FindFirstFileW`，原生 Unicode）。
 *
 * 详见 docs/PITFALLS.md P68。
 *
 * ============================================================
 * 与原 mingw 实现的差异
 * ============================================================
 *
 *   - `d_ino` 恒为 0，`d_reclen` 恒为 0 —— 与原实现一致（Windows 没这两个概念）
 *   - `rewinddir()` 用"关掉再重新 _findfirst"实现，不缓冲
 *   - `seekdir()` 用"rewind + 连续 readdir 跳过"实现（O(n)，但语义正确）
 *   - 路径分隔符：末尾无分隔符时补 `\`；已有 `\` 或 `/` 则直接接 `*`
 *
 * ============================================================
 * 未验证边界
 * ============================================================
 *
 *   - 只测过当前目录与普通子目录。**超长路径（>4096）会返回 NULL**，
 *     未测 UNC 路径与 \\?\ 前缀路径。
 *   - `d_ino` / `d_reclen` 恒为 0，依赖它们的代码（如按 inode 去重）不可用。
 *   - `seekdir` 是 O(n)，大目录下慢。
 *   - `DIR` 不是线程安全的：同一 DIR 不能多线程同时 readdir
 *     （与原 mingw 实现一致；不同 DIR 互不影响）。
 *   - 未测目录在遍历过程中被删除/改名时的行为。
 */

/* 先让 <dirent.h> 拿到它的类型定义，再提供实现 */
#include <dirent.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 内部辅助
 * ============================================================ */

/* 把目录路径拼成 _findfirst 需要的通配模式："dir" -> "dir\*"
 *
 * 处理末尾已有分隔符的情况（"C:\" 不要变成 "C:\\*"）。
 * 返回 0 表示空间不足或路径过长。 */
static int dir_to_pattern(const char *dir, char *out, int cap)
{
    int n = (int)strlen(dir);
    int need;

    if (n == 0) return 0;

    need = n + 3;                 /* dir + '\' + '*' + NUL */
    if (need > cap) return 0;

    memcpy(out, dir, (size_t)n);
    if (dir[n - 1] == '\\' || dir[n - 1] == '/') {
        out[n] = '*';
        out[n + 1] = 0;
    } else {
        out[n] = '\\';
        out[n + 1] = '*';
        out[n + 2] = 0;
    }
    return 1;
}

/* ============================================================
 * 窄字符版：opendir / readdir / closedir / rewinddir / telldir / seekdir
 * ============================================================ */

DIR *opendir(const char *name)
{
    DIR *d;
    char pattern[4096];       /* 覆盖 MAX_PATH 及更长路径 */
    intptr_t h;
    int plen;

    if (!name) return NULL;
    if (!dir_to_pattern(name, pattern, (int)sizeof pattern)) return NULL;

    /* DIR 末尾是 dd_name[1]，实际需要多分配 plen 字节存完整模式
     * （rewinddir 要用它重开搜索）。 */
    plen = (int)strlen(pattern);
    d = (DIR *)malloc(sizeof(DIR) + (size_t)plen);
    if (!d) return NULL;
    memset(d, 0, sizeof(DIR) + (size_t)plen);

    memcpy(d->dd_name, pattern, (size_t)plen + 1);

    h = _findfirst(pattern, &d->dd_dta);
    if (h == -1) { free(d); return NULL; }

    d->dd_handle = (long)h;
    d->dd_stat   = 0;                 /* 0 = 下一个要返回的是第一条 */
    d->dd_dir.d_ino    = 0;
    d->dd_dir.d_reclen = 0;
    d->dd_dir.d_namlen = 0;
    d->dd_dir.d_name   = d->dd_dta.name;   /* 指向结构体内的名字缓冲 */
    return d;
}

struct dirent *readdir(DIR *d)
{
    if (!d) return NULL;
    if (d->dd_stat < 0) return NULL;      /* 已经到底 */

    if (d->dd_stat > 0) {
        /* 不是第一条，走 _findnext */
        if (_findnext((intptr_t)d->dd_handle, &d->dd_dta) != 0) {
            d->dd_stat = -1;
            return NULL;
        }
    }

    d->dd_stat++;
    d->dd_dir.d_namlen = (unsigned short)strlen(d->dd_dta.name);
    d->dd_dir.d_name   = d->dd_dta.name;
    return &d->dd_dir;
}

int closedir(DIR *d)
{
    if (!d) return -1;
    if (d->dd_handle != -1) _findclose((intptr_t)d->dd_handle);
    d->dd_handle = -1;
    free(d);
    return 0;
}

void rewinddir(DIR *d)
{
    intptr_t h;
    if (!d) return;
    if (d->dd_handle != -1) _findclose((intptr_t)d->dd_handle);
    d->dd_handle = -1;
    /* dd_name 里存的就是完整模式，直接重开 */
    h = _findfirst(d->dd_name, &d->dd_dta);
    if (h == -1) { d->dd_stat = -1; return; }
    d->dd_handle = (long)h;
    d->dd_stat   = 0;
}

long telldir(DIR *d)
{
    if (!d) return -1;
    return (long)d->dd_stat;
}

void seekdir(DIR *d, long loc)
{
    if (!d) return;
    if (loc < 0) return;
    /* 无随机定位能力，用 rewind + 连续推进实现。O(n) 但语义正确。 */
    rewinddir(d);
    if (d->dd_stat < 0) return;
    while (d->dd_stat < loc) {
        if (!readdir(d)) return;
    }
}
