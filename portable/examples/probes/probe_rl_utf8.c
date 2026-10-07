/* probe_rl_utf8.c —— tcwin_rl_utf8.h 的纯逻辑验证
 *
 * 背景：tcwinreadline 原来完全按字节处理输入，UTF-8 多字节字符会被
 * 退格删成半个、光标按字节数定位（与屏幕列数不符）。
 *
 * 这个探针只测新增的纯逻辑层（tcwin_rl_utf8.h）—— 不碰控制台、不碰 I/O,
 * 所以能在无人值守下断言每个函数的真实行为。
 *
 * 测试数据用真实的中日韩字符，覆盖 1/2/3/4 字节序列与代理对。
 */

#include "tcwin_rl_utf8.h"

#include <stdio.h>
#include <string.h>

static int pass = 0, fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) {
        ++pass;
    } else {
        ++fail;
        printf("  [FAIL] %s\n", what);
    }
}

static void ck_int(const char *what, int got, int want)
{
    if (got == want) {
        ++pass;
    } else {
        ++fail;
        printf("  [FAIL] %-46s 得到 %d，期望 %d\n", what, got, want);
    }
}

/* ---------------------------------------------------------------- */
/* 1. 序列长度判定                                                   */
/* ---------------------------------------------------------------- */
static void test_seqlen(void)
{
    ck_int("seqlen('A') == 1",            tcwin_u8_seqlen('A'), 1);
    ck_int("seqlen(0x7F) == 1",           tcwin_u8_seqlen(0x7F), 1);
    /* "中" 的 UTF-8 是 E4 B8 AD，首字节 E4 */
    ck_int("seqlen(0xE4) == 3  [中]",     tcwin_u8_seqlen(0xE4), 3);
    /* "é" 的 UTF-8 是 C3 A9，首字节 C3 */
    ck_int("seqlen(0xC3) == 2  [é]",      tcwin_u8_seqlen(0xC3), 2);
    /* U+1F600 😀 的 UTF-8 是 F0 9F 98 80 */
    ck_int("seqlen(0xF0) == 4  [😀]",     tcwin_u8_seqlen(0xF0), 4);
    /* 续字节不应被当首字节 */
    ck_int("seqlen(0x80) == 1  [续字节]", tcwin_u8_seqlen(0x80), 1);
    ck_int("seqlen(0xBF) == 1  [续字节]", tcwin_u8_seqlen(0xBF), 1);
    ck_int("seqlen(0xF8) == 1  [非法]",   tcwin_u8_seqlen(0xF8), 1);
    ck_int("seqlen(0xFF) == 1  [非法]",   tcwin_u8_seqlen(0xFF), 1);
}

/* ---------------------------------------------------------------- */
/* 2. 解码                                                           */
/* ---------------------------------------------------------------- */
static void test_decode(void)
{
    int adv;

    adv = -1;
    ck_int("decode('A') 码点", tcwin_u8_decode("A", &adv), 0x41);
    ck_int("decode('A') 前进 1", adv, 1);

    adv = -1;
    ck_int("decode(\"中\") 码点 == U+4E2D", tcwin_u8_decode("\xE4\xB8\xAD", &adv), 0x4E2D);
    ck_int("decode(\"中\") 前进 3", adv, 3);

    adv = -1;
    ck_int("decode(\"é\") 码点 == U+00E9", tcwin_u8_decode("\xC3\xA9", &adv), 0x00E9);
    ck_int("decode(\"é\") 前进 2", adv, 2);

    adv = -1;
    ck_int("decode(\"😀\") 码点 == U+1F600",
           tcwin_u8_decode("\xF0\x9F\x98\x80", &adv), 0x1F600);
    ck_int("decode(\"😀\") 前进 4", adv, 4);

    /* 截断的序列 -> -1，且前进 1（不能卡死） */
    adv = -1;
    ck_int("decode(\"\\xE4\") 截断 -> -1", tcwin_u8_decode("\xE4", &adv), -1);
    ck_int("decode(截断) 前进 1", adv, 1);

    adv = -1;
    ck_int("decode(\"\\xE4\\xB8\") 截断 -> -1", tcwin_u8_decode("\xE4\xB8", &adv), -1);

    /* 首字节后跟非续字节 -> -1 */
    adv = -1;
    ck_int("decode(\"\\xE4A\") -> -1", tcwin_u8_decode("\xE4" "A", &adv), -1);
    ck_int("decode(\"\\xE4A\") 前进 1", adv, 1);

    /* 超长编码：C0 80 表示 U+0000，非法 */
    adv = -1;
    ck_int("decode(超长 C0 80) -> -1", tcwin_u8_decode("\xC0\x80", &adv), -1);

    /* UTF-16 代理区不是合法 UTF-8：ED A0 80 == U+D800 */
    adv = -1;
    ck_int("decode(代理区 ED A0 80) -> -1", tcwin_u8_decode("\xED\xA0\x80", &adv), -1);

    /* 空串与 NULL */
    adv = -1;
    ck_int("decode(\"\") -> -1", tcwin_u8_decode("", &adv), -1);
    ck_int("decode(NULL) -> -1", tcwin_u8_decode(NULL, &adv), -1);

    /* adv 为 NULL 不应崩 */
    ck_int("decode(\"中\", NULL) 不崩",
           tcwin_u8_decode("\xE4\xB8\xAD", NULL), 0x4E2D);
}

/* ---------------------------------------------------------------- */
/* 3. 前进 / 后退一个码点                                            */
/* ---------------------------------------------------------------- */
static void test_prev_next(void)
{
    /* "a中b" = 61 E4 B8 AD 62，共 5 字节 */
    const char *s = "a\xE4\xB8\xAD" "b";
    int len = 5;

    ck_int("prev 位置 5 -> 4  [左移过 'b']", tcwin_u8_prev(s, 5), 4);
    ck_int("prev 位置 4 -> 1  [左移过 '中'（3 字节）]", tcwin_u8_prev(s, 4), 1);
    ck_int("prev 位置 1 -> 0  [左移过 'a']", tcwin_u8_prev(s, 1), 0);
    ck_int("prev 位置 0 -> 0  [不越界]", tcwin_u8_prev(s, 0), 0);
    ck_int("prev 位置 -5 -> 0  [负数保护]", tcwin_u8_prev(s, -5), 0);

    ck_int("next 位置 0 -> 1  [右移过 'a']", tcwin_u8_next(s, 0, len), 1);
    ck_int("next 位置 1 -> 4  [右移过 '中']", tcwin_u8_next(s, 1, len), 4);
    ck_int("next 位置 4 -> 5  [右移过 'b']", tcwin_u8_next(s, 4, len), 5);
    ck_int("next 位置 5 -> 5  [末尾不越界]", tcwin_u8_next(s, 5, len), 5);

    /* 落在序列中间时（畸形输入），prev 应退到首字节 */
    ck_int("prev 落在序列中间(3) -> 1", tcwin_u8_prev(s, 3), 1);
    ck_int("prev 落在序列中间(2) -> 1", tcwin_u8_prev(s, 2), 1);
}

/* ---------------------------------------------------------------- */
/* 4. 码点计数                                                       */
/* ---------------------------------------------------------------- */
static void test_count(void)
{
    ck_int("count(\"abc\",3) == 3",        tcwin_u8_count("abc", 3), 3);
    ck_int("count(\"中\",3) == 1",         tcwin_u8_count("\xE4\xB8\xAD", 3), 1);
    ck_int("count(\"a中b\",5) == 3",       tcwin_u8_count("a\xE4\xB8\xAD" "b", 5), 3);
    ck_int("count(\"中文\",6) == 2",       tcwin_u8_count("\xE4\xB8\xAD\xE6\x96\x87", 6), 2);
    ck_int("count(\"😀\",4) == 1",         tcwin_u8_count("\xF0\x9F\x98\x80", 4), 1);
    ck_int("count(空,0) == 0",             tcwin_u8_count("", 0), 0);
    /* 畸形字节按 1 个码点计，不能死循环 */
    ck_int("count(畸形 \\xE4, 1) == 1",    tcwin_u8_count("\xE4", 1), 1);
}

/* ---------------------------------------------------------------- */
/* 5. 显示宽度                                                       */
/* ---------------------------------------------------------------- */
static void test_width(void)
{
    ck_int("width('A') == 1",              tcwin_u8_width('A'), 1);
    ck_int("width(' ') == 1",              tcwin_u8_width(' '), 1);
    ck_int("width(中 U+4E2D) == 2",        tcwin_u8_width(0x4E2D), 2);
    ck_int("width(文 U+6587) == 2",        tcwin_u8_width(0x6587), 2);
    ck_int("width(，U+FF0C 全角逗号) == 2", tcwin_u8_width(0xFF0C), 2);
    ck_int("width(あ U+3042 平假名) == 2",  tcwin_u8_width(0x3042), 2);
    ck_int("width(한 U+D55C 谚文) == 2",   tcwin_u8_width(0xD55C), 2);
    ck_int("width(😀 U+1F600) == 2",       tcwin_u8_width(0x1F600), 2);
    ck_int("width(é U+00E9 拉丁) == 1",    tcwin_u8_width(0x00E9), 1);
    ck_int("width(组合记号 U+0301) == 0",   tcwin_u8_width(0x0301), 0);
    ck_int("width(零宽空格 U+200B) == 0",   tcwin_u8_width(0x200B), 0);
    ck_int("width(NUL) == 0",              tcwin_u8_width(0), 0);
    ck_int("width(控制符 0x01) == 0",       tcwin_u8_width(0x01), 0);
    ck_int("width(非法 -1) == 1",           tcwin_u8_width(-1), 1);

    /* 整串宽度 —— 这是光标定位真正要用的 */
    ck_int("串宽 \"abc\" == 3",            tcwin_u8_display_width("abc", 3), 3);
    ck_int("串宽 \"中文\" == 4",           tcwin_u8_display_width("\xE4\xB8\xAD\xE6\x96\x87", 6), 4);
    ck_int("串宽 \"a中b\" == 4",           tcwin_u8_display_width("a\xE4\xB8\xAD" "b", 5), 4);
    ck_int("串宽 \"中a\" == 3",            tcwin_u8_display_width("\xE4\xB8\xAD" "a", 4), 3);
    ck_int("串宽 \"😀😀\" == 4",           tcwin_u8_display_width("\xF0\x9F\x98\x80\xF0\x9F\x98\x80", 8), 4);
}

/* ---------------------------------------------------------------- */
/* 6. 编码（与解码互为逆运算）                                        */
/* ---------------------------------------------------------------- */
static void test_encode(void)
{
    char b[8];
    int n;

    memset(b, 0, sizeof b);
    n = tcwin_u8_encode('A', b, (int)sizeof b);
    ck_int("encode('A') 写 1 字节", n, 1);
    ck("encode('A') 内容正确", n == 1 && b[0] == 'A');

    memset(b, 0, sizeof b);
    n = tcwin_u8_encode(0x4E2D, b, (int)sizeof b);
    ck_int("encode(中) 写 3 字节", n, 3);
    ck("encode(中) 内容 == E4 B8 AD",
       n == 3 && (unsigned char)b[0] == 0xE4
               && (unsigned char)b[1] == 0xB8
               && (unsigned char)b[2] == 0xAD);

    memset(b, 0, sizeof b);
    n = tcwin_u8_encode(0x00E9, b, (int)sizeof b);
    ck("encode(é) 内容 == C3 A9",
       n == 2 && (unsigned char)b[0] == 0xC3 && (unsigned char)b[1] == 0xA9);

    memset(b, 0, sizeof b);
    n = tcwin_u8_encode(0x1F600, b, (int)sizeof b);
    ck("encode(😀) 内容 == F0 9F 98 80",
       n == 4 && (unsigned char)b[0] == 0xF0 && (unsigned char)b[1] == 0x9F
               && (unsigned char)b[2] == 0x98 && (unsigned char)b[3] == 0x80);

    /* 空间不足 */
    ck_int("encode(中) 容量 2 -> 0", tcwin_u8_encode(0x4E2D, b, 2), 0);
    ck_int("encode(中) 容量 0 -> 0", tcwin_u8_encode(0x4E2D, b, 0), 0);
    ck_int("encode(NULL) -> 0",      tcwin_u8_encode(0x4E2D, NULL, 8), 0);

    /* 代理区不能编码 */
    ck_int("encode(U+D800 代理) -> 0", tcwin_u8_encode(0xD800, b, 8), 0);
    ck_int("encode(0x110000 超限) -> 0", tcwin_u8_encode(0x110000, b, 8), 0);

    /* 往返：encode 后再 decode 应得回原码点 */
    {
        int cp;
        int adv;
        const int cases[] = { 0x41, 0x7F, 0x80, 0x7FF, 0x800, 0x4E2D, 0xFFFF, 0x10000, 0x1F600, 0x10FFFF };
        int i;
        int allok = 1;
        for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
            memset(b, 0, sizeof b);
            n = tcwin_u8_encode(cases[i], b, (int)sizeof b);
            if (n <= 0) { allok = 0; break; }
            adv = 0;
            cp = tcwin_u8_decode(b, &adv);
            if (cp != cases[i] || adv != n) { allok = 0; break; }
        }
        ck("encode -> decode 往返 10 个边界码点一致", allok);
    }
}

/* ---------------------------------------------------------------- */
/* 7. UTF-16 -> UTF-8（Windows 控制台给的就是 UTF-16）                */
/* ---------------------------------------------------------------- */
static void test_from_utf16(void)
{
    char b[64];
    int n;

    memset(b, 0, sizeof b);
    n = tcwin_u8_from_utf16((const unsigned short[]){ 'A', 'B' }, 2, b, (int)sizeof b);
    ck_int("utf16 \"AB\" -> 2 字节", n, 2);
    ck("utf16 \"AB\" 内容 == \"AB\"", n == 2 && strcmp(b, "AB") == 0);

    memset(b, 0, sizeof b);
    n = tcwin_u8_from_utf16((const unsigned short[]){ 0x4E2D }, 1, b, (int)sizeof b);
    ck_int("utf16 \"中\" -> 3 字节", n, 3);
    ck("utf16 \"中\" == E4 B8 AD",
       n == 3 && (unsigned char)b[0] == 0xE4
               && (unsigned char)b[1] == 0xB8
               && (unsigned char)b[2] == 0xAD);

    /* 代理对：U+1F600 = D83D DE00 */
    memset(b, 0, sizeof b);
    n = tcwin_u8_from_utf16((const unsigned short[]){ 0xD83D, 0xDE00 }, 2, b, (int)sizeof b);
    ck_int("utf16 代理对 D83D DE00 -> 4 字节", n, 4);
    ck("utf16 代理对 == F0 9F 98 80",
       n == 4 && (unsigned char)b[0] == 0xF0 && (unsigned char)b[1] == 0x9F
               && (unsigned char)b[2] == 0x98 && (unsigned char)b[3] == 0x80);

    /* 落单的高位代理 -> U+FFFD */
    memset(b, 0, sizeof b);
    n = tcwin_u8_from_utf16((const unsigned short[]){ 0xD83D }, 1, b, (int)sizeof b);
    ck("utf16 落单代理 -> U+FFFD (EF BF BD)",
       n == 3 && (unsigned char)b[0] == 0xEF
               && (unsigned char)b[1] == 0xBF
               && (unsigned char)b[2] == 0xBD);

    /* 长度限制不能越界写 */
    memset(b, 0x7F, sizeof b);
    n = tcwin_u8_from_utf16((const unsigned short[]){ 0x4E2D, 0x4E2D, 0x4E2D }, 3, b, 4);
    ck("utf16 容量 4 时不越界且以 NUL 结尾", n <= 3 && b[n] == 0);

    ck_int("utf16(NULL) -> 0", tcwin_u8_from_utf16(NULL, 3, b, 8), 0);
    ck_int("utf16 长度 0 -> 0", tcwin_u8_from_utf16((const unsigned short[]){ 'A' }, 0, b, 8), 0);
}

/* ---------------------------------------------------------------- */
/* 8. 编辑操作模拟（真正的用例：退格/删除/左右移）                     */
/* ---------------------------------------------------------------- */
static void test_edit_simulation(void)
{
    /* 模拟 "a中b"（5 字节）上的编辑操作，确认每次操作都跨整个码点 */
    const char *s = "a\xE4\xB8\xAD" "b";
    char buf[32];
    int len, cur;

    /* 光标在末尾（5），退格应删掉 'b' -> 长度 4 */
    memcpy(buf, s, 6);
    len = 5; cur = 5;
    cur = tcwin_u8_prev(buf, cur);
    memmove(buf + cur, buf + len, 1);          /* 把 NUL 前移 */
    len = cur;
    ck_int("退格删 'b' 后长度 4", len, 4);
    ck("退格删 'b' 后内容 \"a中\"",
       memcmp(buf, "a\xE4\xB8\xAD", 4) == 0);

    /* 再退格应删掉整个 '中'（3 字节）-> 长度 1 */
    cur = tcwin_u8_prev(buf, len);
    ck_int("再退格把光标移到 1（跨过 3 字节的 '中'）", cur, 1);
    memmove(buf + cur, buf + len, 1);
    len = cur;
    ck_int("退格删 '中' 后长度 1", len, 1);
    ck("退格删 '中' 后内容 \"a\"", strcmp(buf, "a") == 0);

    /* 从位置 1 右移应跨过 '中' 到 4 */
    memcpy(buf, s, 6);
    len = 5;
    ck_int("从 1 右移跨过 '中' 到 4", tcwin_u8_next(buf, 1, len), 4);

    /* 从位置 1 删除（Delete 键）应删掉整个 '中' */
    cur = 1;
    {
        int nx = tcwin_u8_next(buf, cur, len);
        ck_int("Delete 的跨度 == 3 字节", nx - cur, 3);
        memmove(buf + cur, buf + nx, (size_t)(len - nx + 1));
        len -= (nx - cur);
        ck_int("Delete 后长度 2", len, 2);
        ck("Delete 后内容 \"ab\"", memcmp(buf, "ab", 2) == 0);
    }

    /* 光标列数：位置 4（在 '中' 之后）应显示在第 3 列（1 + 2） */
    memcpy(buf, s, 6);
    ck_int("光标在字节 4 时占 3 列", tcwin_u8_display_width(buf, 4), 3);
    ck_int("光标在字节 1 时占 1 列", tcwin_u8_display_width(buf, 1), 1);
    ck_int("光标在字节 5 时占 4 列", tcwin_u8_display_width(buf, 5), 4);
}

int main(void)
{
    printf("=== probe_rl_utf8: 行编辑器 UTF-8 纯逻辑 ===\n");

    test_seqlen();
    test_decode();
    test_prev_next();
    test_count();
    test_width();
    test_encode();
    test_from_utf16();
    test_edit_simulation();

    printf("=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
