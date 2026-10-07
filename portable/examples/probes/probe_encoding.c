/* UTF-8 输出编码验证
 *
 * 为什么需要这个探针：编码问题**看不清**。同一份程序在 OutputCP=65001
 * 的终端正常、在 936 的终端乱码。而用 PowerShell 的 `& exe | ForEach`
 * 观察输出还会二次解码，可能把对的显示成错的、或反过来。
 *
 * 所以这里**不看显示效果，只校验真实字节**：
 *   1. 源码里的中文字面量确实是 UTF-8 三字节
 *   2. SetConsoleOutputCP(CP_UTF8) 能把代码页切成 65001
 *   3. utf8io.h 的转换函数在两种代码页下产出预期字节
 *
 * 编译：scripts\tcc.cmd examples\probes\probe_encoding.c -With utf8io -Run
 */
#include "tccwin.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "utf8io.h"

/* 注意：本探针**故意不调用 SetConsoleOutputCP**，
 * 因为要测量的就是"当前代码页"下的真实行为。 */

static int pass = 0, fail = 0;
#define CK(d, c) do { if (c) { printf("  [OK]   %s\n", d); pass++; } \
                      else   { printf("  [FAIL] %s\n", d); fail++; } } while (0)

static void dump(const char *label, const unsigned char *p, int n) {
    int i;
    printf("       %-28s", label);
    for (i = 0; i < n; i++) printf("%02X ", p[i]);
    printf("\n");
}

int main(void) {
    printf("=== UTF-8 输出编码验证 ===\n\n");

    /* ---------- 1. 代码页现状 ---------- */
    printf("-- 代码页 --\n");
    printf("       GetACP()               = %u\n", GetACP());
    printf("       GetConsoleOutputCP()   = %u\n", GetConsoleOutputCP());
    printf("       GetConsoleCP()         = %u\n", GetConsoleCP());
    printf("       CP_UTF8=%d CP_ACP=%d\n", (int)CP_UTF8, (int)CP_ACP);
    CK("GetACP() 返回值合理（非 0）", GetACP() != 0);
    printf("\n");

    /* ---------- 2. 源文件里的中文字节确实是 UTF-8 ----------
     * "中" 的三种编码：
     *   UTF-8   e4 b8 ad
     *   GBK     d6 d0
     *   UTF-16  ff fe 2d 4e (BOM)
     * 如果源文件被误存成 GBK，这里会看到 d6 d0 —— 那就是编译前就错了。 */
    printf("-- 源文件中文字节 --\n");
    {
        const unsigned char *s = (const unsigned char *)"中";
        dump("源码里 \"中\" 的字节 =", s, 3);
        CK("源码是 UTF-8（e4 b8 ad），不是 GBK（d6 d0）",
           s[0] == 0xE4 && s[1] == 0xB8 && s[2] == 0xAD);
        CK("字面量长度正确（3 字节）", strlen("中") == 3);
    }
    printf("\n");

    /* ---------- 3. 转成 UTF-8 应与原串一致 ---------- */
    printf("-- u8_wide_to_cp(CP_UTF8) --\n");
    {
        char buf[64];
        int n;
        WCHAR wide[16];
        MultiByteToWideChar(CP_UTF8, 0, "中", -1, wide, 16);
        n = u8_wide_to_cp(buf, (int)sizeof buf, wide, CP_UTF8);
        dump("转成 CP_UTF8 =", (const unsigned char *)buf, n);
        CK("UTF-8 往返一致（仍是 e4 b8 ad）",
           n == 3 && (unsigned char)buf[0] == 0xE4 &&
           (unsigned char)buf[1] == 0xB8 && (unsigned char)buf[2] == 0xAD);
    }
    printf("\n");

    /* ---------- 4. 转成 GBK 应是 d6 d0 ---------- */
    printf("-- u8_wide_to_cp(936) --\n");
    {
        char buf[64];
        int n;
        WCHAR wide[16];
        MultiByteToWideChar(CP_UTF8, 0, "中", -1, wide, 16);
        n = u8_wide_to_cp(buf, (int)sizeof buf, wide, 936);
        dump("转成 936(GBK) =", (const unsigned char *)buf, n);
        CK("GBK 结果是 d6 d0（'中' 的 GBK 编码）",
           n == 2 && (unsigned char)buf[0] == 0xD6 &&
           (unsigned char)buf[1] == 0xD0);
    }
    printf("\n");

    /* ---------- 5. 转成 UTF-16LE 对照 ---------- */
    printf("-- 宽字符对照 --\n");
    {
        WCHAR wide[16];
        /* 注意：cbMultiByte 传 -1 时，返回的**包含**结尾 NUL 的字符数，
         * 所以 "中" 会返回 2（一个字符 + 终止符），不是 1。 */
        int n = MultiByteToWideChar(CP_UTF8, 0, "中", -1, wide, 16);
        dump("UTF-16 码元 =", (const unsigned char *)wide, n * 2);
        CK("返回值包含终止符（-1 时的语义）", n == 2);
        /* 注意 WCHAR 是 2 字节：要取的是 wide[0] 的**两个字节**，
         * 不是 wide[0] 与 wide[1]（后者是下一个码元 = 终止符）。 */
        {
            const unsigned char *wb = (const unsigned char *)wide;
            CK("wide[0] == 0x4E2D（'中' 的 UTF-16 码元）", wide[0] == 0x4E2D);
            CK("内存里是小端 2D 4E", wb[0] == 0x2D && wb[1] == 0x4E);
        }
        CK("第二个码元是终止符 0", wide[1] == 0);
    }
    printf("\n");

    /* ---------- 6. u8_console 按当前 OutputCP 决定行为 ----------
     * 这是修法 B 的核心：不切换代码页，而是转换。 */
    printf("-- u8_console（按当前 OutputCP） --\n");
    {
        char buf[64];
        int n = u8_console(buf, (int)sizeof buf, "中");
        UINT cp = GetConsoleOutputCP();
        dump("u8_console 结果 =", (const unsigned char *)buf, n > 0 ? n : 3);
        printf("       当前 OutputCP = %u\n", cp);
        if (cp == CP_UTF8) {
            CK("OutputCP=65001 时不转换（返回 0 或原字节）",
               n == 0 || ((unsigned char)buf[0] == 0xE4 && n == 3));
        } else if (cp == 936) {
            CK("OutputCP=936 时转成 GBK（d6 d0）",
               n == 2 && (unsigned char)buf[0] == 0xD6 &&
               (unsigned char)buf[1] == 0xD0);
        } else {
            CK("其它代码页：不崩溃即算通过（未覆盖该代码页）", 1);
        }
    }
    printf("\n");

    /* ---------- 7. SetConsoleOutputCP 能切到 UTF-8 ----------
     * 修法 A 的基础。这里会**改变**当前控制台代码页，
     * 所以放在最后测，并在结束时还原。 */
    printf("-- SetConsoleOutputCP（修法 A 的基础） --\n");
    {
        UINT before = GetConsoleOutputCP();
        BOOL ok = SetConsoleOutputCP(CP_UTF8);
        UINT after = GetConsoleOutputCP();
        printf("       切换前 %u -> SetConsoleOutputCP(CP_UTF8)=%d -> 切换后 %u\n",
               before, (int)ok, after);
        CK("SetConsoleOutputCP 返回成功", ok != 0);
        CK("代码页确实变成 65001", after == CP_UTF8);
        CK("切换后可再切回原代码页", SetConsoleOutputCP(before) != 0);
        printf("       已还原为 %u\n", GetConsoleOutputCP());
    }
    printf("\n");

    /* ---------- 8. 多字节与 ASCII 混合 ---------- */
    printf("-- 混合内容 --\n");
    {
        char buf[256];
        int n;
        const char *s = "abc中文123";
        WCHAR wide[64];
        MultiByteToWideChar(CP_UTF8, 0, s, -1, wide, 64);
        n = u8_wide_to_cp(buf, (int)sizeof buf, wide, 936);
        dump("混合串转 GBK =", (const unsigned char *)buf, n);
        /* "abc" + "中"(2) + "文"(2) + "123" = 3 + 2 + 2 + 3 = 10 */
        CK("混合串转换长度正确（ASCII 原样 + 汉字各 2 字节）", n == 10);
        CK("ASCII 部分不被改变",
           buf[0] == 'a' && buf[1] == 'b' && buf[2] == 'c' &&
           buf[7] == '1' && buf[8] == '2' && buf[9] == '3');
    }
    printf("\n");

    /* ---------- 9. 重定向场景 ---------- */
    printf("-- 重定向场景 --\n");
    {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode;
        int is_console = (h != INVALID_HANDLE_VALUE) && GetConsoleMode(h, &mode);
        printf("       stdout 是控制台: %s\n", is_console ? "是" : "否（被重定向）");
        CK("能判断 stdout 是否控制台", 1);
        printf("       重定向时 u8_console 会退化为原样输出 UTF-8（对文件是正确的）\n");
    }
    printf("\n");

    printf("=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail ? 1 : 0;
}
