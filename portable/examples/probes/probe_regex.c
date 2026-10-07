/* tiny-regex-c 正则表达式验证
 *
 * 库：kokke/tiny-regex-c（Unlicense / 公有领域，re.c 14 KB + re.h）
 * 接口极简，只有三个函数：
 *     re_t re_compile(const char* pattern);
 *     int  re_matchp(re_t pattern, const char* text, int* matchlength);
 *     int  re_match(const char* pattern, const char* text, int* matchlength);
 *
 * 返回的是**匹配起始下标**（-1 表示不匹配），matchlength 是匹配长度。
 * 注意：它**不支持捕获组**（没有分组提取），这是设计上的取舍。
 *
 * 本探针的目的不只是"能编译"，而是**摸清实际支持哪些语法** ——
 * 很多 tiny regex 库只支持子集，文档往往说不清。所以这里逐条枚举。
 *
 * 编译：scripts\tcc.cmd examples\probes\probe_regex.c -With regex -Run
 */
#include "tccwin.h"
#include <stdio.h>
#include <string.h>

#include "re.h"

static int pass = 0, fail = 0, known = 0;

/* 记录库的**已知限制**（不是本环境的问题，但仍要显式列出，
 * 免得使用者以为是自己用错）。
 * 与 fail 分开统计：known 不为 0 时退出码仍为 0。 */
static void expect_unsupported(const char *pattern, const char *text, const char *desc) {
    int len = -1;
    int pos = re_match(pattern, text, &len);
    printf("  [限制] %-34s \"%s\" on \"%s\" -> pos=%d（库不支持，符合已知限制）\n",
           desc, pattern, text, pos);
    known++;
}
#define CK(d, c) do { if (c) { printf("  [OK]   %s\n", d); pass++; } \
                      else   { printf("  [FAIL] %s\n", d); fail++; } } while (0)

/* 断言：在 text 中匹配 pattern，期望命中且起始位置与长度符合预期 */
static void expect_at(const char *pattern, const char *text,
                      int want_pos, int want_len, const char *desc) {
    int len = -1;
    int pos = re_match(pattern, text, &len);
    int ok = (pos == want_pos) && (want_pos < 0 || len == want_len);
    if (ok) {
        printf("  [OK]   %-34s \"%s\" on \"%s\"\n", desc, pattern, text);
        pass++;
    } else {
        printf("  [FAIL] %-34s \"%s\" on \"%s\"  得到 pos=%d len=%d，期望 pos=%d len=%d\n",
               desc, pattern, text, pos, len, want_pos, want_len);
        fail++;
    }
}

/* 断言：匹配成功（位置 >= 0） */
static void expect_match(const char *pattern, const char *text, const char *desc) {
    int len = -1;
    int pos = re_match(pattern, text, &len);
    if (pos >= 0) {
        printf("  [OK]   %-34s \"%s\" on \"%s\" -> pos=%d len=%d\n",
               desc, pattern, text, pos, len);
        pass++;
    } else {
        printf("  [FAIL] %-34s \"%s\" on \"%s\"  未匹配\n", desc, pattern, text);
        fail++;
    }
}

/* 断言：不匹配 */
static void expect_nomatch(const char *pattern, const char *text, const char *desc) {
    int len = -1;
    int pos = re_match(pattern, text, &len);
    if (pos < 0) {
        printf("  [OK]   %-34s \"%s\" on \"%s\" -> 正确不匹配\n", desc, pattern, text);
        pass++;
    } else {
        printf("  [FAIL] %-34s \"%s\" on \"%s\"  意外匹配 pos=%d\n",
               desc, pattern, text, pos);
        fail++;
    }
}

int main(void) {
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);    printf("=== tiny-regex-c 正则表达式验证 ===\n\n");

    /* ---------- 字面量 ---------- */
    printf("-- 字面量匹配 --\n");
    expect_at("abc", "xabcy", 1, 3, "字面量子串");
    expect_at("abc", "abc", 0, 3, "从头匹配");
    expect_nomatch("abc", "abx", "字面量不匹配");
    expect_at("a", "bbb aaa", 4, 1, "单字符");
    printf("\n");

    /* ---------- 锚点 ---------- */
    printf("-- 锚点 ^ 与 $ --\n");
    expect_at("^abc", "abcdef", 0, 3, "^ 锚定开头");
    expect_nomatch("^abc", "xabc", "^ 不匹配中间");
    expect_at("def$", "abcdef", 3, 3, "$ 锚定结尾");
    expect_nomatch("def$", "defx", "$ 不匹配中间");
    expect_at("^abc$", "abc", 0, 3, "^...$ 全串");
    expect_nomatch("^abc$", "abcd", "^...$ 拒绝超长");
    printf("\n");

    /* ---------- 字符类 ---------- */
    printf("-- 字符类 --\n");
    expect_match("[abc]", "xby", "字符类 [abc]");
    expect_match("[a-z]", "123x", "范围 [a-z]");
    expect_match("[0-9]", "ab3", "数字范围 [0-9]");
    expect_nomatch("[0-9]", "abc", "[0-9] 对字母不匹配");
    expect_match("[^0-9]", "123a", "否定类 [^0-9]");
    expect_match("[A-Za-z0-9_]", "  x", "标识符字符类");
    printf("\n");

    /* ---------- 预定义类 ---------- */
    printf("-- 预定义类 \\d \\D \\w \\W \\s \\S --\n");
    expect_match("\\d", "ab1", "\\d 数字");
    expect_nomatch("\\d", "abc", "\\d 对字母不匹配");
    expect_match("\\D", "1a", "\\D 非数字");
    expect_match("\\w", " a", "\\w 词字符");
    expect_match("\\W", "ab ", "\\W 非词字符");
    expect_match("\\s", "ab c", "\\s 空白");
    expect_match("\\S", "  a", "\\S 非空白");
    printf("\n");

    /* ---------- 点号 ---------- */
    printf("-- 点号 . --\n");
    expect_at("a.c", "abc", 0, 3, ". 匹配任意字符");
    expect_at("a.c", "a c", 0, 3, ". 匹配空格");
    printf("\n");

    /* ---------- 量词 ---------- */
    printf("-- 量词 * + ? --\n");
    expect_match("ab*c", "ac", "* 零次");
    expect_match("ab*c", "abbbc", "* 多次");
    expect_match("ab+c", "abc", "+ 一次以上");
    expect_nomatch("ab+c", "ac", "+ 要求至少一次");
    expect_match("ab?c", "ac", "? 零次");
    expect_match("ab?c", "abc", "? 一次");
    expect_nomatch("ab?c", "abbc", "? 至多一次");
    printf("\n");

    /* ---------- 转义 ---------- */
    printf("-- 转义 --\n");
    expect_match("\\.", "a.b", "转义点号");
    expect_match("a\\.c", "a.c", "转义点在字面量中");
    expect_nomatch("a\\.c", "abc", "转义点号不匹配任意字符");
    expect_match("\\*", "a*b", "转义星号");
    printf("\n");

    /* ---------- 交替（已知限制） ----------
     * tiny-regex-c **不支持**分支/交替 `|`。证据：
     *   re.c:135 有注释掉的实现：
     *     case '|': { re_compiled[j].type = BRANCH; } break;  <-- not working properly
     *   README 的 TODO 第一条：Fix implementation of branches (`|`)
     * 但同一个 README 又把 `|` 列在"支持的运算符"里 —— 文档与实现不一致。
     * 需要交替时请改用字符类，或换用其它正则库。 */
    printf("-- 交替 | （已知限制，非本环境问题） --\n");
    expect_unsupported("cat|dog", "a dog here", "交替（库未实现）");
    expect_unsupported("cat|dog", "a cat here", "交替（库未实现）");
    printf("\n");

    /* ---------- re_compile + re_matchp（预编译路径） ---------- */
    printf("-- 预编译路径 re_compile / re_matchp --\n");
    re_t pat = re_compile("^[0-9]+$");
    CK("re_compile 返回非空", pat != 0);
    if (pat) {
        int len = -1;
        int p1 = re_matchp(pat, "12345", &len);
        int p2 = re_matchp(pat, "12a45", &len);
        printf("       纯数字 \"12345\" -> pos=%d\n", p1);
        printf("       含字母 \"12a45\" -> pos=%d\n", p2);
        CK("预编译匹配纯数字串", p1 == 0);
        CK("预编译拒绝含字母串", p2 < 0);

        /* 同一模式复用多次 */
        int cnt = 0;
        const char *tests[4];
        tests[0] = "1"; tests[1] = "999"; tests[2] = "x"; tests[3] = "42";
        for (int i = 0; i < 4; i++) {
            if (re_matchp(pat, tests[i], &len) == 0) cnt++;
        }
        printf("       复用 4 次，命中 %d 次（期望 3）\n", cnt);
        CK("模式可安全复用", cnt == 3);
    }
    printf("\n");

    /* ---------- 大小写敏感 ---------- */
    printf("-- 大小写敏感 --\n");
    expect_nomatch("abc", "ABC", "默认大小写敏感");
    expect_match("[Aa]bc", "Abc", "字符类覆盖大小写");
    printf("\n");

    /* ---------- 实际场景 ---------- */
    printf("-- 实际场景 --\n");
    expect_match("^[A-Za-z_][A-Za-z0-9_]*$", "my_var1", "C 标识符");
    expect_nomatch("^[A-Za-z_][A-Za-z0-9_]*$", "1var", "标识符不能数字开头");
    expect_match("^[0-9]+\\.[0-9]+$", "3.14", "浮点数");
    expect_nomatch("^[0-9]+\\.[0-9]+$", "3.", "浮点数需小数位");
    expect_match("^[0-9a-fA-F]+$", "DEADbeef", "十六进制");
    expect_match("^.*@.*\\..*$", "user@example.com", "简单邮箱形状");
    expect_match("^https?://", "https://x", "URL 前缀 http/https");
    expect_match("^https?://", "http://x", "URL 前缀 http");
    expect_nomatch("^https?://", "ftp://x", "URL 前缀拒绝 ftp");
    printf("\n");

    /* ---------- 边界情况 ---------- */
    printf("-- 边界情况 --\n");
    {
        int len = -1;
        int p = re_match("", "abc", &len);
        printf("       空模式 on \"abc\" -> pos=%d len=%d\n", p, len);
        CK("空模式不崩溃", 1);

        len = -1;
        p = re_match("a*", "", &len);
        printf("       \"a*\" on 空串 -> pos=%d len=%d\n", p, len);
        CK("\"a*\" 对空串不崩溃", 1);

        len = -1;
        p = re_match("abc", "", &len);
        printf("       \"abc\" on 空串 -> pos=%d\n", p);
        CK("\"abc\" 对空串不匹配", p < 0);

        len = -1;
        p = re_match("zzz", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", &len);
        CK("长文本不匹配不崩溃", p < 0);

        /* 匹配长度必须精确 */
        len = -1;
        p = re_match("[0-9]+", "abc12345def", &len);
        printf("       \"[0-9]+\" 匹配长度 = %d（期望 5）\n", len);
        CK("量词匹配长度正确", p == 3 && len == 5);
    }
    printf("\n");

    printf("=== %d 通过, %d 失败", pass, fail);
    if (known) printf(", %d 项已知限制", known);
    printf(" ===\n");
    return fail ? 1 : 0;
}
