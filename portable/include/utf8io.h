/* utf8io.h - 把 UTF-8 文本正确输出到 Windows 控制台 / 文件
 *
 * ============================================================
 * 这个头解决什么问题
 * ============================================================
 *
 * 本环境的源码是 **UTF-8**（无 BOM）。但 Windows 中文系统的
 * **ANSI 代码页默认是 936（GBK）**，控制台的输出代码页也常常是 936。
 *
 * TCC 对字符串字面量**不做任何字符集转换** —— 它把源文件里的字节原样
 * 放进 .exe。TCC 也没有 `-fexec-charset` 之类的选项（实测 `--help` 里
 * 没有任何 charset 相关开关）。
 *
 * 于是 `printf("中文")` 把 UTF-8 的 `e4 b8 ad` 写到控制台，
 * 而 GBK 控制台会把它当成两个半角字符解释 —— 显示成乱码：
 *
 *     tcwinreadline 婕旂ず 鈥斺€?杈撳叆 help ...
 *
 * 同一份程序在 **OutputCP=65001** 的终端（例如默认的 Windows Terminal
 * 或 VSCode 终端）里显示正常，在 **OutputCP=936** 的传统 conhost 里乱码。
 * 这就是"我这儿好好的，你那儿全是乱码"的根源。
 *
 * ============================================================
 * 用法
 * ============================================================
 *
 *     #include "tccwin.h"
 *     #include "utf8io.h"
 *
 *     u8_puts("中文正常显示\n");
 *     u8_printf("结果: %d\n", 42);
 *
 * 也可以只做转换，自己决定往哪写：
 *
 *     char buf[256];
 *     if (u8_to_console(buf, sizeof buf, "中文")) fputs(buf, stdout);
 *
 * ============================================================
 * 行为
 * ============================================================
 *
 * u8_console() 把 UTF-8 转成**当前控制台输出代码页**：
 *
 *   OutputCP=65001 (UTF-8) → 原样返回（不做无谓的转换）
 *   OutputCP=936   (GBK)   → 转成 GBK，控制台正确显示
 *   其它代码页              → 转成对应代码页
 *
 * 拿不到控制台句柄时（输出被重定向到文件/管道）返回 0，
 * 此时 u8_puts 会退化为直接写 UTF-8 字节 —— 对重定向到文件是正确的，
 * 因为文件通常期望 UTF-8。
 *
 * ============================================================
 * 未验证边界
 * ============================================================
 *   - 只处理输出。**输入**（读取用户键入的中文）没有做转换 ——
 *     tcwinreadline 的行编辑目前按字节处理，多字节字符的删除/光标定位
 *     需要额外处理（见 docs/LIBRARIES.md 里 readline 的限制）
 *   - 目标代码页里没有的字符（例如 GBK 里没有的 emoji）会变成 `?`
 *   - 每次调用都会做一次转换，高频输出场景可以自己缓存
 */
#ifndef UTF8IO_H
#define UTF8IO_H

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 把一个宽字符串转换成指定代码页，写入 out（以 NUL 结尾）。
 * 返回写入的字节数（不含结尾 NUL），失败返回 -1。 */
static __inline int u8_wide_to_cp(char *out, int outsz,
                                  const WCHAR *wide, unsigned cp) {
    int n;
    if (!out || outsz <= 1 || !wide) return -1;
    n = WideCharToMultiByte(cp, 0, wide, -1, out, outsz, NULL, NULL);
    if (n <= 0) return -1;
    return n - 1;       /* 去掉结尾 NUL 的计数 */
}

/* 把 UTF-8 字符串转成控制台输出代码页，写入 out（以 NUL 结尾）。
 * 返回字节数；失败或无需转换时返回 0（此时调用方应直接使用原串）。 */
static __inline int u8_console(char *out, int outsz, const char *utf8) {
    UINT cp;
    int  wn;
    WCHAR wide[1024];
    HANDLE h;

    if (!out || outsz <= 1 || !utf8) return 0;

    /* 不是控制台（重定向到文件/管道）就原样输出 UTF-8 */
    h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return 0;

    cp = GetConsoleOutputCP();
    if (cp == 0 || cp == CP_UTF8) return 0;         /* 已经是 UTF-8，不用转 */
    if (cp == CP_ACP && GetACP() == CP_UTF8) return 0;

    wn = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, 1024);
    if (wn <= 0) return 0;                          /* 非法 UTF-8，原样输出 */

    return u8_wide_to_cp(out, outsz, wide, cp);
}

/* 输出一个 UTF-8 字符串到 stdout，自动适配控制台代码页。 */
static __inline void u8_puts(const char *utf8) {
    char buf[4096];
    int n;
    if (!utf8) return;
    n = u8_console(buf, (int)sizeof buf, utf8);
    if (n > 0) fwrite(buf, 1, (size_t)n, stdout);
    else       fputs(utf8, stdout);
}

/* 带格式化的版本。缓冲区不够长时退化为直接输出原串（不做截断转换）。 */
static __inline void u8_printf(const char *fmt, ...) {
    char tmp[2048];
    char out[4096];
    int n;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    n = u8_console(out, (int)sizeof out, tmp);
    if (n > 0) fwrite(out, 1, (size_t)n, stdout);
    else       fputs(tmp, stdout);
}

#ifdef __cplusplus
}
#endif
#endif /* UTF8IO_H */
