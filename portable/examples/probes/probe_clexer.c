/* probe_clexer.c —— stb_c_lexer 的功能验证（此前无探针覆盖）
 *
 * API 名全部**从 include\stb_c_lexer.h 读出**，不是猜的：
 *
 *     enum { CLEX_eof = 256, CLEX_parse_error, CLEX_intlit, CLEX_floatlit,
 *            CLEX_id, CLEX_dqstring, CLEX_sqstring, CLEX_charlit, ... }   L178-202
 *     extern void stb_c_lexer_init(stb_lexer *, const char *, ...);       L145
 *     extern int  stb_c_lexer_get_token(stb_lexer *);                     L153
 *     字段: double real_number; long int_number;                          L129-130
 *
 * （上一轮我猜的 CLEX_qliteral 不存在，编译失败 —— 所以这次先读了头。）
 *
 * 用法：scripts\tcc.cmd examples\probes\probe_clexer.c -With stb_c_lexer -Run
 */

#include "tccwin.h"

#include <stdio.h>
#include <string.h>

#include "stb_c_lexer.h"

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

/* 数一遍各类 token（带防死循环上限） */
static int count_tokens(const char *src, int *t_id, int *t_int, int *t_flt,
                        int *t_eq, int *t_semi, int *t_str, int *t_err,
                        long *last_int, double *last_flt)
{
    stb_lexer lex;
    char store[2048];
    int tok, guard = 0, total = 0;

    *t_id = *t_int = *t_flt = *t_eq = *t_semi = *t_str = *t_err = 0;
    if (last_int) *last_int = 0;
    if (last_flt) *last_flt = 0.0;

    stb_c_lexer_init(&lex, src, src + strlen(src), store, sizeof store);
    while (stb_c_lexer_get_token(&lex) != 0 && guard++ < 500) {
        /* ⚠️ 关键：get_token 返回的是 **0/非 0**（是否还有 token），
         * 真正的 token ID 在 lexer->token 里！
         * 第一版我把返回值当 token ID 用，导致所有计数都是 0。
         * 见 stb_c_lexer.h L153-156 的文档注释。 */
        tok = (int)lex.token;
        total++;
        switch (tok) {
            case CLEX_id:       (*t_id)++;   break;
            case CLEX_intlit:   (*t_int)++;  if (last_int) *last_int = lex.int_number; break;
            case CLEX_floatlit: (*t_flt)++;  if (last_flt) *last_flt = lex.real_number; break;
            case CLEX_dqstring: (*t_str)++;  break;
            case '=':           (*t_eq)++;   break;
            case ';':           (*t_semi)++; break;
            case CLEX_parse_error: (*t_err)++; break;
            default: break;
        }
    }
    return total;
}

int main(void)
{
    SetConsoleOutputCP(CP_UTF8);
    printf("=== probe_clexer: stb_c_lexer 功能验证 ===\n");

    printf("\n-- 1. 基本词法：声明语句 --\n");
    {
        int id, i, f, eq, semi, str, err;
        long li; double lf;
        count_tokens("int x = 42;", &id, &i, &f, &eq, &semi, &str, &err, &li, &lf);
        printf("       id=%d int=%d float=%d '='=%d ';'=%d err=%d\n",
               id, i, f, eq, semi, err);
        ck_int("识别出 2 个标识符（int, x）", id, 2);
        ck_int("识别出 1 个整数字面量", i, 1);
        ck_int("识别出 1 个 '='", eq, 1);
        ck_int("识别出 1 个 ';'", semi, 1);
        ck_int("无解析错误", err, 0);
        ck("整数字面量的**值** == 42", li == 42);
        printf("       int_number=%ld\n", li);
    }

    printf("\n-- 2. 浮点字面量 --\n");
    {
        int id, i, f, eq, semi, str, err;
        long li; double lf;
        count_tokens("y = 3.5;", &id, &i, &f, &eq, &semi, &str, &err, &li, &lf);
        ck_int("识别出 1 个浮点字面量", f, 1);
        ck_int("没有把它当成整数字面量", i, 0);
        ck("浮点字面量的**值** ≈ 3.5", lf > 3.49 && lf < 3.51);
        printf("       real_number=%g\n", lf);
    }

    printf("\n-- 3. 注释必须被跳过 --\n");
    {
        int id, i, f, eq, semi, str, err;
        long li; double lf;
        /* 4 个 id：int, x, 然后是注释、再 y */
        count_tokens("int x; /* comment */ y;", &id, &i, &f, &eq, &semi, &str, &err, &li, &lf);
        printf("       id=%d ';'=%d err=%d\n", id, semi, err);
        ck_int("注释里的词没被当成标识符（id==3: int,x,y）", id, 3);
        ck_int("识别出 2 个分号", semi, 2);
        ck_int("注释没有造成解析错误", err, 0);
    }

    printf("\n-- 4. 字符串字面量（CLEX_dqstring）--\n");
    {
        int id, i, f, eq, semi, str, err;
        long li; double lf;
        count_tokens("s = \"hello\";", &id, &i, &f, &eq, &semi, &str, &err, &li, &lf);
        ck_int("识别出 1 个双引号字符串", str, 1);
        ck_int("字符串没有造成解析错误", err, 0);
    }

    printf("\n-- 5. 十六进制与八进制整数 --\n");
    {
        int id, i, f, eq, semi, str, err;
        long li; double lf;
        count_tokens("a = 0x1F;", &id, &i, &f, &eq, &semi, &str, &err, &li, &lf);
        ck_int("0x1F 识别为整数字面量", i, 1);
        ck("0x1F 的值为 31", li == 31);
        printf("       0x1F -> %ld\n", li);

        count_tokens("b = 010;", &id, &i, &f, &eq, &semi, &str, &err, &li, &lf);
        ck_int("八进制 010 识别为整数字面量", i, 1);
        ck("八进制 010 的值为 8", li == 8);
        printf("       010 -> %ld\n", li);
    }

    printf("\n-- 6. 多字符运算符（CLEX_eq 是 '=='，不是 '='）--\n");
    {
        int id, i, f, eq, semi, str, err;
        long li; double lf;
        int total;
        total = count_tokens("a == b;", &id, &i, &f, &eq, &semi, &str, &err, &li, &lf);
        printf("       total=%d id=%d '='(单)=%d err=%d\n", total, id, eq, err);
        ck_int("'==' 被识别为单个 CLEX_eq，不是两个 '='", eq, 0);
        ck_int("无解析错误", err, 0);
        /* a, ==, b, ; = 4 个 token */
        ck_int("共 4 个 token", total, 4);
    }

    printf("\n-- 7. 畸形输入必须报 CLEX_parse_error --\n");
    {
        int id, i, f, eq, semi, str, err;
        long li; double lf;
        /* 未闭合的字符串 */
        count_tokens("s = \"unterminated", &id, &i, &f, &eq, &semi, &str, &err, &li, &lf);
        ck("未闭合字符串报 CLEX_parse_error", err >= 1);
        printf("       未闭合字符串: err=%d\n", err);
    }

    printf("\n-- 8. 空输入 --\n");
    {
        int id, i, f, eq, semi, str, err;
        long li; double lf;
        int total = count_tokens("", &id, &i, &f, &eq, &semi, &str, &err, &li, &lf);
        ck_int("空输入产生 0 个 token", total, 0);
        ck_int("空输入无错误", err, 0);
    }

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
