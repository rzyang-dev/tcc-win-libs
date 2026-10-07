/* probe_utf8lib.c —— utf8.h 的功能验证（此前无探针覆盖）
 *
 * API 名全部**从 include\utf8.h 读出**，不是猜的（上一轮我猜 utf8_sz /
 * utf8_valid 导致编译失败，这次先读了头文件）：
 *
 *     size_t       utf8len(const utf8_int8_t *str);          L166
 *     size_t       utf8nlen(const utf8_int8_t *str, size_t n); L169
 *     utf8_int8_t *utf8valid(const utf8_int8_t *str);        L246
 *     utf8_int8_t *utf8codepoint(const utf8_int8_t *str,
 *                                utf8_int32_t *out_codepoint); L260
 *
 * 用法：scripts\tcc.cmd examples\probes\probe_utf8lib.c -With utf8 -Run
 */

#include "tccwin.h"

#include <stdio.h>
#include <string.h>

#include "utf8.h"

static int pass = 0, fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) { ++pass; printf("  [OK]   %s\n", what); }
    else    { ++fail; printf("  [FAIL] %s\n", what); }
}

static void ck_int(const char *what, int got, int want)
{
    if (got == want) { ++pass; printf("  [OK]   %s\n", what); }
    else { ++fail; printf("  [FAIL] %-46s 得到 %d，期望 %d\n", what, got, want); }
}

int main(void)
{
    SetConsoleOutputCP(CP_UTF8);
    printf("=== probe_utf8lib: utf8.h 功能验证 ===\n");

    printf("\n-- 1. utf8len：按**字符数**（不是字节数）计数 --\n");
    /* "a中z" = 'a' + U+4E2D + 'z' = 3 个字符，但 5 个字节 */
    {
        static const char *s = "a\xE4\xB8\xADz";
        ck_int("strlen(\"a中z\") == 5（字节）", (int)strlen(s), 5);
        ck_int("utf8len(\"a中z\") == 3（字符）", (int)utf8len(s), 3);
    }
    ck_int("utf8len(\"\") == 0", (int)utf8len(""), 0);
    ck_int("utf8len(\"abc\") == 3", (int)utf8len("abc"), 3);
    /* 纯中文 4 字符 = 12 字节 */
    {
        static const char *s = "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97\xE7\xAC\xA6";
        ck_int("strlen(4 个汉字) == 12", (int)strlen(s), 12);
        ck_int("utf8len(4 个汉字) == 4", (int)utf8len(s), 4);
    }
    /* emoji U+1F600 = 4 字节 1 字符 */
    {
        static const char *s = "\xF0\x9F\x98\x80";
        ck_int("utf8len(1 个 emoji) == 1", (int)utf8len(s), 1);
        ck_int("strlen(1 个 emoji) == 4", (int)strlen(s), 4);
    }

    printf("\n-- 2. utf8valid：合法性判定（返回 0 表示合法）--\n");
    ck("合法 ASCII 返回 0", utf8valid("abc") == 0);
    ck("合法 UTF-8 汉字返回 0", utf8valid("\xE4\xB8\xAD") == 0);
    ck("合法 emoji 返回 0", utf8valid("\xF0\x9F\x98\x80") == 0);
    /* 非法序列：0xFF 在 UTF-8 里永远不合法 */
    ck("utf8valid(\\xFF) **非** 0（判为非法）", utf8valid("\xFF") != 0);
    ck("utf8valid(\\xFE) **非** 0（判为非法）", utf8valid("\xFE") != 0);
    /* 截断的 3 字节序列（只有前 2 个字节） */
    ck("utf8valid(截断的 3 字节序列) **非** 0", utf8valid("\xE4\xB8") != 0);
    /* 合法前缀 + 非法后缀：必须报出非法位置（非 0） */
    ck("utf8valid(\"ab\\xFF\") **非** 0", utf8valid("ab\xFF") != 0);

    printf("\n-- 3. utf8codepoint：逐字符解码，检查码点值 --\n");
    {
        /* "A中" -> 码点应为 0x41 与 0x4E2D */
        static const char *s = "A\xE4\xB8\xAD";
        const utf8_int8_t *p = (const utf8_int8_t *)s;
        utf8_int32_t cp = 0;
        int n = 0;

        p = utf8codepoint(p, &cp);
        ck_int("第 1 个码点 == 0x41 ('A')", (int)cp, 0x41);
        n++;

        p = utf8codepoint(p, &cp);
        ck_int("第 2 个码点 == 0x4E2D ('中')", (int)cp, 0x4E2D);
        n++;

        ck_int("解出 2 个码点", n, 2);
        ck("走完后到达字符串结尾", *p == 0);
    }
    {
        /* emoji 的码点必须是 0x1F600（4 字节路径） */
        static const char *s = "\xF0\x9F\x98\x80";
        utf8_int32_t cp = 0;
        utf8codepoint((const utf8_int8_t *)s, &cp);
        printf("       emoji 码点 = U+%04X\n", (unsigned)cp);
        ck("emoji 码点 == 0x1F600", cp == 0x1F600);
    }
    {
        /* 两字节序列：U+00E9 = C3 A9 */
        static const char *s = "\xC3\xA9";
        utf8_int32_t cp = 0;
        utf8codepoint((const utf8_int8_t *)s, &cp);
        ck("2 字节序列 C3A9 == U+00E9", cp == 0x00E9);
        ck_int("utf8len(C3A9) == 1", (int)utf8len(s), 1);
    }

    printf("\n-- 4. utf8len 与逐字符解码的**交叉校验** --\n");
    /* 这是最有价值的一条：两种独立路径必须给出一致的字符数 */
    {
        static const char *s = "a\xE4\xB8\xAD\xF0\x9F\x98\x80z";  /* a 中 emoji z */
        const utf8_int8_t *p = (const utf8_int8_t *)s;
        utf8_int32_t cp = 0;
        int counted = 0;
        while (*p) { p = utf8codepoint(p, &cp); counted++; }
        printf("       utf8len=%d  逐字符解码=%d  strlen=%d\n",
               (int)utf8len(s), counted, (int)strlen(s));
        ck_int("utf8len 与逐字符解码一致", (int)utf8len(s), counted);
        ck_int("两条路径都得到 4 个字符", counted, 4);
    }

    printf("\n-- 5. utf8nlen：限制字节数的计数 --\n");
    {
        /* "a中z"：只允许读前 2 字节（'a' + 中的首字节）*/
        static const char *s = "a\xE4\xB8\xADz";
        /* 只读 1 字节 -> 只能数到 'a' */
        ck_int("utf8nlen(\"a中z\", 1) == 1", (int)utf8nlen(s, 1), 1);
        /* 读满整个串 -> 3 */
        ck_int("utf8nlen(\"a中z\", 5) == 3", (int)utf8nlen(s, 5), 3);
    }

    printf("\n-- 6. 大小写转换（多字节应保持不变）--\n");
    {
        char buf[32];
        /* ASCII 应当转换 */
        strcpy(buf, "AbC");
        utf8lwr((utf8_int8_t *)buf);
        ck("utf8lwr(\"AbC\") == \"abc\"", strcmp(buf, "abc") == 0);

        strcpy(buf, "abc");
        utf8upr((utf8_int8_t *)buf);
        ck("utf8upr(\"abc\") == \"ABC\"", strcmp(buf, "ABC") == 0);

        /* 汉字没有大小写，必须**逐字节不变** */
        strcpy(buf, "\xE4\xB8\xAD");
        utf8lwr((utf8_int8_t *)buf);
        ck("utf8lwr 对汉字逐字节不变",
           memcmp(buf, "\xE4\xB8\xAD", 3) == 0 && buf[3] == 0);
    }

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
